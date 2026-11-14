// IoTwin - electrical simulation of the scene (in-process adapter, ADR 0008).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "el_session.h"

#include "IoTwinElectricalSubsystem.generated.h"

class FElectricalSimulationThread;
class IElectricalElement;
class UElectricalPortComponent;

/** Electrical state of one simulated device, as Blueprints see it. */
USTRUCT(BlueprintType)
struct FIoTwinElectricalState
{
	GENERATED_BODY()

	/** Load, socket, junction (port): voltage L-N in V. */
	UPROPERTY(BlueprintReadOnly, Category="IoTwin|Electrical")
	float Voltage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="IoTwin|Electrical")
	float Current = 0.0f;

	/** Load: active power in W. */
	UPROPERTY(BlueprintReadOnly, Category="IoTwin|Electrical")
	float Power = 0.0f;

	/** RCD: residual current in A. */
	UPROPERTY(BlueprintReadOnly, Category="IoTwin|Electrical")
	float ResidualCurrent = 0.0f;

	/** Dimmer: 0..1. */
	UPROPERTY(BlueprintReadOnly, Category="IoTwin|Electrical")
	float Level = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="IoTwin|Electrical")
	bool bOn = false;

	/** Breaker/RCD tripped, fuse blown. */
	UPROPERTY(BlueprintReadOnly, Category="IoTwin|Electrical")
	bool bTripped = false;

	/** Load supplied. */
	UPROPERTY(BlueprintReadOnly, Category="IoTwin|Electrical")
	bool bPowered = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnElectricalEvent, const FString&, Text);

/**
 * Runs the electrical simulation core (libs/elsim) for the scene:
 *
 *  - builds the model from the scene (ports become junctions, IElectricalElement
 *    actors and components add devices and cables) and rebuilds it when the
 *    wiring changes;
 *  - steps it in its own thread with a fixed time step (default 10 ms),
 *    independent of the frame rate;
 *  - takes the newest snapshot and the events every frame and hands them to
 *    the elements; commands (switch, dim, faults, reset) go the other way.
 *
 * Unreal owns the topology, the simulator every electrical value.
 * -IoTwinElectricalDisabled switches it off, -IoTwinShortCircuitAfter=<s>
 * puts a short circuit on the first load after s seconds (smoke tests).
 */
UCLASS()
class IOTWIN_API UIoTwinElectricalSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static UIoTwinElectricalSubsystem* Get(const UObject* WorldContextObject);

	/** The wiring changed: the model is rebuilt in the next frame (electrical state starts fresh). */
	void NotifyTopologyChanged() { bTopologyDirty = true; }

	/** Queues a command for the next simulation step. */
	bool SendCommand(const ElCommand& Command);

	/** Newest state of a device; false if unknown or not computed yet. */
	bool GetDeviceState(const FString& DeviceId, FIoTwinElectricalState& OutState) const;

	/** Voltage L-N at a port (0 if the port is not part of the model). */
	float GetPortVoltage(const UElectricalPortComponent* Port) const;

	UFUNCTION(BlueprintCallable, Category="IoTwin|Electrical")
	bool SetDeviceOn(const FString& DeviceId, bool bOn);

	UFUNCTION(BlueprintCallable, Category="IoTwin|Electrical")
	bool SetDimmerLevel(const FString& DeviceId, float Level);

	/** Impedance between two terminals ("device:terminal"), 0 Ohm = short circuit. */
	UFUNCTION(BlueprintCallable, Category="IoTwin|Electrical")
	bool AddFault(const FString& TerminalA, const FString& TerminalB, float Ohm = 0.0f);

	UFUNCTION(BlueprintCallable, Category="IoTwin|Electrical")
	bool ClearFaults();

	/** Short circuit L-N at a port, or removes all faults if one was set this way. */
	UFUNCTION(BlueprintCallable, Category="IoTwin|Electrical")
	bool ToggleShortCircuit(UElectricalPortComponent* Port);

	UFUNCTION(BlueprintPure, Category="IoTwin|Electrical")
	bool HasShortCircuit() const { return bShortCircuit; }

	/** Re-arms every tripped breaker/RCD and replaces blown fuses. */
	UFUNCTION(BlueprintCallable, Category="IoTwin|Electrical")
	int32 ResetProtection();

	/** Junction id of a port in the model (for faults: "<id>:L"). */
	FString GetPortDeviceId(const UElectricalPortComponent* Port) const;

	UFUNCTION(BlueprintPure, Category="IoTwin|Electrical")
	bool IsRunning() const { return Session != nullptr; }

	/** Simulated time in s. */
	UFUNCTION(BlueprintPure, Category="IoTwin|Electrical")
	double GetSimulatedTime() const { return SimulatedTime; }

	/** Duration of the last simulation step in microseconds (the real-time budget is the step). */
	UFUNCTION(BlueprintPure, Category="IoTwin|Electrical")
	float GetLastStepMicroseconds() const;

	int32 GetDeviceCount() const { return DeviceIds.Num(); }

	/** Latest events, newest last ("1.20 s  F1 tripped (...)"). */
	const TArray<FString>& GetRecentEvents() const { return RecentEvents; }

	UPROPERTY(BlueprintAssignable, Category="IoTwin|Electrical")
	FOnElectricalEvent OnElectricalEvent;

	/** Simulated time per step in s. */
	UPROPERTY(EditAnywhere, Category="IoTwin|Electrical")
	float StepSeconds = 0.01f;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Rebuild();
	void Stop();
	void ApplySnapshot();

	ElSession* Session = nullptr;
	FElectricalSimulationThread* Runner = nullptr;
	bool bTopologyDirty = true;
	bool bEnabled = true;
	bool bShortCircuit = false;
	double SimulatedTime = 0.0;
	double ShortCircuitAfter = -1.0;
	double StartTime = 0.0;

	/** Snapshot index per device id. */
	TMap<FString, int32> DeviceIds;
	TArray<FIoTwinElectricalState> States;
	TMap<TWeakObjectPtr<const UElectricalPortComponent>, FString> PortIds;
	TArray<TWeakObjectPtr<UObject>> Elements;
	TArray<FString> RecentEvents;
};
