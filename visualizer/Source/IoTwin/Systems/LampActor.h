// IoTwin - lamp: port -> device -> light, with a visible bulb.

#pragma once

#include "CoreMinimal.h"
#include "ElectricalDeviceComponent.h"
#include "ElectricalPortComponent.h"
#include "LightEmitterComponent.h"
#include "GameFramework/Actor.h"
#include "LampActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

UCLASS()
class IOTWIN_API ALampActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ALampActor();

	/** true while the lamp is powered. */
	UFUNCTION(BlueprintPure, Category="Lamp")
	bool IsOn() const { return Device && Device->bIsPowered; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	UElectricalPortComponent* Port;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	UElectricalDeviceComponent* Device;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	ULightEmitterComponent* Light;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	UStaticMeshComponent* Bulb;

	/** Material with a "Color" vector parameter used for the bulb. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp")
	TObjectPtr<UMaterialInterface> BulbBaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lamp")
	FLinearColor BulbOnColor = FLinearColor(1.0f, 0.85f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lamp")
	FLinearColor BulbOffColor = FLinearColor(0.08f, 0.08f, 0.08f);

private:
	UFUNCTION()
	void HandlePoweredChanged(bool bPowered);

	UFUNCTION()
	void HandlePowerLevelChanged(float Level);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BulbMaterial;
};
