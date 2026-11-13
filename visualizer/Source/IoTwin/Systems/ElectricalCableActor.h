// IoTwin - cable: two ports that conduct to each other, drawn as a hanging cable.

#pragma once

#include "CoreMinimal.h"
#include "ElectricalPortComponent.h"
#include "GameFramework/Actor.h"
#include "ElectricalCableActor.generated.h"

class UCableComponent;

UCLASS()
class IOTWIN_API AElectricalCableActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AElectricalCableActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cable")
	USceneComponent* Root;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable")
	UElectricalPortComponent* EndA;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable")
	UElectricalPortComponent* EndB;

	/** Visual cable from EndA to EndB. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cable")
	UCableComponent* CableMesh;

	/** Cable length relative to the distance of its ends (> 1 = sagging). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable", meta=(ClampMin=1.0))
	float Slack = 1.15f;

	/** Moves the cable ends to A and B and wires them (A - EndA ... EndB - B). */
	UFUNCTION(BlueprintCallable, Category="Cable")
	void ConnectBetween(UElectricalPortComponent* A, UElectricalPortComponent* B);

	/** Updates the visual cable after moving an end. */
	UFUNCTION(BlueprintCallable, Category="Cable")
	void RefreshCable();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
