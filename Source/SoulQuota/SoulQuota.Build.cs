// Copyright 2026 Jack McGuigan. All rights reserved.
using UnrealBuildTool;

public class SoulQuota : ModuleRules
{
	public SoulQuota(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"NetCore", "AIModule", "OnlineSubsystem", "OnlineSubsystemUtils"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "PhysicsCore", "ProceduralMeshComponent", "ApplicationCore", "Sockets" });
	}
}
