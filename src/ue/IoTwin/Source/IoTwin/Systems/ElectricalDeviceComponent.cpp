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
	if (!bInitialised || bPoweredNow != bIsPowered)
	{
		bInitialised = true;
		bIsPowered = bPoweredNow;
		OnPoweredChanged.Broadcast(bIsPowered);
	}
}
