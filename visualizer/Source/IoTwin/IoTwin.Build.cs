// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class IoTwin : ModuleRules
{
	public IoTwin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Allows includes relative to the module root, e.g. "Bus/CanFrame.h".
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// Sockets/Networking: TCP connection to iotwin-bus. CableComponent: visual cables.
		// Json: system descriptions (data/systems) for the schematic view.
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "CableComponent", "Json" });

		// Electrical simulation core (C, libs/elsim) compiled into this module: the
		// in-process adapter of ADR 0008. Electrical/Core/*.c include the sources.
		PrivateIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "libs", "elsim")));
		CStandard = CStandardVersion.C17;
	}
}
