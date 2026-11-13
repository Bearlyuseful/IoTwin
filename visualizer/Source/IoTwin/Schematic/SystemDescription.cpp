// IoTwin - system description (data/systems/*.json): nodes, buses, links, messages.

#include "Schematic/SystemDescription.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}

	TArray<FString> StringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Values;
		Object->TryGetStringArrayField(Field, Values);
		return Values;
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		Object->TryGetArrayField(Field, Array);
		return Array;
	}
}

FString FSystemLink::NodeOf(const FString& Endpoint)
{
	FString Node;
	return Endpoint.Split(TEXT(":"), &Node, nullptr) ? Node : Endpoint;
}

bool FSystemDescription::LoadFromFile(const FString& Path, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}
	return LoadFromString(Json, FPaths::GetPath(Path), OutError);
}

bool FSystemDescription::LoadFromString(const FString& Json, const FString& InBaseDirectory, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("invalid JSON");
		return false;
	}

	BaseDirectory = InBaseDirectory;
	Id = StringField(Root, TEXT("id"));
	Name = StringField(Root, TEXT("name"));
	Description = StringField(Root, TEXT("description"));
	Nodes.Reset();
	Buses.Reset();
	Links.Reset();
	Messages.Reset();

	if (const auto* Array = ArrayField(Root, TEXT("nodes")))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FSystemNode& Node = Nodes.AddDefaulted_GetRef();
			Node.Id = StringField(Object, TEXT("id"));
			Node.Kind = StringField(Object, TEXT("kind"));
			Node.Label = StringField(Object, TEXT("label"));
			Node.Component = StringField(Object, TEXT("component"));
			Node.Program = StringField(Object, TEXT("program"));
			Node.Description = StringField(Object, TEXT("description"));
		}
	}
	if (const auto* Array = ArrayField(Root, TEXT("buses")))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FSystemBus& Bus = Buses.AddDefaulted_GetRef();
			Bus.Id = StringField(Object, TEXT("id"));
			Bus.Type = StringField(Object, TEXT("type"));
			Object->TryGetNumberField(TEXT("bitrate"), Bus.Bitrate);
			Bus.Dbc = StringField(Object, TEXT("dbc"));
			Bus.Transport = StringField(Object, TEXT("transport"));
			Bus.Members = StringArray(Object, TEXT("members"));
		}
	}
	if (const auto* Array = ArrayField(Root, TEXT("links")))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FSystemLink& Link = Links.AddDefaulted_GetRef();
			Link.Id = StringField(Object, TEXT("id"));
			Link.Type = StringField(Object, TEXT("type"));
			Link.From = StringField(Object, TEXT("from"));
			Link.To = StringField(Object, TEXT("to"));
			Link.Carries = StringField(Object, TEXT("carries"));
		}
	}
	if (const auto* Array = ArrayField(Root, TEXT("messages")))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FSystemMessage& Message = Messages.AddDefaulted_GetRef();
			Message.Name = StringField(Object, TEXT("name"));
			Message.Producers = StringArray(Object, TEXT("producers"));
			Message.Consumers = StringArray(Object, TEXT("consumers"));
		}
	}

	if (Nodes.Num() == 0)
	{
		OutError = TEXT("system has no nodes");
		return false;
	}
	return true;
}

const FSystemNode* FSystemDescription::FindNode(const FString& NodeId) const
{
	return Nodes.FindByPredicate([&NodeId](const FSystemNode& Node) { return Node.Id == NodeId; });
}

const FSystemMessage* FSystemDescription::FindMessage(const FString& MessageName) const
{
	return Messages.FindByPredicate([&MessageName](const FSystemMessage& M) { return M.Name == MessageName; });
}

FString FSystemDescription::ResolvePath(const FString& Relative) const
{
	FString Path = FPaths::Combine(BaseDirectory, Relative);
	FPaths::CollapseRelativeDirectories(Path);
	return Path;
}
