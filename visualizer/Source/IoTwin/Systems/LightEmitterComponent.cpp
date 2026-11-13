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

	const float Level = (Device != nullptr && Device->bIsPowered) ? Device->PowerLevel : 0.0f;
	if (!bApplied || !FMath::IsNearlyEqual(Level, LastLevel))
	{
		Apply(Level);
	}
}

void ULightEmitterComponent::Apply(float Level)
{
	// Only touch the render state on changes. Brightness follows the dimming level.
	bApplied = true;
	LastLevel = Level;
	SetVisibility(Level > 0.0f);
	SetIntensity(PoweredIntensity * Level);
}
