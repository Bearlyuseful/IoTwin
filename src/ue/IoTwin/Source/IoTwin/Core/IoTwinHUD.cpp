// IoTwin - on-screen status of bus and devices.

#include "Core/IoTwinHUD.h"

#include "Bus/IoTwinBusSubsystem.h"
#include "Core/GM_Simulation.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Systems/LampActor.h"
#include "Systems/PowerSourceActor.h"
#include "Systems/WallSwitchActor.h"

namespace
{
	const FLinearColor TextColor(0.9f, 0.9f, 0.9f);
	const FLinearColor DimColor(0.6f, 0.6f, 0.6f);
	const FLinearColor GoodColor(0.3f, 0.95f, 0.4f);
	const FLinearColor BadColor(1.0f, 0.4f, 0.3f);
	constexpr float Left = 24.0f;
	constexpr float LineHeight = 22.0f;

	/** Prefers actors of the generated demo, falls back to the first one in the level. */
	template <typename T>
	T* FindDisplayedActor(UWorld* World)
	{
		T* First = nullptr;
		for (TActorIterator<T> It(World); It; ++It)
		{
			if (It->ActorHasTag(AGM_Simulation::DemoTag))
			{
				return *It;
			}
			First = First ? First : *It;
		}
		return First;
	}
}

void AIoTwinHUD::DrawLine(const FString& Text, const FLinearColor& Color, float& Y, float Scale)
{
	DrawText(Text, Color, Left + 12.0f, Y, GEngine ? GEngine->GetMediumFont() : nullptr, Scale);
	Y += LineHeight * Scale;
}

void AIoTwinHUD::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas == nullptr)
	{
		return;
	}

	const UIoTwinBusSubsystem* Bus = UIoTwinBusSubsystem::Get(this);
	APowerSourceActor* Source = nullptr;
	ALampActor* Lamp = nullptr;
	AWallSwitchActor* Switch = nullptr;
	Source = FindDisplayedActor<APowerSourceActor>(GetWorld());
	Lamp = FindDisplayedActor<ALampActor>(GetWorld());
	Switch = FindDisplayedActor<AWallSwitchActor>(GetWorld());

	const int32 TrafficLines = Bus ? Bus->GetRecentTraffic().Num() : 0;
	const float PanelHeight = LineHeight * (7.5f + TrafficLines) + 24.0f;
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f), Left, 20.0f, 255.0f, PanelHeight);

	float Y = 30.0f;
	DrawLine(TEXT("IoTwin  -  Light Demo"), TextColor, Y, 1.2f);

	if (Bus == nullptr || !Bus->bEnabled)
	{
		DrawLine(TEXT("Bus: disabled"), DimColor, Y);
	}
	else if (Bus->IsConnected())
	{
		DrawLine(FString::Printf(TEXT("Bus: connected to %s"), *Bus->GetEndpoint()), GoodColor, Y);
	}
	else
	{
		DrawLine(FString::Printf(TEXT("Bus: waiting for iotwin-bus at %s"), *Bus->GetEndpoint()), BadColor, Y);
	}

	if (Source)
	{
		DrawLine(FString::Printf(TEXT("Relay: %s"), Source->IsOutputEnabled() ? TEXT("closed") : TEXT("open")),
			Source->IsOutputEnabled() ? GoodColor : DimColor, Y);
	}
	if (Lamp)
	{
		DrawLine(FString::Printf(TEXT("Lamp:  %s"), Lamp->IsOn() ? TEXT("ON") : TEXT("OFF")),
			Lamp->IsOn() ? GoodColor : DimColor, Y);
	}
	if (Switch)
	{
		DrawLine(FString::Printf(TEXT("Switch: %s"), Switch->IsOn() ? TEXT("ON") : TEXT("OFF")), TextColor, Y);
	}
	DrawLine(TEXT("Click the wall switch or press SPACE"), DimColor, Y);

	Y += LineHeight * 0.5f;
	DrawLine(TEXT("Recent bus traffic:"), DimColor, Y);
	if (Bus)
	{
		for (const FString& Entry : Bus->GetRecentTraffic())
		{
			DrawLine(Entry, TextColor, Y);
		}
	}
}
