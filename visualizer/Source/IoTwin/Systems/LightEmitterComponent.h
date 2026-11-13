// IoTwin - point light that shines while its device is powered.

#pragma once

#include "CoreMinimal.h"
#include "ElectricalDeviceComponent.h"
#include "Components/PointLightComponent.h"
#include "LightEmitterComponent.generated.h"

UCLASS(ClassGroup=(Light), meta=(BlueprintSpawnableComponent))
class IOTWIN_API ULightEmitterComponent : public UPointLightComponent
{
	GENERATED_BODY()

public:
	ULightEmitterComponent();

	UPROPERTY(EditAnywhere, Category="Light")
	UElectricalDeviceComponent* Device;

	/** Intensity while the device is powered. */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0.0))
	float PoweredIntensity = 30000.0f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Apply(bool bOn);

	bool bApplied = false;
	bool bLastOn = false;
};
