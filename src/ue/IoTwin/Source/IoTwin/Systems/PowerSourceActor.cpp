// IoTwin - power supply with an output relay, optionally controlled via CAN.

#include "PowerSourceActor.h"

#include "Bus/CanSignalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "IoTwin.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

APowerSourceActor::APowerSourceActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Port = CreateDefaultSubobject<UElectricalPortComponent>(TEXT("PowerPort"));
	Port->SetupAttachment(RootComponent);

	Housing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Housing"));
	Housing->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ColorMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeMesh.Succeeded())
	{
		Housing->SetStaticMesh(CubeMesh.Object);
	}
	HousingBaseMaterial = ColorMaterial.Object;
	Housing->SetRelativeScale3D(FVector(0.3f, 0.4f, 0.5f));

	RelayControl = CreateDefaultSubobject<UCanSignalComponent>(TEXT("RelayControl"));
	RelayControl->Configure(ECanSignalDirection::Receive, 0x100);
}

void APowerSourceActor::BeginPlay()
{
	Super::BeginPlay();

	if (Housing && Housing->GetStaticMesh() && HousingBaseMaterial)
	{
		HousingMaterial = UMaterialInstanceDynamic::Create(HousingBaseMaterial, this);
		Housing->SetMaterial(0, HousingMaterial);
	}

	if (bControlledByCan && RelayControl)
	{
		RelayControl->OnValueChanged.AddDynamic(this, &APowerSourceActor::HandleRelaySignal);
		SetOutputEnabled(RelayControl->HasValue() && RelayControl->GetValue());
		UE_LOG(LogIoTwin, Log, TEXT("%s: relay controlled by CAN %s"), *GetName(), *RelayControl->Describe());
	}
	else
	{
		SetOutputEnabled(bOutputEnabled);
	}
}

void APowerSourceActor::SetOutputEnabled(bool bEnabled)
{
	const bool bChanged = bEnabled != Port->IsSupplying();
	bOutputEnabled = bEnabled;
	Port->SetSupplying(bEnabled);
	UpdateHousing();
	if (bChanged)
	{
		UE_LOG(LogIoTwin, Log, TEXT("%s: relay %s"), *GetName(), bEnabled ? TEXT("closed") : TEXT("open"));
		OnRelayChanged.Broadcast(bEnabled);
	}
}

void APowerSourceActor::HandleRelaySignal(bool bClosed)
{
	SetOutputEnabled(bClosed);
}

void APowerSourceActor::UpdateHousing()
{
	if (HousingMaterial)
	{
		HousingMaterial->SetVectorParameterValue(TEXT("Color"),
			bOutputEnabled ? FLinearColor(0.1f, 0.6f, 0.15f) : FLinearColor(0.45f, 0.08f, 0.06f));
	}
}
