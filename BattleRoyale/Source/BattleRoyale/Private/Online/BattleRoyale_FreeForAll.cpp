// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "BattleRoyale.h"
#include "BattleRoyale_FreeForAll.h"
#include "ShooterPlayerState.h"

ABattleRoyale_FreeForAll::ABattleRoyale_FreeForAll(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bDelayedStart = true;
}

void ABattleRoyale_FreeForAll::DetermineMatchWinner()
{
	ABattleRoyaleState const* const MyGameState = CastChecked<ABattleRoyaleState>(GameState);
	float BestScore = MAX_FLT;
	int32 BestPlayer = -1;
	int32 NumBestPlayers = 0;

	for (int32 i = 0; i < MyGameState->PlayerArray.Num(); i++)
	{
		const float PlayerScore = MyGameState->PlayerArray[i]->Score;
		if (BestScore < PlayerScore)
		{
			BestScore = PlayerScore;
			BestPlayer = i;
			NumBestPlayers = 1;
		}
		else if (BestScore == PlayerScore)
		{
			NumBestPlayers++;
		}
	}

	WinnerPlayerState = (NumBestPlayers == 1) ? Cast<AShooterPlayerState>(MyGameState->PlayerArray[BestPlayer]) : NULL;
}

bool ABattleRoyale_FreeForAll::IsWinner(AShooterPlayerState* PlayerState) const
{
	return PlayerState && !PlayerState->IsQuitter() && PlayerState == WinnerPlayerState;
}
