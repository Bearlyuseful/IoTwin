// IoTwin - clickable wall switch that reports its position on the CAN bus.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WallSwitchActor.generated.h"

class UCanSignalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * Digital twin of a wall switch (a sensor input, not part of the power path).
 * Clicking it toggles the position and sends it via Signal (default 0x200 byte 0 bit 0).
 */
UCLASS()
class IOTWIN_API AWallSwitchActor : public AActor
{
	GENERATED_BODY()

public:
	AWallSwitchActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Switch")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Switch")
	UStaticMeshComponent* Plate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Switch")
	UStaticMeshComponent* Rocker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Switch")
	UCanSignalComponent* Signal;

	/** Material with a "Color" vector parameter used for plate and rocker. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Switch")
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UFUNCTION(BlueprintCallable, Category="Switch")
	void Toggle();

	UFUNCTION(BlueprintCallable, Category="Switch")
	void SetSwitchOn(bool bNewOn);

	UFUNCTION(BlueprintPure, Category="Switch")
	bool IsOn() const { return bOn; }

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleClicked(UPrimitiveComponent* ClickedComponent, FKey ButtonPressed);

	void UpdateVisuals();

	bool bOn = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RockerMaterial;
};
