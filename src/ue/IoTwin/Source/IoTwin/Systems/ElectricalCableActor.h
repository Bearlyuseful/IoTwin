// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ElectricalPortComponent.h"
#include "GameFramework/Actor.h"
#include "ElectricalCableActor.generated.h"

UCLASS()
class IOTWIN_API AElectricalCableActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AElectricalCableActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	USceneComponent* Root;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable")
	UElectricalPortComponent* EndA;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable")
	UElectricalPortComponent* EndB;
	
protected:
	// Called when the game starts or when spawned
	virtual void OnConstruction(const FTransform& Transform) override;
	
};
