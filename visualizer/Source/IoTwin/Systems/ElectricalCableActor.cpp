// IoTwin - cable: two ports that conduct to each other, drawn as a hanging cable.

#include "ElectricalCableActor.h"

#include "CableComponent.h"
#include "Electrical/ElectricalModelBuilder.h"

AElectricalCableActor::AElectricalCableActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	EndA = CreateDefaultSubobject<UElectricalPortComponent>(TEXT("EndA"));
	EndA->SetupAttachment(Root);

	EndB = CreateDefaultSubobject<UElectricalPortComponent>(TEXT("EndB"));
	EndB->SetupAttachment(Root);

	CableMesh = CreateDefaultSubobject<UCableComponent>(TEXT("CableMesh"));
	CableMesh->SetupAttachment(EndA);
	CableMesh->bAttachStart = true;
	CableMesh->bAttachEnd = true;
	CableMesh->CableWidth = 2.5f;
	CableMesh->NumSegments = 16;
	CableMesh->NumSides = 6;
	CableMesh->SetCastShadow(false);
}

void AElectricalCableActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Both ends of a cable conduct to each other.
	if (EndA && EndB)
	{
		IPowerInterface::Execute_ConnectTo(EndB, EndA);
	}
	RefreshCable();
}

void AElectricalCableActor::ConnectBetween(UElectricalPortComponent* A, UElectricalPortComponent* B)
{
	if (A == nullptr || B == nullptr || EndA == nullptr || EndB == nullptr)
	{
		return;
	}
	EndA->SetWorldLocation(A->GetComponentLocation());
	EndB->SetWorldLocation(B->GetComponentLocation());
	IPowerInterface::Execute_ConnectTo(EndA, A);
	IPowerInterface::Execute_ConnectTo(EndB, B);
	IPowerInterface::Execute_ConnectTo(EndB, EndA);
	RefreshCable();
}

float AElectricalCableActor::GetLengthMeters() const
{
	if (EndA == nullptr || EndB == nullptr)
	{
		return 0.0f;
	}
	// Unreal units are centimetres.
	return FMath::Max(0.1f, FVector::Distance(EndA->GetComponentLocation(), EndB->GetComponentLocation()) * Slack / 100.0f);
}

void AElectricalCableActor::DescribeElectrical(FElectricalModelBuilder& Builder)
{
	const FString A = Builder.PortId(EndA);
	const FString B = Builder.PortId(EndB);
	if (A.IsEmpty() || B.IsEmpty())
	{
		return;
	}
	const FString Cable = Builder.AddCable(Builder.MakeId(this, TEXT("cable")), CrossSection, GetLengthMeters());
	if (Cable.IsEmpty())
	{
		return;
	}
	for (const TCHAR* Terminal : {TEXT("L"), TEXT("N"), TEXT("PE")})
	{
		Builder.Connect(A + TEXT(":") + Terminal, B + TEXT(":") + Terminal, Terminal, Cable);
	}
	Builder.ClaimWire(EndA, EndB);
}

void AElectricalCableActor::RefreshCable()
{
	if (CableMesh == nullptr || EndA == nullptr || EndB == nullptr)
	{
		return;
	}
	CableMesh->SetAttachEndToComponent(EndB);
	CableMesh->EndLocation = FVector::ZeroVector;
	const float Distance = FVector::Distance(EndA->GetComponentLocation(), EndB->GetComponentLocation());
	CableMesh->CableLength = FMath::Max(1.0f, Distance * Slack);
}
