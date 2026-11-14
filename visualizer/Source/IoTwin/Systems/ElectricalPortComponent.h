// IoTwin - electrical connection point of a device.

#pragma once

#include "CoreMinimal.h"
#include "PowerInterface.h"
#include "Components/SceneComponent.h"
#include "ElectricalPortComponent.generated.h"

/**
 * A port can be wired to any number of other ports. Wiring is symmetric and
 * stored on both ports, so it is saved with the level and survives PIE.
 * In the electrical simulation every port is a junction with the terminals
 * L, N and PE, a wire carries all three conductors. Whether a port has
 * power is computed by the simulator (UIoTwinElectricalSubsystem); changing
 * the wiring rebuilds the simulation model.
 */
UCLASS(ClassGroup=(Electric), meta=(BlueprintSpawnableComponent))
class IOTWIN_API UElectricalPortComponent : public USceneComponent, public IPowerInterface
{
	GENERATED_BODY()

public:
	UElectricalPortComponent();

	virtual bool HasPower_Implementation() const override;
	virtual void ConnectTo_Implementation(const TScriptInterface<IPowerInterface>& other) override;

	/** Removes the wire between this port and Other (both directions). */
	UFUNCTION(BlueprintCallable, Category="Power")
	void DisconnectFrom(UElectricalPortComponent* Other);

	/** Removes all wires of this port. */
	UFUNCTION(BlueprintCallable, Category="Power")
	void DisconnectAll();

	/** Ports this port is wired to. */
	UFUNCTION(BlueprintPure, Category="Power")
	TArray<UElectricalPortComponent*> GetConnections() const;

	/** Marks this port as the output of a power source (informational; the simulator decides about power). */
	UFUNCTION(BlueprintCallable, Category="Power")
	void SetSupplying(bool bSupply);

	UFUNCTION(BlueprintPure, Category="Power")
	bool IsSupplying() const { return bSupplying; }

	/** Output level of a supplying port, 0..1 (dimming, PWM duty) - the dimmer level sent to the simulator. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power", meta=(ClampMin=0.0, ClampMax=1.0))
	float SupplyLevel = 1.0f;

	/** Share of the nominal power a resistive load gets here: (U / 230 V)^2, 0..1. */
	UFUNCTION(BlueprintPure, Category="Power")
	float GetSupplyLevel() const;

	/** Voltage L-N at this port from the simulation, 0 without power or simulation. */
	UFUNCTION(BlueprintPure, Category="Power")
	float GetVoltage() const { return SimulatedVoltage; }

	/** Set by the electrical simulation every frame. */
	void SetSimulatedVoltage(float Voltage) { SimulatedVoltage = Voltage; }

	/** Nominal voltage L-N; a port has power above half of it. */
	static constexpr float NominalVoltage = 230.0f;

	/** Kept for existing callers, use SetSupplying(). */
	void setPowerSource(bool bPowered) { SetSupplying(bPowered); }

	/**
	 * Last port connected via ConnectTo. Kept for existing assets and tools:
	 * a value set here is turned into a wire when the component is registered.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Connection", meta=(ExposeOnSpawn=true))
	TObjectPtr<UElectricalPortComponent> ConnectedPort;

protected:
	virtual void OnRegister() override;

private:
	void AddWire(UElectricalPortComponent* Other);
	void NotifyTopologyChanged() const;

	/** Wired ports (symmetric). */
	UPROPERTY(VisibleAnywhere, Category="Connection")
	TArray<TObjectPtr<UElectricalPortComponent>> Connections;

	UPROPERTY(VisibleInstanceOnly, Category="Power")
	bool bSupplying = false;

	UPROPERTY(VisibleInstanceOnly, Transient, Category="Power")
	float SimulatedVoltage = 0.0f;
};
