// Copyright 2026 Jack McGuigan. All rights reserved.
using UnrealBuildTool;
using System.Collections.Generic;

public class SoulQuotaTarget : TargetRules
{
	public SoulQuotaTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "SoulQuota" });
	}
}
