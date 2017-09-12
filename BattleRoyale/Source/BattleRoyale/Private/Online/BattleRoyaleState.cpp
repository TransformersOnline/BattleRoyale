// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "BattleRoyale.h"
#include "Online/ShooterPlayerState.h"
#include "BattleRoyaleInstance.h"

ABattleRoyaleState::ABattleRoyaleState(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NumTeams = 0;
	RemainingTime = 0;
	bTimerPaused = false;
}

void ABattleRoyaleState::GetLifetimeReplicatedProps( TArray< FLifetimeProperty > & OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );

	DOREPLIFETIME( ABattleRoyaleState, NumTeams );
	DOREPLIFETIME( ABattleRoyaleState, RemainingTime );
	DOREPLIFETIME( ABattleRoyaleState, bTimerPaused );
	DOREPLIFETIME( ABattleRoyaleState, TeamScores );
	DOREPLIFETIME( ABattleRoyaleState, CurGameStatus );
	DOREPLIFETIME(ABattleRoyaleState, RoundCount);
	DOREPLIFETIME(ABattleRoyaleState, CurrentRoundRadius);
	DOREPLIFETIME(ABattleRoyaleState, RoundCenter);
}

void ABattleRoyaleState::GetRankedMap(int32 TeamIndex, RankedPlayerMap& OutRankedMap) const
{
	OutRankedMap.Empty();

	//first, we need to go over all the PlayerStates, grab their score, and rank them
	TMultiMap<int32, AShooterPlayerState*> SortedMap;
	for(int32 i = 0; i < PlayerArray.Num(); ++i)
	{
		int32 Score = 0;
		AShooterPlayerState* CurPlayerState = Cast<AShooterPlayerState>(PlayerArray[i]);
		if (CurPlayerState && (CurPlayerState->GetTeamNum() == TeamIndex))
		{
			SortedMap.Add(FMath::TruncToInt(CurPlayerState->Score), CurPlayerState);
		}
	}

	//sort by the keys
	SortedMap.KeySort(TGreater<int32>());

	//now, add them back to the ranked map
	OutRankedMap.Empty();

	int32 Rank = 0;
	for(TMultiMap<int32, AShooterPlayerState*>::TIterator It(SortedMap); It; ++It)
	{
		OutRankedMap.Add(Rank++, It.Value());
	}
	
}


void ABattleRoyaleState::RequestFinishAndExitToMainMenu()
{
	if (AuthorityGameMode)
	{
		// we are server, tell the gamemode
		ABattleRoyaleMode* const GameMode = Cast<ABattleRoyaleMode>(AuthorityGameMode);
		if (GameMode)
		{
			GameMode->RequestFinishAndExitToMainMenu();
		}
	}
	else
	{
		// we are client, handle our own business
		UBattleRoyaleInstance* GameInstance = Cast<UBattleRoyaleInstance>(GetGameInstance());
		if (GameInstance)
		{
			GameInstance->RemoveSplitScreenPlayers();
		}

		AShooterPlayerController* const PrimaryPC = Cast<AShooterPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
		if (PrimaryPC)
		{
			check(PrimaryPC->GetNetMode() == ENetMode::NM_Client);
			PrimaryPC->HandleReturnToMainMenu();
		}
	}

}

void  ABattleRoyaleState::SetRoundCount(int32 Count)
{
	RoundCount = Count;

	CacheLastRoundCenter();

	ResetRemainingTime();
}

void  ABattleRoyaleState::ResetRemainingTime()
{
	

	RemainingTime = 300 - FMath::Min((RoundCount - 1) * 30, 240);
}

void  ABattleRoyaleState::CacheLastRoundCenter()
{
	if (RoundCount > 1)
	{
		LastRoundCenterCache = RoundCenter;
	}
	else
	{
		LastRoundCenterCache = FVector::ZeroVector;
	}
}

void  ABattleRoyaleState::OnRep_RoundCount()
{
	CacheLastRoundCenter();
}

void  ABattleRoyaleState::OnRep_RoundCenter()
{
	;
}