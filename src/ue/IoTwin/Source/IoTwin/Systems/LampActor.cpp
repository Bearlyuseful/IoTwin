// IoTwin - lamp: port -> device -> light, with a visible bulb.

#include "LampActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ALampActor::ALampActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Port = CreateDefaultSubobject<UElectricalPortComponent>(TEXT("ElectricalPort"));
	Port->SetupAttachment(Root);

	Device = CreateDefaultSubobject<UElectricalDeviceComponent>(TEXT("ElectricalDevice"));
	Device->SetComponentTickEnabled(true);

	Light = CreateDefaultSubobject<ULightEmitterComponent>(TEXT("LightEmitter"));
	Light->SetupAttachment(Root);
	// Just below the bulb, so the bulb itself is lit and appears to glow.
	Light->SetRelativeLocation(FVector(0.0f, 0.0f, -16.0f));
	Light->SetComponentTickEnabled(true);

	Bulb = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bulb"));
	Bulb->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ColorMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (SphereMesh.Succeeded())
	{
		Bulb->SetStaticMesh(SphereMesh.Object);
	}
	BulbBaseMaterial = ColorMaterial.Object;
	Bulb->SetRelativeScale3D(FVector(0.25f));
	// The light sits inside the bulb: the bulb must not block it.
	Bulb->SetCastShadow(false);
	Bulb->SetCollisionProfileName(TEXT("NoCollision"));
}

void ALampActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (Device && Port && Device->InputPort == nullptr)
	{
		Device->InputPort = Port;
	}
	if (Light && Device && Light->Device == nullptr)
	{
		Light->Device = Device;
	}
}

void ALampActor::BeginPlay()
{
	Super::BeginPlay();

	if (Bulb && Bulb->GetStaticMesh() && BulbBaseMaterial)
	{
		BulbMaterial = UMaterialInstanceDynamic::Create(BulbBaseMaterial, this);
		Bulb->SetMaterial(0, BulbMaterial);
	}
	if (Device)
	{
		Device->OnPoweredChanged.AddDynamic(this, &ALampActor::HandlePoweredChanged);
		HandlePoweredChanged(Device->bIsPowered);
	}
}

void ALampActor::HandlePoweredChanged(bool bPowered)
{
	if (BulbMaterial)
	{
		BulbMaterial->SetVectorParameterValue(TEXT("Color"), bPowered ? BulbOnColor : BulbOffColor);
	}
}
