// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElectricalPortComponent.h"
#include "PowerSourceActor.generated.h"

UCLASS()
class IOTWIN_API APowerSourceActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	APowerSourceActor();

	UPROPERTY(VisibleAnywhere)
	UElectricalPortComponent* Port;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
};
