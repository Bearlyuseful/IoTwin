// IoTwin - DBC reader for the visualizer (messages, signals, value tables).
//
// Same bit semantics as libs/can (can_signal.c / dbc.c): Intel = little endian,
// Motorola = big endian in DBC sawtooth numbering. Multiplexing is not
// evaluated here (all signals are decoded).

#pragma once

#include "CoreMinimal.h"
#include "Bus/CanFrame.h"

struct IOTWIN_API FCanSignalDef
{
	FString Name;
	int32 StartBit = 0;
	int32 Length = 1;
	bool bIntel = true;
	bool bSigned = false;
	double Factor = 1.0;
	double Offset = 0.0;
	FString Unit;
	TMap<int64, FString> ValueNames;
};

struct IOTWIN_API FCanMessageDef
{
	uint32 Id = 0;
	bool bExtended = false;
	FString Name;
	FString Transmitter;
	TArray<FCanSignalDef> Signals;
};

class IOTWIN_API FCanDatabase
{
public:
	bool LoadFromFile(const FString& Path, FString& OutError);
	bool LoadFromString(const FString& Text, FString& OutError);

	const FCanMessageDef* FindMessage(int32 Id, bool bExtended) const;
	const FCanMessageDef* FindMessageByName(const FString& Name) const;
	int32 NumMessages() const { return Messages.Num(); }

	/** "LAMP_CMD LampOn=1 (On)", or an empty string for unknown ids. */
	FString Describe(const FCanFrame& Frame) const;

	static uint64 ExtractRaw(const FCanSignalDef& Signal, const uint8 Data[8]);
	static double Decode(const FCanSignalDef& Signal, const uint8 Data[8], int64* OutRawSigned = nullptr);

private:
	TArray<FCanMessageDef> Messages;
};
