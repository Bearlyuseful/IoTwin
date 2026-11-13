// IoTwin - on-screen status of bus and devices.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "IoTwinHUD.generated.h"

/** Draws bus connection, device states and recent CAN traffic. */
UCLASS()
class IOTWIN_API AIoTwinHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawLine(const FString& Text, const FLinearColor& Color, float& Y, float Scale = 1.0f);
};
