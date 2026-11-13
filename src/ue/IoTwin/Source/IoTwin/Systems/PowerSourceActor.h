// IoTwin - power supply with an output relay, optionally controlled via CAN.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElectricalPortComponent.h"
#include "PowerSourceActor.generated.h"

class UCanSignalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRelayChanged, bool, bClosed);

UCLASS()
class IOTWIN_API APowerSourceActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	APowerSourceActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Power")
	UElectricalPortComponent* Port;

	/** Housing; its colour shows the relay state (green = closed). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Power")
	UStaticMeshComponent* Housing;

	/** Material with a "Color" vector parameter used for the housing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Power")
	TObjectPtr<UMaterialInterface> HousingBaseMaterial;

	/** CAN signal driving the relay when bControlledByCan is set (default 0x100 byte 0 bit 0). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Power")
	UCanSignalComponent* RelayControl;

	/** If true the output relay follows RelayControl; it stays open until a frame arrives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power")
	bool bControlledByCan = false;

	/** Relay state when not controlled by CAN. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power")
	bool bOutputEnabled = true;

	UPROPERTY(BlueprintAssignable, Category="Power")
	FOnRelayChanged OnRelayChanged;

	UFUNCTION(BlueprintCallable, Category="Power")
	void SetOutputEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category="Power")
	bool IsOutputEnabled() const { return bOutputEnabled; }

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleRelaySignal(bool bClosed);

	void UpdateHousing();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HousingMaterial;
};
