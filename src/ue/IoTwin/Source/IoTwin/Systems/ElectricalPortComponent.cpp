// Fill out your copyright notice in the Description page of Project Settings.


#include "ElectricalPortComponent.h"


// Sets default values for this component's properties
UElectricalPortComponent::UElectricalPortComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


bool UElectricalPortComponent::HasPower_Implementation() const
{
	if (bHasPower)
	{
		return true;
	}
	if(ConnectedDevicePrevious)
	{
		return IPowerInterface::Execute_HasPower(ConnectedDevicePrevious.GetObject());
	}
	return false;
}

void UElectricalPortComponent::ConnectTo_Implementation(const TScriptInterface<IPowerInterface>& other)
{
	if (other.GetObject() == this)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot connect a port to itself."));
		return;
	}
	
	//ConnectedDevicePrevious = other;
	//TODO unused is broken bidirectionally
	if (UElectricalPortComponent* OtherPort = Cast<UElectricalPortComponent>(other.GetObject()))
	{
		ConnectedPort = OtherPort;
	}
	
	// Optional: also connect the other side back to this
	if (other && other.GetObject()->GetClass()->ImplementsInterface(UPowerInterface::StaticClass()))
	{
		bool bAlreadyConnected = false;
		if(UElectricalPortComponent* OtherPort = Cast<UElectricalPortComponent>(other.GetObject()))
		{
			if(OtherPort->ConnectedDeviceNext.GetObject() == this)
			{
				ConnectedDevicePrevious = other;
			}
			else if(OtherPort->ConnectedDevicePrevious.GetObject() == this)
			{
				ConnectedDeviceNext = other;
			}
			else
			{
				ConnectedDevicePrevious = other;
			}
		}
		if(UElectricalPortComponent* OtherPort = Cast<UElectricalPortComponent>(other.GetObject()))
		{
			bAlreadyConnected = (OtherPort->ConnectedDeviceNext.GetObject() == this || OtherPort->ConnectedDevicePrevious.GetObject() == this);
		}
		if(!bAlreadyConnected)
		{
			IPowerInterface::Execute_ConnectTo(other.GetObject(), this);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("%s connected to %s"),
		*GetName(),
		other ? *other.GetObject()->GetName() : TEXT("None"));
}

void UElectricalPortComponent::setPowerSource(bool bPowered)
{
	bHasPower = bPowered;
}




