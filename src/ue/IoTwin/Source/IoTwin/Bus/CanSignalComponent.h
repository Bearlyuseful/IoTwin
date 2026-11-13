// IoTwin - binds a single bit of a CAN frame to an actor.

#pragma once

#include "CoreMinimal.h"
#include "Bus/CanFrame.h"
#include "Components/ActorComponent.h"
#include "CanSignalComponent.generated.h"

class UIoTwinBusSubsystem;

UENUM(BlueprintType)
enum class ECanSignalDirection : uint8
{
	/** The value follows frames received from the bus. */
	Receive,
	/** SetValue() sends frames to the bus. */
	Transmit
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCanSignalChanged, bool, bNewValue);

/**
 * A boolean signal mapped to one bit of a CAN frame, e.g. "0x100 byte 0 bit 0".
 * Receive signals update from the bus, transmit signals put the value on the bus.
 */
UCLASS(ClassGroup = (IoTwin), meta = (BlueprintSpawnableComponent))
class IOTWIN_API UCanSignalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCanSignalComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAN")
	ECanSignalDirection Direction = ECanSignalDirection::Receive;

	/** Identifier in hex as written on the bus, e.g. "100". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAN")
	FString CanId = TEXT("100");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAN")
	bool bExtendedId = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAN", meta = (ClampMin = 0, ClampMax = 7))
	int32 ByteIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAN", meta = (ClampMin = 0, ClampMax = 7))
	int32 BitIndex = 0;

	/** Transmit only: resend the value periodically (0 = only on change and on reconnect). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAN", meta = (ClampMin = 0.0, Units = "s"))
	float TransmitCycleSeconds = 0.f;

	/** Fired when the value changes. */
	UPROPERTY(BlueprintAssignable, Category = "CAN")
	FOnCanSignalChanged OnValueChanged;

	UFUNCTION(BlueprintPure, Category = "CAN")
	bool GetValue() const { return bValue; }

	/** false until a frame was received (Receive) or a value was set (Transmit). */
	UFUNCTION(BlueprintPure, Category = "CAN")
	bool HasValue() const { return bHasValue; }

	/** Transmit: stores and sends the value. Receive: local override until the next frame. */
	UFUNCTION(BlueprintCallable, Category = "CAN")
	void SetValue(bool bNewValue);

	/** Human readable mapping, e.g. "0x100 byte 0 bit 0". */
	UFUNCTION(BlueprintPure, Category = "CAN")
	FString Describe() const;

	/** Configures the signal from C++, e.g. before FinishSpawning an actor. */
	void Configure(ECanSignalDirection InDirection, int32 InId, bool bInExtendedId = false, int32 InByteIndex = 0, int32 InBitIndex = 0);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION()
	void HandleFrame(const FCanFrame& Frame);

	UFUNCTION()
	void HandleConnectionChanged(bool bConnected);

	bool Matches(const FCanFrame& Frame) const;
	void ApplyValue(bool bNewValue);
	void Transmit();

	UPROPERTY(VisibleInstanceOnly, Category = "CAN")
	bool bValue = false;

	bool bHasValue = false;
	/** Transmit: the current value has not reached the bus yet (offline or reconnected). */
	bool bTransmitPending = false;
	int32 ResolvedId = INDEX_NONE;
	float TimeSinceTransmit = 0.f;
	TWeakObjectPtr<UIoTwinBusSubsystem> Bus;
};
