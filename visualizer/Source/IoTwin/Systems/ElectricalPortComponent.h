// IoTwin - electrical connection point of a device.

#pragma once

#include "CoreMinimal.h"
#include "PowerInterface.h"
#include "Components/SceneComponent.h"
#include "ElectricalPortComponent.generated.h"

/**
 * A port can be wired to any number of other ports. Wiring is symmetric and
 * stored on both ports, so it is saved with the level and survives PIE.
 * A port has power if a supplying port (e.g. of a power source) is reachable
 * through the wiring graph. The search is cycle-safe.
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

	/** Marks this port as a supply (power source output). */
	UFUNCTION(BlueprintCallable, Category="Power")
	void SetSupplying(bool bSupply);

	UFUNCTION(BlueprintPure, Category="Power")
	bool IsSupplying() const { return bSupplying; }

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

	/** Wired ports (symmetric). */
	UPROPERTY(VisibleAnywhere, Category="Connection")
	TArray<TObjectPtr<UElectricalPortComponent>> Connections;

	UPROPERTY(VisibleInstanceOnly, Category="Power")
	bool bSupplying = false;
};
