// IoTwin - consumer (load) that is supplied through its input port.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Electrical/ElectricalElement.h"
#include "ElectricalPortComponent.h"
#include "ElectricalDeviceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPoweredChanged, bool, bPowered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPowerLevelChanged, float, Level);

/**
 * A resistive load (lamp, heater, ...) in the electrical simulation: its L, N
 * and PE terminals are connected to the input port. Whether it is powered,
 * and with how much power, comes from the simulator (UIoTwinElectricalSubsystem).
 */
UCLASS(ClassGroup=(Electric), meta=(BlueprintSpawnableComponent))
class IOTWIN_API UElectricalDeviceComponent : public UActorComponent, public IElectricalElement
{
	GENERATED_BODY()

public:
	UElectricalDeviceComponent();

	virtual void DescribeElectrical(FElectricalModelBuilder& Builder) override;
	virtual void ApplyElectrical(const UIoTwinElectricalSubsystem& Simulation) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power")
	UElectricalPortComponent* InputPort;

	/** Rated power at the rated voltage in W. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power", meta=(ClampMin=0.1))
	float RatedPower = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power", meta=(ClampMin=1.0))
	float RatedVoltage = 230.0f;

	/** Supplied according to the simulation (at least 1 % of the rated power). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power")
	bool bIsPowered = false;

	/** Fired when the device gets or loses power (and once at start). */
	UPROPERTY(BlueprintAssignable, Category="Power")
	FOnPoweredChanged OnPoweredChanged;

	/** Share of the rated power the device receives, 0..1 (dimming, voltage drop). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Power")
	float PowerLevel = 0.0f;

	/** Fired when the power level changes (and once at start). */
	UPROPERTY(BlueprintAssignable, Category="Power")
	FOnPowerLevelChanged OnPowerLevelChanged;

	/** Results of the simulation: voltage L-N (V), current (A), power (W). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Power")
	float Voltage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Power")
	float Current = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Power")
	float Power = 0.0f;

	/** Id of the load in the simulation model (set while the model is built). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Power")
	FString SimulationId;

private:
	bool bInitialised = false;
};
