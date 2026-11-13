// IoTwin - binds a single bit of a CAN frame to an actor.

#include "Bus/CanSignalComponent.h"

#include "Bus/IoTwinBusSubsystem.h"
#include "GameFramework/Actor.h"
#include "IoTwin.h"

UCanSignalComponent::UCanSignalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UCanSignalComponent::Configure(ECanSignalDirection InDirection, int32 InId, bool bInExtendedId, int32 InByteIndex, int32 InBitIndex)
{
	Direction = InDirection;
	bExtendedId = bInExtendedId;
	CanId = bInExtendedId ? FString::Printf(TEXT("%08X"), InId) : FString::Printf(TEXT("%03X"), InId);
	ByteIndex = FMath::Clamp(InByteIndex, 0, 7);
	BitIndex = FMath::Clamp(InBitIndex, 0, 7);
}

FString UCanSignalComponent::Describe() const
{
	return FString::Printf(TEXT("0x%s byte %d bit %d"), *CanId, ByteIndex, BitIndex);
}

void UCanSignalComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!FCanFrame::ParseId(CanId, bExtendedId, ResolvedId))
	{
		UE_LOG(LogIoTwinBus, Warning, TEXT("%s: invalid CAN id '%s', signal disabled"), *GetPathName(), *CanId);
		ResolvedId = INDEX_NONE;
		return;
	}

	UIoTwinBusSubsystem* Subsystem = UIoTwinBusSubsystem::Get(this);
	Bus = Subsystem;
	if (Subsystem == nullptr)
	{
		return;
	}
	Subsystem->OnConnectionChanged.AddDynamic(this, &UCanSignalComponent::HandleConnectionChanged);

	if (Direction == ECanSignalDirection::Receive)
	{
		Subsystem->OnFrameReceived.AddDynamic(this, &UCanSignalComponent::HandleFrame);
		FCanFrame Last;
		if (Subsystem->GetLastFrame(ResolvedId, bExtendedId, Last))
		{
			HandleFrame(Last);
		}
	}
	else if (TransmitCycleSeconds > 0.f)
	{
		SetComponentTickEnabled(true);
	}
}

void UCanSignalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UIoTwinBusSubsystem* Subsystem = Bus.Get())
	{
		Subsystem->OnFrameReceived.RemoveDynamic(this, &UCanSignalComponent::HandleFrame);
		Subsystem->OnConnectionChanged.RemoveDynamic(this, &UCanSignalComponent::HandleConnectionChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void UCanSignalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TimeSinceTransmit += DeltaTime;
	if (bHasValue && TransmitCycleSeconds > 0.f && TimeSinceTransmit >= TransmitCycleSeconds)
	{
		Transmit();
	}
}

void UCanSignalComponent::SetValue(bool bNewValue)
{
	const bool bChanged = !bHasValue || bNewValue != bValue;
	ApplyValue(bNewValue);
	if (Direction == ECanSignalDirection::Transmit && bChanged)
	{
		Transmit();
	}
}

bool UCanSignalComponent::Matches(const FCanFrame& Frame) const
{
	return ResolvedId != INDEX_NONE && Frame.Id == ResolvedId && Frame.bExtended == bExtendedId && !Frame.bRemote;
}

void UCanSignalComponent::HandleFrame(const FCanFrame& Frame)
{
	if (!Matches(Frame))
	{
		return;
	}
	if (!Frame.Data.IsValidIndex(ByteIndex))
	{
		UE_LOG(LogIoTwinBus, Verbose, TEXT("%s: frame %s too short for byte %d"), *GetPathName(), *Frame.ToString(), ByteIndex);
		return;
	}
	ApplyValue(Frame.GetBit(ByteIndex, BitIndex));
}

void UCanSignalComponent::HandleConnectionChanged(bool bConnected)
{
	if (Direction != ECanSignalDirection::Transmit)
	{
		return;
	}
	if (!bConnected)
	{
		// Nodes may have restarted meanwhile: announce the value again after reconnecting.
		bTransmitPending = bHasValue;
		return;
	}
	if (bHasValue && bTransmitPending)
	{
		Transmit();
	}
}

void UCanSignalComponent::ApplyValue(bool bNewValue)
{
	const bool bChanged = !bHasValue || bNewValue != bValue;
	bValue = bNewValue;
	bHasValue = true;
	if (bChanged)
	{
		OnValueChanged.Broadcast(bValue);
	}
}

void UCanSignalComponent::Transmit()
{
	TimeSinceTransmit = 0.f;
	UIoTwinBusSubsystem* Subsystem = Bus.Get();
	if (Subsystem == nullptr || ResolvedId == INDEX_NONE)
	{
		bTransmitPending = true;
		return;
	}
	TArray<uint8> Data;
	Data.SetNumZeroed(ByteIndex + 1);
	if (bValue)
	{
		Data[ByteIndex] |= static_cast<uint8>(1u << BitIndex);
	}
	bTransmitPending = !Subsystem->SendFrame(FCanFrame::MakeData(ResolvedId, MoveTemp(Data), bExtendedId));
}
