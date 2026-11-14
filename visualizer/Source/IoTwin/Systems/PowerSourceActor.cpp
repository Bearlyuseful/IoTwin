// IoTwin - power supply with an output relay, optionally controlled via CAN.

#include "PowerSourceActor.h"

#include "Bus/CanSignalComponent.h"
#include "Bus/IoTwinBusSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Electrical/ElectricalModelBuilder.h"
#include "Electrical/IoTwinElectricalSubsystem.h"
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
		// Dimming: the brightness travels in the same frame as the relay bit.
		if (LevelByteIndex >= 0 && FCanFrame::ParseId(RelayControl->CanId, RelayControl->bExtendedId, RelayFrameId))
		{
			if (UIoTwinBusSubsystem* Bus = UIoTwinBusSubsystem::Get(this))
			{
				Bus->OnFrameReceived.AddDynamic(this, &APowerSourceActor::HandleBusFrame);
				FCanFrame Last;
				if (Bus->GetLastFrame(RelayFrameId, RelayControl->bExtendedId, Last))
				{
					HandleBusFrame(Last);
				}
			}
		}
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
	if (UIoTwinElectricalSubsystem* Simulation = UIoTwinElectricalSubsystem::Get(this); Simulation && !RelayId.IsEmpty())
	{
		Simulation->SetDeviceOn(RelayId, bEnabled);
	}
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

void APowerSourceActor::HandleBusFrame(const FCanFrame& Frame)
{
	if (Frame.Id != RelayFrameId || Frame.bExtended != RelayControl->bExtendedId || Frame.bRemote)
	{
		return;
	}
	// Frames without the brightness byte (older senders) mean full brightness.
	const float Level = Frame.Data.IsValidIndex(LevelByteIndex) ? FMath::Clamp(Frame.Data[LevelByteIndex], 0, 100) / 100.0f : 1.0f;
	if (Level != Port->SupplyLevel)
	{
		Port->SupplyLevel = Level;
		if (UIoTwinElectricalSubsystem* Simulation = UIoTwinElectricalSubsystem::Get(this); Simulation && !RelayId.IsEmpty())
		{
			Simulation->SetDimmerLevel(RelayId, Level);
		}
	}
}

void APowerSourceActor::DescribeElectrical(FElectricalModelBuilder& Builder)
{
	RelayId.Reset();
	BreakerId.Reset();
	const FString PortId = Builder.PortId(Port);
	if (PortId.IsEmpty())
	{
		return;
	}
	const FString SupplyId = Builder.MakeId(this, TEXT("supply"));
	const FString F1 = Builder.MakeId(this, TEXT("F1"));
	const FString Relay = Builder.MakeId(this, TEXT("relay"));
	const FElectricalDeviceRef Supply = Builder.AddDevice(SupplyId, EL_SUPPLY, TEXT("House connection"));
	const FElectricalDeviceRef Mcb = Builder.AddDevice(F1, EL_BREAKER, FString::Printf(TEXT("F1 B%.0f"), BreakerRatedCurrent));
	const FElectricalDeviceRef Output = Builder.AddDevice(Relay, EL_DIMMER, TEXT("Relay output"));
	if (!Supply || !Mcb || !Output)
	{
		return;
	}
	Supply->loop_impedance_ohm = LoopImpedance;
	Mcb->rated_current_a = BreakerRatedCurrent;
	Mcb->magnetic_multiple = 5.0; // characteristic B
	Output->on = bOutputEnabled;
	Output->level = FMath::Clamp(Port->SupplyLevel, 0.0f, 1.0f);
	bool bOk = Builder.Connect(SupplyId + TEXT(":L1"), F1 + TEXT(":in"), TEXT("L"));
	bOk &= Builder.Connect(F1 + TEXT(":out"), Relay + TEXT(":in"), TEXT("L"));
	bOk &= Builder.Connect(Relay + TEXT(":out"), PortId + TEXT(":L"), TEXT("L"));
	bOk &= Builder.Connect(SupplyId + TEXT(":N"), PortId + TEXT(":N"), TEXT("N"));
	bOk &= Builder.Connect(SupplyId + TEXT(":PE"), PortId + TEXT(":PE"), TEXT("PE"));
	if (bOk)
	{
		RelayId = Relay;
		BreakerId = F1;
	}
}

void APowerSourceActor::ApplyElectrical(const UIoTwinElectricalSubsystem& Simulation)
{
	const bool bWasTripped = Breaker.bTripped;
	if (BreakerId.IsEmpty() || !Simulation.GetDeviceState(BreakerId, Breaker))
	{
		Breaker = FIoTwinElectricalState();
	}
	if (Breaker.bTripped != bWasTripped)
	{
		UpdateHousing();
	}
}

void APowerSourceActor::UpdateHousing()
{
	if (HousingMaterial)
	{
		// Green: relay closed, dark red: open, amber: breaker F1 tripped.
		const FLinearColor Color = Breaker.bTripped ? FLinearColor(0.9f, 0.45f, 0.02f)
			: bOutputEnabled ? FLinearColor(0.1f, 0.6f, 0.15f) : FLinearColor(0.45f, 0.08f, 0.06f);
		HousingMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
}
