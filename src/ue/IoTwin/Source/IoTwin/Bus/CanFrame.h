// IoTwin - CAN frame model shared with iotwin-bus (see docs/dev/protocol.md).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CanFrame.generated.h"

/**
 * One classic CAN frame as exchanged with iotwin-bus.
 * Text form: "123#DEADBEEF" (standard), "1F334455#11" (extended), "123#R" (remote).
 */
USTRUCT(BlueprintType)
struct IOTWIN_API FCanFrame
{
	GENERATED_BODY()

	static constexpr int32 MaxDataLength = 8;
	static constexpr uint32 StandardIdMask = 0x7FF;
	static constexpr uint32 ExtendedIdMask = 0x1FFFFFFF;

	/** 11-bit (standard) or 29-bit (extended) identifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAN")
	int32 Id = 0;

	/** true = 29-bit identifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAN")
	bool bExtended = false;

	/** Remote transmission request. The length of Data is the requested DLC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAN")
	bool bRemote = false;

	/** Payload, 0..8 bytes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAN")
	TArray<uint8> Data;

	/** Identifier range and payload length are valid. */
	bool IsValid() const;

	/** Encodes the frame ("123#DEADBEEF"); empty string if the frame is invalid. */
	FString ToString() const;

	/** Returns bit BitIndex of byte ByteIndex, false if the byte is not present. */
	bool GetBit(int32 ByteIndex, int32 BitIndex) const;

	/** Parses the text form. Returns false (and leaves OutFrame untouched) on malformed input. */
	static bool Parse(const FString& Text, FCanFrame& OutFrame);

	/** Parses an identifier written in hex ("100", "0x1F334455"). */
	static bool ParseId(const FString& HexText, bool bExtended, int32& OutId);

	/** Creates a data frame. */
	static FCanFrame MakeData(int32 Id, TArray<uint8> Data, bool bExtended = false);
};

/** Blueprint helpers for CAN frames. */
UCLASS()
class IOTWIN_API UCanFrameLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Encodes a frame in the bus text format, e.g. "123#DEADBEEF". */
	UFUNCTION(BlueprintPure, Category = "IoTwin|CAN", meta = (DisplayName = "To String (CAN Frame)", CompactNodeTitle = "->", BlueprintAutocast))
	static FString CanFrameToString(const FCanFrame& Frame);

	/** Parses the bus text format. */
	UFUNCTION(BlueprintCallable, Category = "IoTwin|CAN")
	static bool ParseCanFrame(const FString& Text, FCanFrame& Frame);

	/** Reads a single bit of the payload. */
	UFUNCTION(BlueprintPure, Category = "IoTwin|CAN")
	static bool GetCanFrameBit(const FCanFrame& Frame, int32 ByteIndex, int32 BitIndex);
};
