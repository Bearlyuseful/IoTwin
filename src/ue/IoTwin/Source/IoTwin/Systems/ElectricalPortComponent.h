// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PowerInterface.h"
#include "Components/SceneComponent.h"
#include "ElectricalPortComponent.generated.h"


UCLASS(ClassGroup=(Electric), meta=(BlueprintSpawnableComponent))
class IOTWIN_API UElectricalPortComponent : public USceneComponent, public IPowerInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UElectricalPortComponent();


	virtual bool HasPower_Implementation() const override;
	virtual void ConnectTo_Implementation(const TScriptInterface<IPowerInterface>& other) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Connection", meta=(ExposeOnSpawn=true))
	TObjectPtr<UElectricalPortComponent> ConnectedPort;

	void setPowerSource(bool bPowered);

private:
	bool bHasPower = false;
	TScriptInterface<IPowerInterface> ConnectedDeviceNext;
	TScriptInterface<IPowerInterface> ConnectedDevicePrevious;
	


};
