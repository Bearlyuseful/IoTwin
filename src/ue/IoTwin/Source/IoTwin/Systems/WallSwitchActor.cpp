// IoTwin - clickable wall switch that reports its position on the CAN bus.

#include "WallSwitchActor.h"

#include "Bus/CanSignalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "IoTwin.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AWallSwitchActor::AWallSwitchActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	// Front side is -X: the switch is meant to sit on a wall facing -X.
	Plate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
	Plate->SetupAttachment(Root);
	Plate->SetRelativeScale3D(FVector(0.04f, 0.30f, 0.40f));

	Rocker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rocker"));
	Rocker->SetupAttachment(Root);
	Rocker->SetRelativeLocation(FVector(-4.0f, 0.0f, 0.0f));
	Rocker->SetRelativeScale3D(FVector(0.05f, 0.14f, 0.22f));

	if (CubeMesh.Succeeded())
	{
		Plate->SetStaticMesh(CubeMesh.Object);
		Rocker->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ColorMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = ColorMaterial.Object;

	Signal = CreateDefaultSubobject<UCanSignalComponent>(TEXT("Signal"));
	Signal->Configure(ECanSignalDirection::Transmit, 0x200);
}

void AWallSwitchActor::BeginPlay()
{
	Super::BeginPlay();

	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* PlateMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		PlateMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.8f, 0.8f, 0.78f));
		Plate->SetMaterial(0, PlateMaterial);
		RockerMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Rocker->SetMaterial(0, RockerMaterial);
	}
	Plate->OnClicked.AddDynamic(this, &AWallSwitchActor::HandleClicked);
	Rocker->OnClicked.AddDynamic(this, &AWallSwitchActor::HandleClicked);

	// The position of a physical switch is always known: publish it, so the
	// ECU learns it as soon as the bus is connected (see UCanSignalComponent).
	if (Signal)
	{
		Signal->SetValue(bOn);
	}
	UpdateVisuals();
}

void AWallSwitchActor::HandleClicked(UPrimitiveComponent* ClickedComponent, FKey ButtonPressed)
{
	Toggle();
}

void AWallSwitchActor::Toggle()
{
	SetSwitchOn(!bOn);
}

void AWallSwitchActor::SetSwitchOn(bool bNewOn)
{
	bOn = bNewOn;
	UE_LOG(LogIoTwin, Log, TEXT("%s switched %s"), *GetName(), bOn ? TEXT("ON") : TEXT("OFF"));
	if (Signal)
	{
		Signal->SetValue(bOn);
	}
	UpdateVisuals();
}

void AWallSwitchActor::UpdateVisuals()
{
	// Tilt the rocker like a real switch and colour it by state.
	Rocker->SetRelativeRotation(FRotator(bOn ? -12.0f : 12.0f, 0.0f, 0.0f));
	if (RockerMaterial)
	{
		RockerMaterial->SetVectorParameterValue(TEXT("Color"),
			bOn ? FLinearColor(0.15f, 0.75f, 0.2f) : FLinearColor(0.15f, 0.15f, 0.15f));
	}
}
