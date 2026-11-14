// IoTwin - player controller: mouse interaction with the scene.

#include "Core/IoTwinPlayerController.h"

#include "Components/InputComponent.h"
#include "Core/GM_Simulation.h"
#include "EngineUtils.h"
#include "Bus/IoTwinBusSubsystem.h"
#include "Electrical/IoTwinElectricalSubsystem.h"
#include "Schematic/IoTwinSchematicSubsystem.h"
#include "Systems/LampActor.h"
#include "Systems/WallSwitchActor.h"

AIoTwinPlayerController::AIoTwinPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Hand;
	// The game mode sets a fixed camera; do not switch back to the pawn.
	bAutoManageActiveCameraTarget = false;
}

void AIoTwinPlayerController::BeginPlay()
{
	Super::BeginPlay();

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}

void AIoTwinPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AIoTwinPlayerController::HandleToggleKey);
		InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AIoTwinPlayerController::HandleViewKey);
		InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AIoTwinPlayerController::HandleDimKey);
		InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AIoTwinPlayerController::HandleDimKey);
		InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AIoTwinPlayerController::HandleFaultKey);
		InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AIoTwinPlayerController::HandleResetKey);
	}
}

void AIoTwinPlayerController::HandleToggleKey(FKey Key)
{
	for (TActorIterator<AWallSwitchActor> It(GetWorld()); It; ++It)
	{
		It->Toggle();
		break;
	}
}

void AIoTwinPlayerController::HandleViewKey(FKey Key)
{
	if (UIoTwinSchematicSubsystem* Schematic = UIoTwinSchematicSubsystem::Get(this))
	{
		Schematic->ToggleView();
	}
}

void AIoTwinPlayerController::HandleFaultKey(FKey Key)
{
	UIoTwinElectricalSubsystem* Electrical = UIoTwinElectricalSubsystem::Get(this);
	if (Electrical == nullptr)
	{
		return;
	}
	// The lamp the HUD shows: the one of the generated demo, else the first.
	ALampActor* Lamp = nullptr;
	for (TActorIterator<ALampActor> It(GetWorld()); It; ++It)
	{
		if (Lamp == nullptr || It->ActorHasTag(AGM_Simulation::DemoTag))
		{
			Lamp = *It;
		}
	}
	if (Lamp)
	{
		Electrical->ToggleShortCircuit(Lamp->Port);
	}
}

void AIoTwinPlayerController::HandleResetKey(FKey Key)
{
	if (UIoTwinElectricalSubsystem* Electrical = UIoTwinElectricalSubsystem::Get(this))
	{
		Electrical->ResetProtection();
	}
}

void AIoTwinPlayerController::HandleDimKey(FKey Key)
{
	UIoTwinBusSubsystem* Bus = UIoTwinBusSubsystem::Get(this);
	if (Bus == nullptr)
	{
		return;
	}
	// Start from the brightness the ECU reports in LAMP_CMD byte 1.
	int32 Level = 100;
	FCanFrame LampCmd;
	if (Bus->GetLastFrame(0x100, false, LampCmd) && LampCmd.Data.Num() >= 2)
	{
		Level = LampCmd.Data[1];
	}
	Level = FMath::Clamp(Level + (Key == EKeys::MouseScrollUp ? DimStep : -DimStep), 0, 100);
	Bus->SendFrame(FCanFrame::MakeData(0x201, {static_cast<uint8>(Level)}));
}
