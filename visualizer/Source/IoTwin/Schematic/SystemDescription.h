// IoTwin - system description (data/systems/*.json): nodes, buses, links, messages.

#pragma once

#include "CoreMinimal.h"

struct IOTWIN_API FSystemNode
{
	FString Id;
	FString Kind; // physical | virtual
	FString Label;
	FString Component;
	FString Program;
	FString Description;
};

struct IOTWIN_API FSystemBus
{
	FString Id;
	FString Type;
	int32 Bitrate = 0;
	FString Dbc; // relative to the system file
	FString Transport;
	TArray<FString> Members;
};

struct IOTWIN_API FSystemLink
{
	FString Id;
	FString Type;
	FString From; // node or node:port
	FString To;
	FString Carries;

	static FString NodeOf(const FString& Endpoint);
};

struct IOTWIN_API FSystemMessage
{
	FString Name;
	TArray<FString> Producers;
	TArray<FString> Consumers;
};

class IOTWIN_API FSystemDescription
{
public:
	bool LoadFromFile(const FString& Path, FString& OutError);
	bool LoadFromString(const FString& Json, const FString& BaseDirectory, FString& OutError);

	const FSystemNode* FindNode(const FString& NodeId) const;
	const FSystemMessage* FindMessage(const FString& Name) const;

	/** Absolute path of a file referenced relative to the system file. */
	FString ResolvePath(const FString& Relative) const;

	FString Id;
	FString Name;
	FString Description;
	TArray<FSystemNode> Nodes;
	TArray<FSystemBus> Buses;
	TArray<FSystemLink> Links;
	TArray<FSystemMessage> Messages;
	FString BaseDirectory;
};
