// Copyright 2026 Jack McGuigan. All rights reserved.
using UnrealBuildTool;
using System.Collections.Generic;

public class SoulQuotaEditorTarget : TargetRules
{
	public SoulQuotaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "SoulQuota" });
	}
}
