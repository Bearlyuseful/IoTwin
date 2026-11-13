// IoTwin - model behind the 2D schematic view.

#include "Schematic/IoTwinSchematicSubsystem.h"

#include "Bus/IoTwinBusSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "IoTwin.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

void UIoTwinSchematicSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FString ViewOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("IoTwinView="), ViewOverride))
	{
		bSchematicVisible = ViewOverride.Equals(TEXT("Schematic"), ESearchCase::IgnoreCase);
	}
	FParse::Value(FCommandLine::Get(), TEXT("IoTwinSelectNode="), SelectedNode);

	const FString Path = FindSystemFile();
	if (Path.IsEmpty())
	{
		LoadError = FString::Printf(TEXT("system file %s not found"), *SystemFile);
		UE_LOG(LogIoTwin, Warning, TEXT("Schematic: %s"), *LoadError);
	}
	else
	{
		LoadSystem(Path);
	}

	if (UIoTwinBusSubsystem* Bus = Collection.InitializeDependency<UIoTwinBusSubsystem>())
	{
		Bus->OnFrameReceived.AddDynamic(this, &UIoTwinSchematicSubsystem::HandleFrameReceived);
		Bus->OnFrameSent.AddDynamic(this, &UIoTwinSchematicSubsystem::HandleFrameSent);
	}
}

void UIoTwinSchematicSubsystem::Deinitialize()
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIoTwinBusSubsystem* Bus = GameInstance->GetSubsystem<UIoTwinBusSubsystem>())
		{
			Bus->OnFrameReceived.RemoveAll(this);
			Bus->OnFrameSent.RemoveAll(this);
		}
	}
	Super::Deinitialize();
}

UIoTwinSchematicSubsystem* UIoTwinSchematicSubsystem::Get(const UObject* WorldContextObject)
{
	if (GEngine == nullptr || WorldContextObject == nullptr)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UIoTwinSchematicSubsystem>() : nullptr;
}

void UIoTwinSchematicSubsystem::ToggleView()
{
	SetSchematicVisible(!bSchematicVisible);
}

void UIoTwinSchematicSubsystem::SetSchematicVisible(bool bVisible)
{
	if (bSchematicVisible != bVisible)
	{
		bSchematicVisible = bVisible;
		OnViewChanged.Broadcast(bSchematicVisible);
	}
}

FString UIoTwinSchematicSubsystem::FindSystemFile() const
{
	FString Requested = SystemFile;
	FParse::Value(FCommandLine::Get(), TEXT("IoTwinSystem="), Requested);
	if (Requested.IsEmpty())
	{
		return FString();
	}
	if (!FPaths::IsRelative(Requested))
	{
		return FPaths::FileExists(Requested) ? Requested : FString();
	}

	// Source tree: <repo>/visualizer/ is the project, data/ sits next to it.
	// Package: <root>/IoTwin/ is the project directory, data/ is copied to <root>.
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString Bases[] = {
		FPaths::LaunchDir(),
		FPaths::Combine(ProjectDir, TEXT("..")),
		FPaths::Combine(ProjectDir, TEXT(".."), TEXT("..")),
		ProjectDir,
	};
	for (const FString& Base : Bases)
	{
		FString Candidate = FPaths::Combine(Base, Requested);
		FPaths::CollapseRelativeDirectories(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
	return FString();
}

bool UIoTwinSchematicSubsystem::LoadSystem(const FString& Path)
{
	bSystemLoaded = false;
	SystemPath = Path;
	LoadError.Reset();
	LiveDatabase = FCanDatabase();
	Components = FComponentCatalog();

	if (!System.LoadFromFile(Path, LoadError))
	{
		UE_LOG(LogIoTwin, Warning, TEXT("Schematic: cannot load %s: %s"), *Path, *LoadError);
		return false;
	}
	bSystemLoaded = true;

	if (const FSystemBus* Bus = GetLiveBus(); Bus && !Bus->Dbc.IsEmpty())
	{
		FString DbcError;
		if (!LiveDatabase.LoadFromFile(System.ResolvePath(Bus->Dbc), DbcError))
		{
			UE_LOG(LogIoTwin, Warning, TEXT("Schematic: DBC of bus %s: %s"), *Bus->Id, *DbcError);
		}
	}

	// <data>/systems/<file>.json -> <data>/components
	const FString ComponentDir = System.ResolvePath(TEXT("../components"));
	TArray<FString> ComponentErrors;
	const int32 ComponentCount = Components.LoadDirectory(ComponentDir, &ComponentErrors);
	for (const FString& Error : ComponentErrors)
	{
		UE_LOG(LogIoTwin, Warning, TEXT("Schematic: component %s"), *Error);
	}

	UE_LOG(LogIoTwin, Log, TEXT("Schematic: loaded %s (%d nodes, %d DBC messages, %d components)"),
		*Path, System.Nodes.Num(), LiveDatabase.NumMessages(), ComponentCount);
	return true;
}

const FSystemBus* UIoTwinSchematicSubsystem::GetLiveBus() const
{
	return System.Buses.FindByPredicate([](const FSystemBus& Bus) { return Bus.Type == TEXT("can"); });
}

FString UIoTwinSchematicSubsystem::Describe(const FCanFrame& Frame, FString* OutMessageName) const
{
	if (const FCanMessageDef* Message = LiveDatabase.FindMessage(Frame.Id, Frame.bExtended))
	{
		if (OutMessageName)
		{
			*OutMessageName = Message->Name;
		}
		return LiveDatabase.Describe(Frame);
	}
	return FString();
}

FString UIoTwinSchematicSubsystem::GuessProducer(const FString& MessageName, bool bSentByThisNode) const
{
	if (bSentByThisNode)
	{
		return System.FindNode(SelfNode) ? SelfNode : FString();
	}
	const FSystemMessage* Message = MessageName.IsEmpty() ? nullptr : System.FindMessage(MessageName);
	if (Message == nullptr)
	{
		return FString();
	}
	// iotwin-bus does not echo our own frames, so a received frame comes from another producer.
	for (const FString& Producer : Message->Producers)
	{
		if (Producer != SelfNode)
		{
			return Producer;
		}
	}
	return FString();
}

TArray<FString> UIoTwinSchematicSubsystem::GetConsumers(const FString& MessageName, const FString& Producer) const
{
	if (const FSystemMessage* Message = MessageName.IsEmpty() ? nullptr : System.FindMessage(MessageName))
	{
		return Message->Consumers.FilterByPredicate([&Producer](const FString& Node) { return Node != Producer; });
	}
	const FSystemBus* Bus = GetLiveBus();
	return Bus ? Bus->Members.FilterByPredicate([&Producer](const FString& Node) { return Node != Producer; })
	           : TArray<FString>();
}

void UIoTwinSchematicSubsystem::RecordFrame(const FCanFrame& Frame, bool bSentByThisNode, double Now)
{
	FString MessageName;
	const FString Decoded = Describe(Frame, &MessageName);
	const FString Producer = GuessProducer(MessageName, bSentByThisNode);

	++TotalFrames;
	if (Now - RateWindowStart >= 1.0)
	{
		LastRate = (RateWindowStart > 0.0) ? RateWindowFrames / static_cast<float>(Now - RateWindowStart) : 0.0f;
		RateWindowStart = Now;
		RateWindowFrames = 0;
	}
	++RateWindowFrames;

	FSchematicLogEntry& Entry = Log.AddDefaulted_GetRef();
	Entry.Time = Now;
	Entry.NodeId = Producer;
	Entry.Raw = Frame.ToString();
	Entry.Decoded = Decoded;
	if (Log.Num() > MaxLogEntries)
	{
		Log.RemoveAt(0, Log.Num() - MaxLogEntries);
	}

	if (Producer.IsEmpty())
	{
		return;
	}
	FSchematicNodeActivity& NodeActivity = Activity.FindOrAdd(Producer);
	NodeActivity.LastSeen = Now;
	NodeActivity.LastMessage = Decoded.IsEmpty() ? Entry.Raw : Decoded;
	++NodeActivity.FrameCount;

	Pulses.RemoveAll([Now](const FSchematicPulse& Pulse) { return Now - Pulse.Time > PulseDuration; });
	if (Pulses.Num() >= MaxPulses)
	{
		Pulses.RemoveAt(0);
	}
	FSchematicPulse& Pulse = Pulses.AddDefaulted_GetRef();
	Pulse.Time = Now;
	Pulse.From = Producer;
	Pulse.To = GetConsumers(MessageName, Producer);
}

float UIoTwinSchematicSubsystem::GetFramesPerSecond(double Now) const
{
	// Rate of the last finished window (>= 1 s); 0 once the bus has been quiet for 2 s.
	return (RateWindowStart > 0.0 && Now - RateWindowStart <= 2.0) ? LastRate : 0.0f;
}

void UIoTwinSchematicSubsystem::HandleFrameReceived(const FCanFrame& Frame)
{
	RecordFrame(Frame, /*bSentByThisNode=*/false, FPlatformTime::Seconds());
}

void UIoTwinSchematicSubsystem::HandleFrameSent(const FCanFrame& Frame)
{
	RecordFrame(Frame, /*bSentByThisNode=*/true, FPlatformTime::Seconds());
}
