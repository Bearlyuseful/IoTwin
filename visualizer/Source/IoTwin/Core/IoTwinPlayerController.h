// IoTwin - player controller: mouse interaction with the scene.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "IoTwinPlayerController.generated.h"

/**
 * Shows the mouse cursor, enables clicking on actors (and HUD hit boxes),
 * toggles the first wall switch with SPACE, dims the lamp with the mouse wheel
 * (DIMMER_CMD 0x201) and switches 3D / schematic view with TAB. Electrical
 * simulation: F puts a short circuit on the lamp (or removes it), R resets
 * tripped breakers.
 */
UCLASS()
class IOTWIN_API AIoTwinPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AIoTwinPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void HandleToggleKey(FKey Key);
	void HandleViewKey(FKey Key);
	void HandleDimKey(FKey Key);
	void HandleFaultKey(FKey Key);
	void HandleResetKey(FKey Key);

	/** Brightness step per wheel notch in percent. */
	static constexpr int32 DimStep = 10;
};
