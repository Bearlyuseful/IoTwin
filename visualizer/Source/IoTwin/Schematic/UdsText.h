// IoTwin - short text for CAN frames carrying UDS over ISO-TP (same names as libs/can/uds.c).

#pragma once

#include "CoreMinimal.h"

/** Describes ISO-TP frames with UDS payload for the schematic message log. */
struct IOTWIN_API FUdsText
{
	/**
	 * "SF ReadDataByIdentifier F190", "SF +DiagnosticSessionControl extended",
	 * "SF NRC WriteDataByIdentifier: serviceNotSupportedInActiveSession",
	 * "FF 20 bytes +ReadDataByIdentifier F190", "CF 1", "FC continue".
	 */
	static FString Describe(const TArray<uint8>& Data);

	static FString ServiceName(uint8 Sid);
	static FString NrcName(uint8 Nrc);
};
