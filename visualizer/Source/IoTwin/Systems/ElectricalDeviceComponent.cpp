// IoTwin - consumer (load) that is supplied through its input port.

#include "ElectricalDeviceComponent.h"

#include "Electrical/ElectricalModelBuilder.h"
#include "Electrical/IoTwinElectricalSubsystem.h"
#include "GameFramework/Actor.h"

UElectricalDeviceComponent::UElectricalDeviceComponent()
{
	// The simulation pushes its results (ApplyElectrical); nothing to poll.
	PrimaryComponentTick.bCanEverTick = false;
}

void UElectricalDeviceComponent::DescribeElectrical(FElectricalModelBuilder& Builder)
{
	SimulationId.Reset();
	if (InputPort == nullptr)
	{
		return;
	}
	const FString Id = Builder.MakeId(this);
	const FElectricalDeviceRef Load = Builder.AddDevice(Id, EL_LOAD, GetOwner() ? GetOwner()->GetName() : FString());
	if (!Load)
	{
		return;
	}
	Load->rated_power_w = RatedPower;
	Load->voltage_v = RatedVoltage;
	if (Builder.ConnectToPort(Id, InputPort))
	{
		SimulationId = Id;
	}
}

void UElectricalDeviceComponent::ApplyElectrical(const UIoTwinElectricalSubsystem& Simulation)
{
	FIoTwinElectricalState State;
	const bool bKnown = !SimulationId.IsEmpty() && Simulation.GetDeviceState(SimulationId, State);
	const bool bPoweredNow = bKnown && State.bPowered;
	const float LevelNow = bPoweredNow && RatedPower > 0.0f ? FMath::Clamp(State.Power / RatedPower, 0.0f, 1.0f) : 0.0f;
	Voltage = bKnown ? State.Voltage : 0.0f;
	Current = bKnown ? State.Current : 0.0f;
	Power = bKnown ? State.Power : 0.0f;

	const bool bFirst = !bInitialised;
	bInitialised = true;
	if (bFirst || bPoweredNow != bIsPowered)
	{
		bIsPowered = bPoweredNow;
		OnPoweredChanged.Broadcast(bIsPowered);
	}
	// Ignore the tiny changes of every step; brightness steps of 0.5 % are invisible.
	if (bFirst || FMath::Abs(LevelNow - PowerLevel) > 0.005f || (LevelNow == 0.0f) != (PowerLevel == 0.0f))
	{
		PowerLevel = LevelNow;
		OnPowerLevelChanged.Broadcast(PowerLevel);
	}
}
