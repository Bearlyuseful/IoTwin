// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ElectricalDeviceComponent.h"
#include "ElectricalPortComponent.h"
#include "LightEmitterComponent.h"
#include "GameFramework/Actor.h"
#include "LampActor.generated.h"

UCLASS()
class IOTWIN_API ALampActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ALampActor();

protected:
	// Called when the game starts or when spawned
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	UElectricalPortComponent* Port;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	UElectricalDeviceComponent* Device;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	ULightEmitterComponent* Light;
	
};
