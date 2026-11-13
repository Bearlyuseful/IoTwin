// IoTwin - game mode of the visualizer.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GM_Simulation.generated.h"

class ACameraActor;
class UMaterialInterface;
class UStaticMesh;

/**
 * Game mode of the IoTwin visualizer. Optionally builds the light demo at
 * runtime (so it works in any map, no binary assets needed):
 *
 *   power source with CAN relay (0x100) --cable--> lamp,   wall switch (0x200)
 *
 * The scene is driven by can_sim through iotwin-bus.
 */
UCLASS()
class IOTWIN_API AGM_Simulation : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGM_Simulation();

	virtual void StartPlay() override;

	/** Tag of all actors created for the demo. */
	static const FName DemoTag;

	/** Build the light demo when play starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="IoTwin|Demo")
	bool bSpawnDemoScene = true;

	/** World location of the demo room. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="IoTwin|Demo")
	FVector DemoOrigin = FVector::ZeroVector;

	/** CAN id (hex) of LAMP_CMD that drives the relay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="IoTwin|Demo")
	FString RelayCanId = TEXT("100");

	/** CAN id (hex) of SWITCH_STATE sent by the wall switch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="IoTwin|Demo")
	FString SwitchCanId = TEXT("200");

protected:
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

private:
	void SpawnDemoScene();
	AActor* SpawnBlock(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color);
	void AddLabel(AActor* LabelOwner, const FString& Text, const FVector& WorldLocation, float Size = 12.0f);

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> DemoCamera;
};
