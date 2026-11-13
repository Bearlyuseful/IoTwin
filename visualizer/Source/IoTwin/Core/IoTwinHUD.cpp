// IoTwin - on-screen status of bus and devices.

#include "Core/IoTwinHUD.h"

#include "Bus/IoTwinBusSubsystem.h"
#include "Core/GM_Simulation.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Schematic/IoTwinSchematicSubsystem.h"
#include "Schematic/SchematicRenderer.h"
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
	const FName SchematicButton(TEXT("Hud.Schematic"));

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

	const UIoTwinSchematicSubsystem* Schematic = UIoTwinSchematicSubsystem::Get(this);
	if (Schematic && Schematic->IsSchematicVisible())
	{
		TArray<FSchematicHitBox> HitBoxes;
		FSchematicRenderer(Canvas, *Schematic, UIoTwinBusSubsystem::Get(this), FPlatformTime::Seconds()).Draw(HitBoxes);
		for (const FSchematicHitBox& HitBox : HitBoxes)
		{
			AddHitBox(HitBox.Position, HitBox.Size, HitBox.Name, /*bConsumesInput=*/true, HitBox.Priority);
		}
		return;
	}
	DrawSimulationPanel();
}

void AIoTwinHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);

	UIoTwinSchematicSubsystem* Schematic = UIoTwinSchematicSubsystem::Get(this);
	if (Schematic == nullptr)
	{
		return;
	}
	if (BoxName == SchematicButton)
	{
		Schematic->SetSchematicVisible(true);
	}
	else if (BoxName == FSchematicRenderer::View3DBox)
	{
		Schematic->SetSchematicVisible(false);
	}
	else if (BoxName == FSchematicRenderer::BackgroundBox)
	{
		Schematic->SelectedNode.Reset();
	}
	else
	{
		FString Name = BoxName.ToString();
		if (Name.RemoveFromStart(FSchematicRenderer::NodeBoxPrefix))
		{
			Schematic->SelectedNode = Name;
		}
	}
}

void AIoTwinHUD::DrawSimulationPanel()
{
	const UIoTwinBusSubsystem* Bus = UIoTwinBusSubsystem::Get(this);
	APowerSourceActor* Source = nullptr;
	ALampActor* Lamp = nullptr;
	AWallSwitchActor* Switch = nullptr;
	Source = FindDisplayedActor<APowerSourceActor>(GetWorld());
	Lamp = FindDisplayedActor<ALampActor>(GetWorld());
	Switch = FindDisplayedActor<AWallSwitchActor>(GetWorld());

	const int32 TrafficLines = Bus ? Bus->GetRecentTraffic().Num() : 0;
	// LAMP_STATUS (0x101) of the light ECU: current, supply voltage, duty, flags.
	FCanFrame Status;
	const bool bHaveStatus = Bus && Bus->GetLastFrame(0x101, false, Status) && Status.Data.Num() >= 6;
	const float PanelHeight = LineHeight * (9.5f + (bHaveStatus ? 1.0f : 0.0f) + TrafficLines) + 24.0f;
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
		DrawLine(Lamp->IsOn() ? FString::Printf(TEXT("Lamp:  ON  %d %%"), FMath::RoundToInt(Lamp->Device->PowerLevel * 100.0f))
		                      : FString(TEXT("Lamp:  OFF")),
			Lamp->IsOn() ? GoodColor : DimColor, Y);
	}
	if (Switch)
	{
		DrawLine(FString::Printf(TEXT("Switch: %s"), Switch->IsOn() ? TEXT("ON") : TEXT("OFF")), TextColor, Y);
	}
	if (bHaveStatus)
	{
		const int32 CurrentMa = Status.Data[0] | (Status.Data[1] << 8);
		const int32 SupplyMv = Status.Data[2] | (Status.Data[3] << 8);
		const uint8 Flags = Status.Data[5];
		FString Power = FString::Printf(TEXT("ECU output: %.2f A  %.2f V"), CurrentMa / 1000.0f, SupplyMv / 1000.0f);
		if (Flags & 0x01)
		{
			Power += TEXT("  FUSE BLOWN");
		}
		else if (Flags & 0x04)
		{
			Power += TEXT("  SHORT");
		}
		else if (Flags & 0x02)
		{
			Power += TEXT("  OVERCURRENT");
		}
		DrawLine(Power, (Flags != 0) ? BadColor : TextColor, Y);
	}
	DrawLine(TEXT("Click the wall switch or press SPACE"), DimColor, Y);
	DrawLine(TEXT("Mouse wheel dims the lamp"), DimColor, Y);
	if (UIoTwinSchematicSubsystem::Get(this))
	{
		AddHitBox(FVector2D(Left, Y), FVector2D(255.0f, LineHeight), SchematicButton, /*bConsumesInput=*/true, 10);
		DrawLine(TEXT("[Tab] schematic view"), FLinearColor(0.45f, 0.7f, 1.0f), Y);
	}

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
