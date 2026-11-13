// IoTwin - consumer that is powered through its input port.

#include "ElectricalDeviceComponent.h"

UElectricalDeviceComponent::UElectricalDeviceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UElectricalDeviceComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                               FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const bool bPoweredNow = InputPort != nullptr && IPowerInterface::Execute_HasPower(InputPort);
	const float LevelNow = bPoweredNow ? InputPort->GetSupplyLevel() : 0.0f;
	const bool bFirst = !bInitialised;
	bInitialised = true;
	if (bFirst || bPoweredNow != bIsPowered)
	{
		bIsPowered = bPoweredNow;
		OnPoweredChanged.Broadcast(bIsPowered);
	}
	if (bFirst || !FMath::IsNearlyEqual(LevelNow, PowerLevel))
	{
		PowerLevel = LevelNow;
		OnPowerLevelChanged.Broadcast(PowerLevel);
	}
}
