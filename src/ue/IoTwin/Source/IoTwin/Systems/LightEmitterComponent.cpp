// IoTwin - point light that shines while its device is powered.

#include "LightEmitterComponent.h"

ULightEmitterComponent::ULightEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULightEmitterComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const bool bOn = Device != nullptr && Device->bIsPowered;
	if (!bApplied || bOn != bLastOn)
	{
		Apply(bOn);
	}
}

void ULightEmitterComponent::Apply(bool bOn)
{
	// Only touch the render state on changes.
	bApplied = true;
	bLastOn = bOn;
	SetVisibility(bOn);
	SetIntensity(bOn ? PoweredIntensity : 0.0f);
}
