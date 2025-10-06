// Fill out your copyright notice in the Description page of Project Settings.


#include "ElectricalCableActor.h"


// Sets default values
AElectricalCableActor::AElectricalCableActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	
	EndA = CreateDefaultSubobject<UElectricalPortComponent>(TEXT("EndA"));
	EndA->SetupAttachment(Root);

	EndB = CreateDefaultSubobject<UElectricalPortComponent>(TEXT("EndB"));
	EndB->SetupAttachment(Root);

	
}

// Called when the game starts or when spawned
void AElectricalCableActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if(EndA && EndB)
	{
		if(EndB->GetClass()->ImplementsInterface(UPowerInterface::StaticClass()))
		{
			IPowerInterface::Execute_ConnectTo(EndB, EndA);
		}
	}
	
	
}

