// IoTwin - player controller: mouse interaction with the scene.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "IoTwinPlayerController.generated.h"

/** Shows the mouse cursor, enables clicking on actors and toggles the first wall switch with SPACE. */
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
};
