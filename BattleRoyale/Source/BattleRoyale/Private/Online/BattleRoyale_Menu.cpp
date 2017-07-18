// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "BattleRoyale.h"
#include "BattleRoyale_Menu.h"
#include "ShooterMainMenu.h"
#include "ShooterWelcomeMenu.h"
#include "ShooterMessageMenu.h"
#include "ShooterPlayerController_Menu.h"
#include "Online/BattleRoyaleSession.h"

ABattleRoyale_Menu::ABattleRoyale_Menu(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PlayerControllerClass = AShooterPlayerController_Menu::StaticClass();
}

void ABattleRoyale_Menu::RestartPlayer(class AController* NewPlayer)
{
	// don't restart
}

/** Returns game session class to use */
TSubclassOf<AGameSession> ABattleRoyale_Menu::GetGameSessionClass() const
{
	return ABattleRoyaleSession::StaticClass();
}
