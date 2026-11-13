// IoTwin - state of iotwin-bus reported by "!status" (counters and injected faults).

#pragma once

#include "CoreMinimal.h"
#include "BusStatus.generated.h"

/** Answer of iotwin-bus to "!status", see docs/dev/protocol.md. */
USTRUCT(BlueprintType)
struct IOTWIN_API FIoTwinBusStatus
{
	GENERATED_BODY()

	/** A status line was received recently. */
	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	int32 Nodes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	int64 FramesRouted = 0;

	/** Frames lost by injected faults (drop, silence). */
	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	int64 FramesDropped = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	int64 FramesDelayed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	float DropPercent = 0.0f;

	/** Drop applies only to frames matching an id filter. */
	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	bool bDropFiltered = false;

	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	int32 DelayMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	int32 JitterMs = 0;

	/** Remaining time of an injected silence. */
	UPROPERTY(BlueprintReadOnly, Category = "IoTwin|Bus")
	int32 SilenceMs = 0;

	bool HasFaults() const { return DropPercent > 0.0f || DelayMs > 0 || JitterMs > 0 || SilenceMs > 0; }

	/** "drop 100 % (filtered), delay 250 +-50 ms, silent 1.2 s" or empty. */
	FString DescribeFaults() const;

	/** Parses "!status nodes=2 routed=10 dropped=0 delayed=0 drop=0.0% delay=0 jitter=0 silence=0". */
	static bool Parse(const FString& Line, FIoTwinBusStatus& OutStatus);
};
