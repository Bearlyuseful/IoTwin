// Copyright Epic Games, Inc. All Rights Reserved.

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
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "CableComponent" });
	}
}
