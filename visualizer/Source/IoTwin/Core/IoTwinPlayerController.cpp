// IoTwin - player controller: mouse interaction with the scene.

#include "Core/IoTwinPlayerController.h"

#include "Components/InputComponent.h"
#include "EngineUtils.h"
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
