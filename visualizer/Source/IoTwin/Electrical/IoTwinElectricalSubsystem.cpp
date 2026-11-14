// IoTwin - electrical simulation of the scene (in-process adapter, ADR 0008).

#include "Electrical/IoTwinElectricalSubsystem.h"

#include "Core/GM_Simulation.h"
#include "Electrical/ElectricalElement.h"
#include "Electrical/ElectricalModelBuilder.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IoTwin.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Systems/ElectricalDeviceComponent.h"
#include "Systems/ElectricalPortComponent.h"

#include <atomic>

/**
 * Simulation thread: steps the session with a fixed time step in real time.
 * It only touches the session (lock-free towards the game thread) and never
 * waits for a frame. After a hitch (debugger, loading) it does not try to
 * catch up in a burst.
 */
class FElectricalSimulationThread : public FRunnable
{
public:
	FElectricalSimulationThread(ElSession* InSession, double InStep) : Session(InSession), Step(InStep)
	{
		Thread = FRunnableThread::Create(this, TEXT("IoTwinElectrical"), 0, TPri_AboveNormal);
	}

	virtual ~FElectricalSimulationThread() override
	{
		if (Thread)
		{
			Thread->Kill(/*bShouldWait=*/true);
			delete Thread;
		}
	}

	virtual uint32 Run() override
	{
		double Next = FPlatformTime::Seconds();
		while (!bStop.load())
		{
			const double Start = FPlatformTime::Seconds();
			el_session_step(Session, Step);
			const double End = FPlatformTime::Seconds();
			LastStepNanoseconds.store(static_cast<int64>((End - Start) * 1e9));
			Next += Step;
			if (End - Next > 0.25)
			{
				Next = End;
			}
			else if (Next > End)
			{
				FPlatformProcess::SleepNoStats(static_cast<float>(Next - End));
			}
		}
		return 0;
	}

	virtual void Stop() override { bStop.store(true); }

	bool IsValid() const { return Thread != nullptr; }

	std::atomic<int64> LastStepNanoseconds{0};

private:
	ElSession* Session;
	double Step;
	std::atomic<bool> bStop{false};
	FRunnableThread* Thread = nullptr;
};

void UIoTwinElectricalSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bEnabled = !FParse::Param(FCommandLine::Get(), TEXT("IoTwinElectricalDisabled"));
	double After = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("IoTwinShortCircuitAfter="), After) && After > 0.0)
	{
		ShortCircuitAfter = After;
	}
	StartTime = FPlatformTime::Seconds();
}

void UIoTwinElectricalSubsystem::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

bool UIoTwinElectricalSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UIoTwinElectricalSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UIoTwinElectricalSubsystem, STATGROUP_Tickables);
}

UIoTwinElectricalSubsystem* UIoTwinElectricalSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UIoTwinElectricalSubsystem>() : nullptr;
}

void UIoTwinElectricalSubsystem::Stop()
{
	// The thread goes first: it is the only other user of the session.
	delete Runner;
	Runner = nullptr;
	el_session_destroy(Session);
	Session = nullptr;
	DeviceIds.Reset();
	States.Reset();
	PortIds.Reset();
	Elements.Reset();
	bShortCircuit = false;
}

void UIoTwinElectricalSubsystem::Rebuild()
{
	Stop();
	bTopologyDirty = false;
	UWorld* World = GetWorld();
	if (!bEnabled || World == nullptr)
	{
		return;
	}

	ElModel Model;
	el_model_init(&Model);
	FElectricalModelBuilder Builder(Model);

	// 1. Every port is a junction (L, N, PE).
	TArray<UElectricalPortComponent*> Ports;
	TArray<IElectricalElement*> Found;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TArray<UElectricalPortComponent*> ActorPorts;
		Actor->GetComponents(ActorPorts);
		Ports.Append(ActorPorts);
		if (IElectricalElement* Element = Cast<IElectricalElement>(Actor))
		{
			Found.Add(Element);
			Elements.Add(Actor);
		}
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (IElectricalElement* Element = Cast<IElectricalElement>(Component))
			{
				Found.Add(Element);
				Elements.Add(Component);
			}
		}
	}
	for (const UElectricalPortComponent* Port : Ports)
	{
		const FString Id = Builder.AddPort(Port);
		if (!Id.IsEmpty())
		{
			PortIds.Add(Port, Id);
		}
	}

	// 2. Supplies, loads, cables, ...
	for (IElectricalElement* Element : Found)
	{
		Element->DescribeElectrical(Builder);
	}

	// 3. Remaining wires between ports: short links for L, N and PE.
	for (const UElectricalPortComponent* Port : Ports)
	{
		for (const UElectricalPortComponent* Other : Port->GetConnections())
		{
			if (Other == nullptr || Other < Port || Builder.IsWireClaimed(Port, Other) || !PortIds.Contains(Other))
			{
				continue; // each wire once
			}
			Builder.ClaimWire(Port, Other);
			const FString A = Builder.PortId(Port);
			const FString B = Builder.PortId(Other);
			for (const TCHAR* Terminal : {TEXT("L"), TEXT("N"), TEXT("PE")})
			{
				Builder.Connect(A + TEXT(":") + Terminal, B + TEXT(":") + Terminal, Terminal);
			}
		}
	}

	bool bHasSupply = false;
	for (size_t i = 0; i < Model.device_count; ++i)
	{
		bHasSupply |= Model.devices[i].type == EL_SUPPLY;
	}
	if (!bHasSupply)
	{
		el_model_free(&Model);
		UE_LOG(LogIoTwin, Log, TEXT("Electrical simulation: no supply in the scene, not started"));
		return;
	}

	const size_t DeviceCount = Model.device_count;
	const size_t ConnectionCount = Model.connection_count;
	Session = el_session_create(&Model);
	if (Session == nullptr)
	{
		UE_LOG(LogIoTwin, Error, TEXT("Electrical simulation: out of memory"));
		return;
	}
	for (size_t i = 0; i < DeviceCount; ++i)
	{
		DeviceIds.Add(UTF8_TO_TCHAR(el_session_device_id(Session, i)), static_cast<int32>(i));
	}
	States.SetNum(static_cast<int32>(DeviceCount));
	Runner = new FElectricalSimulationThread(Session, FMath::Max(0.001, static_cast<double>(StepSeconds)));
	UE_LOG(LogIoTwin, Log, TEXT("Electrical simulation started: %d devices, %d conductors, %d elements, step %.0f ms%s"),
		static_cast<int32>(DeviceCount), static_cast<int32>(ConnectionCount), Elements.Num(), StepSeconds * 1000.0f,
		Builder.GetFailures() > 0 ? TEXT(" (with errors, see above)") : TEXT(""));
}

void UIoTwinElectricalSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bTopologyDirty)
	{
		Rebuild();
	}
	if (Session == nullptr)
	{
		return;
	}
	ApplySnapshot();

	if (ShortCircuitAfter > 0.0 && FPlatformTime::Seconds() - StartTime >= ShortCircuitAfter)
	{
		ShortCircuitAfter = -1.0;
		// The port of a load, preferably the one of the generated demo (the lamp).
		const UElectricalDeviceComponent* Target = nullptr;
		for (const TWeakObjectPtr<UObject>& Object : Elements)
		{
			const UElectricalDeviceComponent* Load = Cast<UElectricalDeviceComponent>(Object.Get());
			if (Load && Load->InputPort && (Target == nullptr || Load->GetOwner()->ActorHasTag(AGM_Simulation::DemoTag)))
			{
				Target = Load;
			}
		}
		if (Target)
		{
			UE_LOG(LogIoTwin, Log, TEXT("Short circuit at %s (-IoTwinShortCircuitAfter)"), *GetPortDeviceId(Target->InputPort));
			ToggleShortCircuit(Target->InputPort);
		}
	}
}

void UIoTwinElectricalSubsystem::ApplySnapshot()
{
	const ElSnapshot* Snapshot = el_session_snapshot(Session);
	if (Snapshot->step > 0)
	{
		SimulatedTime = Snapshot->time_s;
		const int32 Count = FMath::Min(States.Num(), static_cast<int32>(Snapshot->device_count));
		for (int32 i = 0; i < Count; ++i)
		{
			const ElDeviceState& In = Snapshot->devices[i];
			FIoTwinElectricalState& Out = States[i];
			Out.Voltage = static_cast<float>(In.voltage_v);
			Out.Current = static_cast<float>(In.current_a);
			Out.Power = static_cast<float>(In.power_w);
			Out.ResidualCurrent = static_cast<float>(In.residual_a);
			Out.Level = static_cast<float>(In.level);
			Out.bOn = (In.flags & EL_STATE_ON) != 0;
			Out.bTripped = (In.flags & EL_STATE_TRIPPED) != 0;
			Out.bPowered = (In.flags & EL_STATE_POWERED) != 0;
		}
	}

	ElEvent Event;
	while (el_session_next_event(Session, &Event))
	{
		const FString Text = FString::Printf(TEXT("%.2f s  %s"), Event.time_s, UTF8_TO_TCHAR(Event.text));
		UE_LOG(LogIoTwin, Log, TEXT("Electrical: %s"), *Text);
		RecentEvents.Add(Text);
		if (RecentEvents.Num() > 5)
		{
			RecentEvents.RemoveAt(0);
		}
		OnElectricalEvent.Broadcast(Text);
	}

	for (const TPair<TWeakObjectPtr<const UElectricalPortComponent>, FString>& Port : PortIds)
	{
		if (UElectricalPortComponent* Component = const_cast<UElectricalPortComponent*>(Port.Key.Get()))
		{
			Component->SetSimulatedVoltage(GetPortVoltage(Component));
		}
	}
	for (const TWeakObjectPtr<UObject>& Object : Elements)
	{
		if (IElectricalElement* Element = Cast<IElectricalElement>(Object.Get()))
		{
			Element->ApplyElectrical(*this);
		}
	}
}

bool UIoTwinElectricalSubsystem::SendCommand(const ElCommand& Command)
{
	if (Session == nullptr)
	{
		return false;
	}
	if (!el_session_send(Session, &Command))
	{
		UE_LOG(LogIoTwin, Warning, TEXT("Electrical simulation: command queue full"));
		return false;
	}
	return true;
}

bool UIoTwinElectricalSubsystem::GetDeviceState(const FString& DeviceId, FIoTwinElectricalState& OutState) const
{
	const int32* Index = DeviceIds.Find(DeviceId);
	if (Index == nullptr || !States.IsValidIndex(*Index) || SimulatedTime <= 0.0)
	{
		return false;
	}
	OutState = States[*Index];
	return true;
}

FString UIoTwinElectricalSubsystem::GetPortDeviceId(const UElectricalPortComponent* Port) const
{
	const FString* Id = PortIds.Find(Port);
	return Id ? *Id : FString();
}

float UIoTwinElectricalSubsystem::GetPortVoltage(const UElectricalPortComponent* Port) const
{
	FIoTwinElectricalState State;
	return GetDeviceState(GetPortDeviceId(Port), State) ? State.Voltage : 0.0f;
}

float UIoTwinElectricalSubsystem::GetLastStepMicroseconds() const
{
	return Runner ? static_cast<float>(Runner->LastStepNanoseconds.load()) / 1000.0f : 0.0f;
}

bool UIoTwinElectricalSubsystem::SetDeviceOn(const FString& DeviceId, bool bOn)
{
	return SendCommand(el_command(EL_CMD_SET_ON, TCHAR_TO_UTF8(*DeviceId), nullptr, bOn ? 1.0 : 0.0));
}

bool UIoTwinElectricalSubsystem::SetDimmerLevel(const FString& DeviceId, float Level)
{
	return SendCommand(el_command(EL_CMD_SET_LEVEL, TCHAR_TO_UTF8(*DeviceId), nullptr, FMath::Clamp(Level, 0.0f, 1.0f)));
}

bool UIoTwinElectricalSubsystem::AddFault(const FString& TerminalA, const FString& TerminalB, float Ohm)
{
	const FTCHARToUTF8 A(*TerminalA);
	const FTCHARToUTF8 B(*TerminalB);
	return SendCommand(el_command(EL_CMD_ADD_FAULT, A.Get(), B.Get(), FMath::Max(0.0f, Ohm)));
}

bool UIoTwinElectricalSubsystem::ClearFaults()
{
	bShortCircuit = false;
	return SendCommand(el_command(EL_CMD_CLEAR_FAULTS, nullptr, nullptr, 0.0));
}

bool UIoTwinElectricalSubsystem::ToggleShortCircuit(UElectricalPortComponent* Port)
{
	if (bShortCircuit)
	{
		return ClearFaults();
	}
	const FString Id = GetPortDeviceId(Port);
	if (Id.IsEmpty() || !AddFault(Id + TEXT(":L"), Id + TEXT(":N"), 0.0f))
	{
		return false;
	}
	bShortCircuit = true;
	return true;
}

int32 UIoTwinElectricalSubsystem::ResetProtection()
{
	int32 Count = 0;
	for (const TPair<FString, int32>& Device : DeviceIds)
	{
		if (States.IsValidIndex(Device.Value) && States[Device.Value].bTripped &&
			SendCommand(el_command(EL_CMD_RESET, TCHAR_TO_UTF8(*Device.Key), nullptr, 0.0)))
		{
			++Count;
		}
	}
	return Count;
}
