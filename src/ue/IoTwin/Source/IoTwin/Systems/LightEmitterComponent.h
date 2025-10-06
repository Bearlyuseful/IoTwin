// Fill out your copyright notice in the Description page of Project Settings.

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

	UPROPERTY(EditAnywhere)
	UElectricalDeviceComponent* Device;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
};
