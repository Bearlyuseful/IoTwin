// IoTwin - power supply with an output relay, optionally controlled via CAN.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElectricalPortComponent.h"
#include "Bus/CanFrame.h"
#include "Electrical/ElectricalElement.h"
#include "Electrical/IoTwinElectricalSubsystem.h"
#include "PowerSourceActor.generated.h"

class UCanSignalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRelayChanged, bool, bClosed);

/**
 * In the electrical simulation: house connection (230 V, TN-S, loop impedance
 * Ze) -> circuit breaker F1 -> relay output (a dimmer: on/off and level from
 * LAMP_CMD) -> the port.
 */
UCLASS()
class IOTWIN_API APowerSourceActor : public AActor, public IElectricalElement
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

	/** Byte of the relay frame with the brightness 0..100 % (LAMP_CMD byte 1), -1 = not dimmable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power", meta=(ClampMin=-1, ClampMax=7))
	int32 LevelByteIndex = 1;

	/** Relay state when not controlled by CAN. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power")
	bool bOutputEnabled = true;

	UPROPERTY(BlueprintAssignable, Category="Power")
	FOnRelayChanged OnRelayChanged;

	UFUNCTION(BlueprintCallable, Category="Power")
	void SetOutputEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category="Power")
	bool IsOutputEnabled() const { return bOutputEnabled; }

	/** Output level 0..1 (dimming). */
	UFUNCTION(BlueprintPure, Category="Power")
	float GetOutputLevel() const { return Port ? Port->SupplyLevel : 1.0f; }

	/** Rated current of the circuit breaker F1 (characteristic B). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power|Simulation", meta=(ClampMin=0.5))
	float BreakerRatedCurrent = 10.0f;

	/** External loop impedance Ze of the house connection in Ohm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power|Simulation", meta=(ClampMin=0.01))
	float LoopImpedance = 0.35f;

	/** State of F1 and the relay output according to the simulation. */
	UFUNCTION(BlueprintPure, Category="Power|Simulation")
	bool IsBreakerTripped() const { return Breaker.bTripped; }

	const FIoTwinElectricalState& GetBreakerState() const { return Breaker; }
	const FString& GetBreakerId() const { return BreakerId; }

	virtual void DescribeElectrical(FElectricalModelBuilder& Builder) override;
	virtual void ApplyElectrical(const UIoTwinElectricalSubsystem& Simulation) override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleRelaySignal(bool bClosed);

	UFUNCTION()
	void HandleBusFrame(const FCanFrame& Frame);

	int32 RelayFrameId = -1;

	void UpdateHousing();

	FString RelayId;
	FString BreakerId;
	FIoTwinElectricalState Breaker;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HousingMaterial;
};
