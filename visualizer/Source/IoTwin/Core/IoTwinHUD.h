// IoTwin - on-screen status of bus and devices.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "IoTwinHUD.generated.h"

/**
 * 3D view: draws bus connection, device states and recent CAN traffic.
 * Schematic view: full-screen 2D schematic (FSchematicRenderer).
 */
UCLASS()
class IOTWIN_API AIoTwinHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

private:
	void DrawSimulationPanel();
	void DrawLine(const FString& Text, const FLinearColor& Color, float& Y, float Scale = 1.0f);
};
