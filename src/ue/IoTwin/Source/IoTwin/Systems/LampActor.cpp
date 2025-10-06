// Fill out your copyright notice in the Description page of Project Settings.


#include "LampActor.h"


// Sets default values
ALampActor::ALampActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Port = CreateDefaultSubobject<UElectricalPortComponent>(TEXT("ElectricalPort"));
	Port->SetupAttachment(Root);

	Device = CreateDefaultSubobject<UElectricalDeviceComponent>(TEXT("ElectricalDevice"));
	Device->SetComponentTickEnabled(true);

	Light = CreateDefaultSubobject<ULightEmitterComponent>(TEXT("LightEmitter"));
	Light->SetupAttachment(Root);
	Light->SetComponentTickEnabled(true);
}

// Called when the game starts or when spawned
void ALampActor::OnConstruction(const FTransform& Transform)

{
	Super::OnConstruction(Transform);
	
	if (Device && Port)
	{
		if (Device->InputPort == nullptr)
		{
			Device->InputPort = Port;
		}
	}

	if (Light && Device)
	{
		if (Light->Device == nullptr)
		{
			Light->Device = Device;
		}
	}
	
}
