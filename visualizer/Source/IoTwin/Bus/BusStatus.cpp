// IoTwin - state of iotwin-bus reported by "!status".

#include "Bus/BusStatus.h"

bool FIoTwinBusStatus::Parse(const FString& Line, FIoTwinBusStatus& OutStatus)
{
	if (!Line.StartsWith(TEXT("!status")))
	{
		return false;
	}
	TArray<FString> Fields;
	Line.ParseIntoArrayWS(Fields);
	FIoTwinBusStatus Status;
	int32 Known = 0;
	for (const FString& Field : Fields)
	{
		FString Key;
		FString Value;
		if (!Field.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		Known++;
		if (Key == TEXT("nodes"))
		{
			Status.Nodes = FCString::Atoi(*Value);
		}
		else if (Key == TEXT("routed"))
		{
			Status.FramesRouted = FCString::Atoi64(*Value);
		}
		else if (Key == TEXT("dropped"))
		{
			Status.FramesDropped = FCString::Atoi64(*Value);
		}
		else if (Key == TEXT("delayed"))
		{
			Status.FramesDelayed = FCString::Atoi64(*Value);
		}
		else if (Key == TEXT("drop"))
		{
			Status.bDropFiltered = Value.Contains(TEXT("(filtered)"));
			Status.DropPercent = FCString::Atof(*Value); // stops at '%'
		}
		else if (Key == TEXT("delay"))
		{
			Status.DelayMs = FCString::Atoi(*Value);
		}
		else if (Key == TEXT("jitter"))
		{
			Status.JitterMs = FCString::Atoi(*Value);
		}
		else if (Key == TEXT("silence"))
		{
			Status.SilenceMs = FCString::Atoi(*Value);
		}
		else
		{
			Known--;
		}
	}
	if (Known == 0)
	{
		return false;
	}
	Status.bValid = true;
	OutStatus = Status;
	return true;
}

FString FIoTwinBusStatus::DescribeFaults() const
{
	TArray<FString> Parts;
	if (DropPercent > 0.0f)
	{
		Parts.Add(FString::Printf(TEXT("drop %s %%%s"), *FString::SanitizeFloat(DropPercent, 0),
			bDropFiltered ? TEXT(" (filtered)") : TEXT("")));
	}
	if (DelayMs > 0 || JitterMs > 0)
	{
		Parts.Add(JitterMs > 0 ? FString::Printf(TEXT("delay %d +-%d ms"), DelayMs, JitterMs)
		                       : FString::Printf(TEXT("delay %d ms"), DelayMs));
	}
	if (SilenceMs > 0)
	{
		Parts.Add(FString::Printf(TEXT("silent %.1f s"), SilenceMs / 1000.0f));
	}
	return FString::Join(Parts, TEXT(", "));
}
