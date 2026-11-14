// IoTwin - builds the model of the electrical simulation from the scene.

#pragma once

#include "CoreMinimal.h"

#include "elsim.h"

class UElectricalPortComponent;

/**
 * A device while the model is being built. Unlike a plain ElDevice pointer it
 * stays valid when more devices are added (the device array may move).
 */
struct FElectricalDeviceRef
{
	ElModel* Model = nullptr;
	int32 Index = -1;

	ElDevice* operator->() const { return &Model->devices[Index]; }
	ElDevice& operator*() const { return Model->devices[Index]; }
	explicit operator bool() const { return Model != nullptr && Index >= 0; }
};

/**
 * Collects devices, cables and conductors for an ElModel (libs/elsim).
 * Ids are "<actor>.<part>", limited to the characters and length the core
 * accepts; every port is a junction with the terminals L, N and PE.
 */
class IOTWIN_API FElectricalModelBuilder
{
public:
	explicit FElectricalModelBuilder(ElModel& InModel) : Model(InModel) {}

	/** Id for a part of an object: "<actor>.<suffix>" (or "<actor>.<component>" without suffix). */
	FString MakeId(const UObject* Object, const FString& Suffix = FString()) const;

	/** Adds a device; an empty ref if the id exists. Parameters can be set through the ref while building. */
	FElectricalDeviceRef AddDevice(const FString& Id, ElDeviceType Type, const FString& Label = FString());

	/** Adds a copper cable; returns its id (empty on failure). */
	FString AddCable(const FString& Id, double CrossSectionMm2, double LengthM);

	/** Conductor between two terminals ("device:terminal"); Cable empty = short internal link. */
	bool Connect(const FString& From, const FString& To, const TCHAR* Role, const FString& Cable = FString());

	/** Connects L, N and PE of a three-terminal device (load, socket, junction) with the port. */
	bool ConnectToPort(const FString& Device, const UElectricalPortComponent* Port, const FString& Cable = FString());

	/** Junction id of a port; empty if the port is not part of the model. */
	FString PortId(const UElectricalPortComponent* Port) const;

	/** The wire between A and B is described by the caller (e.g. as a cable), not as a short link. */
	void ClaimWire(const UElectricalPortComponent* A, const UElectricalPortComponent* B);

	// Used by the subsystem.
	FString AddPort(const UElectricalPortComponent* Port);
	bool IsWireClaimed(const UElectricalPortComponent* A, const UElectricalPortComponent* B) const;
	int32 GetFailures() const { return Failures; }

	/** Replaces characters the core does not accept and shortens to its id length. */
	static FString SanitizeId(const FString& Id);

private:
	ElModel& Model;
	TMap<const UElectricalPortComponent*, FString> Ports;
	TSet<TPair<const UElectricalPortComponent*, const UElectricalPortComponent*>> ClaimedWires;
	int32 Failures = 0;
	int32 Counter = 0;
};
