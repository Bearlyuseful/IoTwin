// Fill out your copyright notice in the Description page of Project Settings.


#include "PowerSourceActor.h"


// Sets default values
APowerSourceActor::APowerSourceActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Port = CreateDefaultSubobject<UElectricalPortComponent>(TEXT("PowerPort"));
	
	Port->SetupAttachment(RootComponent);
}

// Called when the game starts or when spawned
void APowerSourceActor::BeginPlay()
{
	Super::BeginPlay();
	Port->setPowerSource(true);
	
}

