// IoTwin - interface of scene objects that take part in the electrical simulation.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ElectricalElement.generated.h"

class FElectricalModelBuilder;
class UIoTwinElectricalSubsystem;

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UElectricalElement : public UInterface
{
	GENERATED_BODY()
};

/**
 * An actor or component that contributes devices and conductors to the
 * electrical simulation (ADR 0008). Unreal owns the topology, the simulator
 * computes every electrical value:
 *
 *  - DescribeElectrical() adds the element to the model when the subsystem
 *    (re)builds it, e.g. a load connected to its port, or a cable between its ends.
 *  - ApplyElectrical() is called every frame with the newest results.
 *
 * Ports (UElectricalPortComponent) become junctions with the terminals L, N
 * and PE automatically; wires between ports become short links unless an
 * element describes them (ClaimWire).
 */
class IOTWIN_API IElectricalElement
{
	GENERATED_BODY()

public:
	virtual void DescribeElectrical(FElectricalModelBuilder& Builder) = 0;
	virtual void ApplyElectrical(const UIoTwinElectricalSubsystem& Simulation) {}
};
