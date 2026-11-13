// IoTwin - read-only view of the component database (data/components/**/*.json).

#include "Schematic/ComponentCatalog.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString Number(double Value)
	{
		// Up to three decimals, without trailing zeros.
		FString Text = FString::Printf(TEXT("%.3f"), Value);
		while (Text.EndsWith(TEXT("0")))
		{
			Text.LeftChopInline(1);
		}
		Text.RemoveFromEnd(TEXT("."));
		return Text;
	}

	bool TryNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& OutValue)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
		if (!Value.IsValid() || Value->Type != EJson::Number)
		{
			return false;
		}
		OutValue = Value->AsNumber();
		return true;
	}
}

void FComponentCatalog::ChooseScale(double Magnitude, const FString& Unit, double& OutDivisor, FString& OutSuffix)
{
	OutDivisor = 1.0;
	OutSuffix = Unit;
	if (Unit == TEXT("Hz"))
	{
		if (Magnitude >= 1.0e6)
		{
			OutDivisor = 1.0e6;
			OutSuffix = TEXT("MHz");
		}
		else if (Magnitude >= 1.0e3)
		{
			OutDivisor = 1.0e3;
			OutSuffix = TEXT("kHz");
		}
	}
	else if (Unit == TEXT("B"))
	{
		if (Magnitude >= 1024.0 * 1024.0)
		{
			OutDivisor = 1024.0 * 1024.0;
			OutSuffix = TEXT("MiB");
		}
		else if (Magnitude >= 1024.0)
		{
			OutDivisor = 1024.0;
			OutSuffix = TEXT("KiB");
		}
	}
	else if (Unit == TEXT("A") || Unit == TEXT("V") || Unit == TEXT("W") || Unit == TEXT("s"))
	{
		if (Magnitude > 0.0 && Magnitude < 1.0e-3)
		{
			OutDivisor = 1.0e-6;
			OutSuffix = TEXT("µ") + Unit; // micro sign
		}
		else if (Magnitude > 0.0 && Magnitude < 1.0)
		{
			OutDivisor = 1.0e-3;
			OutSuffix = TEXT("m") + Unit;
		}
	}
}

FString FComponentCatalog::FormatQuantity(double Value, const FString& Unit)
{
	double Divisor = 1.0;
	FString Suffix;
	ChooseScale(FMath::Abs(Value), Unit, Divisor, Suffix);
	return Suffix.IsEmpty() ? Number(Value / Divisor) : Number(Value / Divisor) + TEXT(" ") + Suffix;
}

FString FComponentCatalog::FormatParameter(double Value, bool bHasValue, double Min, bool bHasMin, double Typ,
	bool bHasTyp, double Max, bool bHasMax, const FString& Unit)
{
	if (bHasValue)
	{
		return FormatQuantity(Value, Unit);
	}
	if (bHasMin && bHasMax)
	{
		double Divisor = 1.0;
		FString Suffix;
		ChooseScale(FMath::Max(FMath::Abs(Min), FMath::Abs(Max)), Unit, Divisor, Suffix);
		const FString Range = Number(Min / Divisor) + TEXT(" .. ") + Number(Max / Divisor);
		return Suffix.IsEmpty() ? Range : Range + TEXT(" ") + Suffix;
	}
	if (bHasTyp)
	{
		return TEXT("typ ") + FormatQuantity(Typ, Unit);
	}
	if (bHasMax)
	{
		return TEXT("max ") + FormatQuantity(Max, Unit);
	}
	if (bHasMin)
	{
		return TEXT("min ") + FormatQuantity(Min, Unit);
	}
	return Unit.IsEmpty() ? TEXT("?") : FString::Printf(TEXT("? %s"), *Unit);
}

bool FComponentCatalog::AddFromString(const FString& Json, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("invalid JSON");
		return false;
	}

	FComponentInfo Info;
	if (!Root->TryGetStringField(TEXT("id"), Info.Id) || Info.Id.IsEmpty())
	{
		OutError = TEXT("component without id");
		return false;
	}
	Root->TryGetStringField(TEXT("name"), Info.Name);
	Root->TryGetStringField(TEXT("category"), Info.Category);
	Root->TryGetStringField(TEXT("manufacturer"), Info.Manufacturer);
	Root->TryGetStringField(TEXT("description"), Info.Description);

	const TSharedPtr<FJsonObject>* Parameters = nullptr;
	if (Root->TryGetObjectField(TEXT("parameters"), Parameters))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Parameters)->Values)
		{
			const TSharedPtr<FJsonObject> Parameter = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
			if (!Parameter.IsValid())
			{
				continue;
			}
			double Value = 0.0, Min = 0.0, Typ = 0.0, Max = 0.0;
			const bool bValue = TryNumber(Parameter, TEXT("value"), Value);
			const bool bMin = TryNumber(Parameter, TEXT("min"), Min);
			const bool bTyp = TryNumber(Parameter, TEXT("typ"), Typ);
			const bool bMax = TryNumber(Parameter, TEXT("max"), Max);
			FString Unit;
			Parameter->TryGetStringField(TEXT("unit"), Unit);

			FComponentParameter& Entry = Info.Parameters.AddDefaulted_GetRef();
			Entry.Name = Pair.Key;
			Entry.Display = FormatParameter(Value, bValue, Min, bMin, Typ, bTyp, Max, bMax, Unit);
			Parameter->TryGetBoolField(TEXT("verified"), Entry.bVerified);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Contains = nullptr;
	if (Root->TryGetArrayField(TEXT("contains"), Contains))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Contains)
		{
			const TSharedPtr<FJsonObject> Part = Value->AsObject();
			if (Part.IsValid())
			{
				FComponentPart& Entry = Info.Contains.AddDefaulted_GetRef();
				Part->TryGetStringField(TEXT("instance"), Entry.Instance);
				Part->TryGetStringField(TEXT("component"), Entry.ComponentId);
			}
		}
	}

	Components.Add(Info.Id, MoveTemp(Info));
	return true;
}

int32 FComponentCatalog::LoadDirectory(const FString& Directory, TArray<FString>* OutErrors)
{
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.json"), /*Files=*/true, /*Directories=*/false);
	Files.Sort();
	int32 Loaded = 0;
	for (const FString& File : Files)
	{
		FString Json, Error;
		if (FFileHelper::LoadFileToString(Json, *File) && AddFromString(Json, Error))
		{
			++Loaded;
		}
		else if (OutErrors)
		{
			OutErrors->Add(FString::Printf(TEXT("%s: %s"), *File, Error.IsEmpty() ? TEXT("cannot read") : *Error));
		}
	}
	return Loaded;
}
