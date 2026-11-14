// IoTwin - builds the model of the electrical simulation from the scene.

#include "Electrical/ElectricalModelBuilder.h"

#include "GameFramework/Actor.h"
#include "IoTwin.h"
#include "Systems/ElectricalPortComponent.h"

namespace
{
	TPair<const UElectricalPortComponent*, const UElectricalPortComponent*> WireKey(
		const UElectricalPortComponent* A, const UElectricalPortComponent* B)
	{
		return A < B ? MakeTuple(A, B) : MakeTuple(B, A);
	}
}

FString FElectricalModelBuilder::SanitizeId(const FString& Id)
{
	FString Out;
	Out.Reserve(Id.Len());
	for (const TCHAR Char : Id)
	{
		const bool bAllowed = (Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z') ||
			(Char >= '0' && Char <= '9') || Char == '_' || Char == '.' || Char == '-';
		Out.AppendChar(bAllowed ? Char : TEXT('_'));
	}
	return Out.Left(EL_ID_MAX - 1);
}

FString FElectricalModelBuilder::MakeId(const UObject* Object, const FString& Suffix) const
{
	if (Object == nullptr)
	{
		return SanitizeId(Suffix);
	}
	const AActor* Owner = Cast<AActor>(Object);
	if (Owner == nullptr)
	{
		Owner = Object->GetTypedOuter<AActor>();
	}
	const FString Part = Suffix.IsEmpty() ? Object->GetName() : Suffix;
	return SanitizeId(Owner ? FString::Printf(TEXT("%s.%s"), *Owner->GetName(), *Part) : Part);
}

FElectricalDeviceRef FElectricalModelBuilder::AddDevice(const FString& Id, ElDeviceType Type, const FString& Label)
{
	const FString Clean = SanitizeId(Id);
	const int Index = el_model_add_device(&Model, TCHAR_TO_UTF8(*Clean), Type);
	if (Index < 0)
	{
		++Failures;
		UE_LOG(LogIoTwin, Warning, TEXT("Electrical model: device '%s' could not be added (duplicate id?)"), *Clean);
		return FElectricalDeviceRef();
	}
	const FElectricalDeviceRef Device{&Model, Index};
	if (!Label.IsEmpty())
	{
		FCStringAnsi::Strncpy(Device->label, TCHAR_TO_UTF8(*Label), EL_TEXT_MAX);
	}
	return Device;
}

FString FElectricalModelBuilder::AddCable(const FString& Id, double CrossSectionMm2, double LengthM)
{
	const FString Clean = SanitizeId(Id);
	if (el_model_add_cable(&Model, TCHAR_TO_UTF8(*Clean), "", CrossSectionMm2, LengthM) < 0)
	{
		++Failures;
		UE_LOG(LogIoTwin, Warning, TEXT("Electrical model: cable '%s' could not be added"), *Clean);
		return FString();
	}
	return Clean;
}

bool FElectricalModelBuilder::Connect(const FString& From, const FString& To, const TCHAR* Role, const FString& Cable)
{
	const FTCHARToUTF8 FromText(*From);
	const FTCHARToUTF8 ToText(*To);
	const FTCHARToUTF8 RoleText(Role);
	const FTCHARToUTF8 CableText(*Cable);
	if (el_model_connect(&Model, nullptr, FromText.Get(), ToText.Get(), RoleText.Get(),
			Cable.IsEmpty() ? nullptr : CableText.Get()) < 0)
	{
		++Failures;
		UE_LOG(LogIoTwin, Warning, TEXT("Electrical model: cannot connect %s - %s"), *From, *To);
		return false;
	}
	return true;
}

bool FElectricalModelBuilder::ConnectToPort(const FString& Device, const UElectricalPortComponent* Port, const FString& Cable)
{
	const FString Junction = PortId(Port);
	if (Junction.IsEmpty())
	{
		++Failures;
		return false;
	}
	bool bOk = true;
	for (const TCHAR* Terminal : {TEXT("L"), TEXT("N"), TEXT("PE")})
	{
		bOk &= Connect(Device + TEXT(":") + Terminal, Junction + TEXT(":") + Terminal, Terminal, Cable);
	}
	return bOk;
}

FString FElectricalModelBuilder::PortId(const UElectricalPortComponent* Port) const
{
	const FString* Id = Ports.Find(Port);
	return Id ? *Id : FString();
}

FString FElectricalModelBuilder::AddPort(const UElectricalPortComponent* Port)
{
	FString Id = MakeId(Port);
	if (el_model_find_device(&Model, TCHAR_TO_UTF8(*Id)) >= 0)
	{
		Id = SanitizeId(FString::Printf(TEXT("port%d.%s"), ++Counter, *Port->GetName()));
	}
	if (!AddDevice(Id, EL_JUNCTION))
	{
		return FString();
	}
	Ports.Add(Port, Id);
	return Id;
}

void FElectricalModelBuilder::ClaimWire(const UElectricalPortComponent* A, const UElectricalPortComponent* B)
{
	ClaimedWires.Add(WireKey(A, B));
}

bool FElectricalModelBuilder::IsWireClaimed(const UElectricalPortComponent* A, const UElectricalPortComponent* B) const
{
	return ClaimedWires.Contains(WireKey(A, B));
}
