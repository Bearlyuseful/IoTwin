// IoTwin - read-only view of the component database (data/components/**/*.json).

#pragma once

#include "CoreMinimal.h"

struct IOTWIN_API FComponentParameter
{
	FString Name;
	/** Formatted value with unit, e.g. "1.7 .. 3.6 V", "max 64 MHz", "?" when unknown. */
	FString Display;
	bool bVerified = false;
};

struct IOTWIN_API FComponentPart
{
	FString Instance;
	FString ComponentId;
};

struct IOTWIN_API FComponentInfo
{
	FString Id;
	FString Name;
	FString Category;
	FString Manufacturer;
	FString Description;
	TArray<FComponentParameter> Parameters;
	TArray<FComponentPart> Contains;
};

class IOTWIN_API FComponentCatalog
{
public:
	/** Loads every *.json below Directory. Files that fail to parse are skipped and counted. */
	int32 LoadDirectory(const FString& Directory, TArray<FString>* OutErrors = nullptr);
	bool AddFromString(const FString& Json, FString& OutError);

	const FComponentInfo* Find(const FString& ComponentId) const { return Components.Find(ComponentId); }
	int32 Num() const { return Components.Num(); }

	/** Formats a parameter object ({value|min|typ|max, unit}) for display. */
	static FString FormatParameter(double Value, bool bHasValue, double Min, bool bHasMin, double Typ, bool bHasTyp,
		double Max, bool bHasMax, const FString& Unit);
	/** "64 MHz", "512 KiB", "25 mA", "3.3 V". */
	static FString FormatQuantity(double Value, const FString& Unit);

private:
	/** Picks a display prefix (MHz, KiB, mA, ...) for a magnitude. */
	static void ChooseScale(double Magnitude, const FString& Unit, double& OutDivisor, FString& OutSuffix);

	TMap<FString, FComponentInfo> Components;
};
