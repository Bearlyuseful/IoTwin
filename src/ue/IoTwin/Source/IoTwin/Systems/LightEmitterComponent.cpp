// Fill out your copyright notice in the Description page of Project Settings.


#include "LightEmitterComponent.h"


// Sets default values for this component's properties
ULightEmitterComponent::ULightEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}


// Called every frame
void ULightEmitterComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if(Device)
	{
		SetVisibility(Device->bIsPowered);
		SetIntensity(Device->bIsPowered ? 30000.0f : 0.0f);
	}
	else
	{
		SetVisibility(false);
		SetIntensity(0.0f);
	}
	
}

