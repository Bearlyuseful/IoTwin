// IoTwin - game-wide access to the virtual CAN bus.

#include "Bus/IoTwinBusSubsystem.h"

#include "Bus/IoTwinBusConnection.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "IoTwin.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

void UIoTwinBusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FString HostOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("IoTwinBusHost="), HostOverride))
	{
		Host = HostOverride;
	}
	int32 PortOverride = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("IoTwinBusPort="), PortOverride))
	{
		Port = PortOverride;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("IoTwinBusDisabled")))
	{
		bEnabled = false;
	}

	if (!bEnabled)
	{
		UE_LOG(LogIoTwinBus, Log, TEXT("Bus connection disabled"));
		return;
	}

	UE_LOG(LogIoTwinBus, Log, TEXT("Connecting to iotwin-bus at %s"), *GetEndpoint());
	Connection = MakeUnique<FIoTwinBusConnection>(Host, Port);
	Connection->Start();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UIoTwinBusSubsystem::Tick));
}

void UIoTwinBusSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	Connection.Reset(); // joins the worker thread
	Super::Deinitialize();
}

UIoTwinBusSubsystem* UIoTwinBusSubsystem::Get(const UObject* WorldContextObject)
{
	if (GEngine == nullptr || WorldContextObject == nullptr)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UIoTwinBusSubsystem>() : nullptr;
}

bool UIoTwinBusSubsystem::SendFrame(const FCanFrame& Frame)
{
	const FString Text = Frame.ToString();
	if (Text.IsEmpty())
	{
		UE_LOG(LogIoTwinBus, Warning, TEXT("Not sending invalid CAN frame (id %d)"), Frame.Id);
		return false;
	}
	if (!Connection.IsValid() || !Connection->EnqueueLine(Text))
	{
		UE_LOG(LogIoTwinBus, Verbose, TEXT("Bus offline, frame %s dropped"), *Text);
		return false;
	}
	AddTraffic(TEXT("TX"), Text);
	OnFrameSent.Broadcast(Frame);
	UE_LOG(LogIoTwinBus, Verbose, TEXT("TX %s"), *Text);
	return true;
}

bool UIoTwinBusSubsystem::IsConnected() const
{
	return Connection.IsValid() && Connection->IsConnected();
}

FString UIoTwinBusSubsystem::GetEndpoint() const
{
	return FString::Printf(TEXT("%s:%d"), *Host, Port);
}

bool UIoTwinBusSubsystem::GetLastFrame(int32 Id, bool bExtended, FCanFrame& OutFrame) const
{
	if (const FCanFrame* Frame = LastFrames.Find(MakeKey(Id, bExtended)))
	{
		OutFrame = *Frame;
		return true;
	}
	return false;
}

bool UIoTwinBusSubsystem::Tick(float DeltaSeconds)
{
	if (!Connection.IsValid())
	{
		return true;
	}

	const bool bConnectedNow = Connection->IsConnected();
	if (bConnectedNow != bWasConnected)
	{
		bWasConnected = bConnectedNow;
		AddTraffic(TEXT("--"), bConnectedNow ? TEXT("connected") : TEXT("disconnected"));
		OnConnectionChanged.Broadcast(bConnectedNow);
		BusStatus = FIoTwinBusStatus();
		UnansweredStatus = 0;
		bStatusSupported = true;
		if (bConnectedNow && !NodeName.IsEmpty())
		{
			Connection->EnqueueLine(TEXT("!name ") + NodeName);
		}
	}

	// Poll counters and injected faults; an older bus without "!status" never answers.
	const double Now = FPlatformTime::Seconds();
	if (bConnectedNow && bStatusSupported && Now >= NextStatusQuery)
	{
		NextStatusQuery = Now + StatusIntervalSeconds;
		if (++UnansweredStatus > MaxUnansweredStatus)
		{
			bStatusSupported = false;
			UE_LOG(LogIoTwinBus, Log, TEXT("iotwin-bus does not answer !status (older version), fault display off"));
		}
		else
		{
			Connection->EnqueueLine(TEXT("!status"));
		}
	}

	FString Line;
	for (int32 Count = 0; Count < MaxFramesPerTick && Connection->DequeueLine(Line); ++Count)
	{
		if (Line.StartsWith(TEXT("!")) || Line.StartsWith(TEXT("@")))
		{
			HandleControlLine(Line);
			continue;
		}
		FCanFrame Frame;
		if (!FCanFrame::Parse(Line, Frame))
		{
			UE_LOG(LogIoTwinBus, Warning, TEXT("Ignoring invalid line from bus: '%s'"), *Line.Left(40));
			continue;
		}
		LastFrames.Add(MakeKey(Frame.Id, Frame.bExtended), Frame);
		AddTraffic(TEXT("RX"), Line);
		UE_LOG(LogIoTwinBus, Verbose, TEXT("RX %s"), *Line);
		OnFrameReceived.Broadcast(Frame);
	}
	return true;
}

void UIoTwinBusSubsystem::HandleControlLine(const FString& Line)
{
	FIoTwinBusStatus Status;
	if (FIoTwinBusStatus::Parse(Line, Status))
	{
		BusStatus = Status;
		LastStatusTime = FPlatformTime::Seconds();
		UnansweredStatus = 0;
	}
	else if (Line.StartsWith(TEXT("!error")))
	{
		UE_LOG(LogIoTwinBus, Log, TEXT("iotwin-bus: %s"), *Line);
	}
}

FIoTwinBusStatus UIoTwinBusSubsystem::GetBusStatus() const
{
	FIoTwinBusStatus Status = BusStatus;
	Status.bValid = Status.bValid && IsConnected() && FPlatformTime::Seconds() - LastStatusTime < StatusStaleSeconds;
	return Status;
}

void UIoTwinBusSubsystem::AddTraffic(const TCHAR* Direction, const FString& Line)
{
	// Collapse repetitions of cyclic messages so the list stays readable.
	const FString Entry = FString::Printf(TEXT("%s %s"), Direction, *Line);
	const FString RepeatPrefix = Entry + TEXT("   x");
	if (RecentTraffic.Num() > 0)
	{
		FString& Last = RecentTraffic.Last();
		if (Last == Entry)
		{
			Last = RepeatPrefix + TEXT("2");
			return;
		}
		if (Last.StartsWith(RepeatPrefix, ESearchCase::CaseSensitive))
		{
			const int32 Repeats = FCString::Atoi(*Last.RightChop(RepeatPrefix.Len()));
			Last = RepeatPrefix + FString::FromInt(Repeats + 1);
			return;
		}
	}
	RecentTraffic.Add(Entry);
	if (RecentTraffic.Num() > MaxRecentTraffic)
	{
		RecentTraffic.RemoveAt(0);
	}
}
