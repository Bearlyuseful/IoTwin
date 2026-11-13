// IoTwin - consumer that is powered through its input port.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ElectricalPortComponent.h"
#include "ElectricalDeviceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPoweredChanged, bool, bPowered);

UCLASS(ClassGroup=(Electric), meta=(BlueprintSpawnableComponent))
class IOTWIN_API UElectricalDeviceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UElectricalDeviceComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power")
	UElectricalPortComponent* InputPort;

	/** Current supply state, updated every tick from the input port. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Power")
	bool bIsPowered = false;

	/** Fired when the device gets or loses power (and once at start). */
	UPROPERTY(BlueprintAssignable, Category="Power")
	FOnPoweredChanged OnPoweredChanged;

private:
	bool bInitialised = false;
};
