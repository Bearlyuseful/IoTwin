// IoTwin - model behind the 2D schematic view: system description, DBC,
// component data and the live activity of every node.

#pragma once

#include "CoreMinimal.h"
#include "Bus/CanFrame.h"
#include "Schematic/CanDatabase.h"
#include "Schematic/ComponentCatalog.h"
#include "Schematic/SystemDescription.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IoTwinSchematicSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSchematicViewChanged, bool, bSchematicVisible);

/** Traffic attributed to one node of the system. */
struct FSchematicNodeActivity
{
	double LastSeen = -1.0;
	FString LastMessage;
	int32 FrameCount = 0;
};

/** One line of the message log in the schematic view. */
struct FSchematicLogEntry
{
	double Time = 0.0;
	FString NodeId;  // producer, empty if unknown
	FString Raw;     // "100#01"
	FString Decoded; // "LAMP_CMD LampOn=1 (On)", empty without DBC entry
};

/** A frame travelling from its producer over the bus to its consumers (animation). */
struct FSchematicPulse
{
	double Time = 0.0;
	FString From;
	TArray<FString> To;
};

/**
 * Loads the system description (data/systems/*.json) with its DBC files and the
 * component database, follows the traffic of UIoTwinBusSubsystem and owns the
 * 3D / schematic view switch.
 *
 * Configure in DefaultGame.ini ([/Script/IoTwin.IoTwinSchematicSubsystem]) or per
 * run with -IoTwinSystem=<file>, -IoTwinView=Schematic and -IoTwinSelectNode=<node id>.
 */
UCLASS(Config = Game)
class IOTWIN_API UIoTwinSchematicSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Convenience accessor, returns nullptr outside of a game world. */
	static UIoTwinSchematicSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "IoTwin|Schematic")
	void ToggleView();

	UFUNCTION(BlueprintCallable, Category = "IoTwin|Schematic")
	void SetSchematicVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "IoTwin|Schematic")
	bool IsSchematicVisible() const { return bSchematicVisible; }

	/** Fired when the view switches between 3D scene and schematic. */
	UPROPERTY(BlueprintAssignable, Category = "IoTwin|Schematic")
	FOnSchematicViewChanged OnViewChanged;

	/** Loads a system file plus the DBC files it references and the component database next to it. */
	bool LoadSystem(const FString& Path);

	bool HasSystem() const { return bSystemLoaded; }
	const FSystemDescription& GetSystem() const { return System; }
	const FString& GetSystemPath() const { return SystemPath; }
	const FString& GetLoadError() const { return LoadError; }
	const FComponentCatalog& GetComponents() const { return Components; }

	/** Bus that carries the frames of iotwin-bus (first CAN bus of the system), nullptr if none. */
	const FSystemBus* GetLiveBus() const;

	/** "LAMP_CMD LampOn=1 (On)" decoded with the DBC of the live bus, empty if unknown. */
	FString Describe(const FCanFrame& Frame, FString* OutMessageName = nullptr) const;

	/** Node that most likely produced a message ("" if unknown). */
	FString GuessProducer(const FString& MessageName, bool bSentByThisNode) const;

	/** Nodes that consume a message; all other bus members if the system does not say. */
	TArray<FString> GetConsumers(const FString& MessageName, const FString& Producer) const;

	/** Records a frame seen on the live bus. Public for tests. */
	void RecordFrame(const FCanFrame& Frame, bool bSentByThisNode, double Now);

	const FSchematicNodeActivity* FindActivity(const FString& NodeId) const { return Activity.Find(NodeId); }
	const TArray<FSchematicLogEntry>& GetLog() const { return Log; }
	const TArray<FSchematicPulse>& GetPulses() const { return Pulses; }
	int32 GetTotalFrames() const { return TotalFrames; }
	float GetFramesPerSecond(double Now) const;

	/** Node selected in the schematic, empty if none. */
	FString SelectedNode;

	/** Relative to the repository / package root, or absolute. */
	UPROPERTY(Config, EditAnywhere, Category = "IoTwin|Schematic")
	FString SystemFile = TEXT("data/systems/light-demo.json");

	/** Id of this visualizer in the system description. */
	UPROPERTY(Config, EditAnywhere, Category = "IoTwin|Schematic")
	FString SelfNode = TEXT("visualizer");

	static constexpr int32 MaxLogEntries = 14;
	static constexpr int32 MaxPulses = 24;
	static constexpr double PulseDuration = 0.8;

private:
	UFUNCTION()
	void HandleFrameReceived(const FCanFrame& Frame);

	UFUNCTION()
	void HandleFrameSent(const FCanFrame& Frame);

	FString FindSystemFile() const;

	FSystemDescription System;
	FCanDatabase LiveDatabase;
	FComponentCatalog Components;
	FString SystemPath;
	FString LoadError;
	bool bSystemLoaded = false;
	bool bSchematicVisible = false;

	TMap<FString, FSchematicNodeActivity> Activity;
	TArray<FSchematicLogEntry> Log;
	TArray<FSchematicPulse> Pulses;
	int32 TotalFrames = 0;
	double RateWindowStart = 0.0;
	int32 RateWindowFrames = 0;
	float LastRate = 0.0f;
};
