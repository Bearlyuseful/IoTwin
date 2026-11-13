// IoTwin - cable: two ports that conduct to each other, drawn as a hanging cable.

#include "ElectricalCableActor.h"

#include "CableComponent.h"

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
