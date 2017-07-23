// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BattleRoyale : ModuleRules
{
	public BattleRoyale(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] { 
				"BattleRoyale/Classes/Player",
				"BattleRoyale/Private",
				"BattleRoyale/Private/UI",
				"BattleRoyale/Private/UI/Menu",
				"BattleRoyale/Private/UI/Style",
				"BattleRoyale/Private/UI/Widgets",
            }
		);

        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"OnlineSubsystem",
				"OnlineSubsystemUtils",
				"AssetRegistry",
                "AIModule",
				"GameplayTasks",
                "Landscape",
                "PhysXVehicles"
            }
		);

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"InputCore",
				"Slate",
				"SlateCore",
				"BattleRoyaleLoadingScreen",
				"Json"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"OnlineSubsystemNull",
				"NetworkReplayStreaming",
				"NullNetworkReplayStreaming",
				"HttpNetworkReplayStreaming"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"NetworkReplayStreaming"
			}
		);
	}
}
