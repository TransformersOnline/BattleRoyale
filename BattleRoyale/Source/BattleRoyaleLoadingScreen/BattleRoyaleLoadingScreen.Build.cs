// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// This module must be loaded "PreLoadingScreen" in the .uproject file, otherwise it will not hook in time!

public class BattleRoyaleLoadingScreen : ModuleRules
{
    public BattleRoyaleLoadingScreen(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("../../BattleRoyale/Source/BattleRoyaleLoadingScreen/Private");

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"MoviePlayer",
				"Slate",
				"SlateCore",
				"InputCore"
			}
		);
	}
}
