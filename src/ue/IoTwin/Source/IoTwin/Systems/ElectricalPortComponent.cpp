// IoTwin - electrical connection point of a device.

#include "ElectricalPortComponent.h"

#include "IoTwin.h"

UElectricalPortComponent::UElectricalPortComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UElectricalPortComponent::OnRegister()
{
	Super::OnRegister();

	// Assets saved before wires were stored on both ports only know ConnectedPort.
	if (ConnectedPort && ConnectedPort != this && !Connections.Contains(ConnectedPort))
	{
		Connections.AddUnique(ConnectedPort);
		ConnectedPort->Connections.AddUnique(this);
	}
}

bool UElectricalPortComponent::HasPower_Implementation() const
{
	TArray<const UElectricalPortComponent*, TInlineAllocator<16>> Pending;
	TSet<const UElectricalPortComponent*, DefaultKeyFuncs<const UElectricalPortComponent*>, TInlineSetAllocator<16>> Visited;
	Pending.Add(this);
	Visited.Add(this);

	while (Pending.Num() > 0)
	{
		const UElectricalPortComponent* Port = Pending.Pop(EAllowShrinking::No);
		if (Port->bSupplying)
		{
			return true;
		}
		for (const TObjectPtr<UElectricalPortComponent>& Next : Port->Connections)
		{
			if (Next && !Visited.Contains(Next.Get()))
			{
				Visited.Add(Next.Get());
				Pending.Add(Next.Get());
			}
		}
	}
	return false;
}

void UElectricalPortComponent::ConnectTo_Implementation(const TScriptInterface<IPowerInterface>& other)
{
	UElectricalPortComponent* OtherPort = Cast<UElectricalPortComponent>(other.GetObject());
	if (OtherPort == nullptr)
	{
		UE_LOG(LogIoTwin, Warning, TEXT("%s: can only be connected to another electrical port"), *GetName());
		return;
	}
	if (OtherPort == this)
	{
		UE_LOG(LogIoTwin, Warning, TEXT("Cannot connect a port to itself."));
		return;
	}

	AddWire(OtherPort);
	UE_LOG(LogIoTwin, Verbose, TEXT("%s.%s connected to %s.%s"),
		*GetNameSafe(GetOwner()), *GetName(), *GetNameSafe(OtherPort->GetOwner()), *OtherPort->GetName());
}

void UElectricalPortComponent::AddWire(UElectricalPortComponent* Other)
{
	Modify();
	Other->Modify();
	Connections.AddUnique(Other);
	Other->Connections.AddUnique(this);
	ConnectedPort = Other;
	if (Other->ConnectedPort == nullptr)
	{
		Other->ConnectedPort = this;
	}
}

void UElectricalPortComponent::DisconnectFrom(UElectricalPortComponent* Other)
{
	if (Other == nullptr)
	{
		return;
	}
	Modify();
	Other->Modify();
	Connections.Remove(Other);
	Other->Connections.Remove(this);
	if (ConnectedPort == Other)
	{
		ConnectedPort = Connections.Num() > 0 ? Connections.Last() : nullptr;
	}
	if (Other->ConnectedPort == this)
	{
		Other->ConnectedPort = Other->Connections.Num() > 0 ? Other->Connections.Last() : nullptr;
	}
}

void UElectricalPortComponent::DisconnectAll()
{
	const TArray<TObjectPtr<UElectricalPortComponent>> Wired = Connections;
	for (const TObjectPtr<UElectricalPortComponent>& Other : Wired)
	{
		DisconnectFrom(Other.Get());
	}
}

TArray<UElectricalPortComponent*> UElectricalPortComponent::GetConnections() const
{
	TArray<UElectricalPortComponent*> Result;
	Result.Reserve(Connections.Num());
	for (const TObjectPtr<UElectricalPortComponent>& Port : Connections)
	{
		if (Port)
		{
			Result.Add(Port.Get());
		}
	}
	return Result;
}

void UElectricalPortComponent::SetSupplying(bool bSupply)
{
	bSupplying = bSupply;
}
