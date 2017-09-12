// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BattleRoyaleState.generated.h"

/** ranked PlayerState map, created from the GameState */
typedef TMap<int32, TWeakObjectPtr<AShooterPlayerState> > RankedPlayerMap; 

UENUM()
enum EGameStatusType
{
	EGameStatusType_None,			
	//EGameStatusType_Prepare,		// 出生点准备
	//EGameStatusType_BeforeThrow,	// 飞机起飞，开始投放前
	//EGameStatusType_Throwing,		// 投放
	//EGameStatusType_AfterThrow,		// 投放结束
	EGameStatusType_Fighting,		// 开始战斗
	EGameStatusType_Max
};

UCLASS()
class ABattleRoyaleState : public AGameState
{
	GENERATED_UCLASS_BODY()

public:

	/** number of teams in current game (doesn't deprecate when no players are left in a team) */
	UPROPERTY(Transient, Replicated)
	int32 NumTeams;

	/** accumulated score per team */
	UPROPERTY(Transient, Replicated)
	TArray<int32> TeamScores;

	/** time left for warmup / match */
	UPROPERTY(Transient, Replicated)
	int32 RemainingTime;

	/** is timer paused? */
	UPROPERTY(Transient, Replicated)
	bool bTimerPaused;

	UPROPERTY(Transient, Replicated)
	EGameStatusType CurGameStatus = EGameStatusType_None;

	UPROPERTY(Transient, ReplicatedUsing = OnRep_RoundCount)
	int32	RoundCount = 1;

	UPROPERTY(Transient, Replicated)
	int32   CurrentRoundRadius = 0;

	UPROPERTY(Transient, Replicated)
	FVector RoundCenter = FVector::ZeroVector;

	UPROPERTY()
	FVector	LastRoundCenterCache = FVector::ZeroVector;

	/** gets ranked PlayerState map for specific team */
	void GetRankedMap(int32 TeamIndex, RankedPlayerMap& OutRankedMap) const;	

	void RequestFinishAndExitToMainMenu();

	void SetMatchStatus(EGameStatusType GameStatus)
	{
		CurGameStatus = GameStatus;
	}

	EGameStatusType GetMatchStatus()
	{
		return CurGameStatus;
	}

	void SetRemainingTime(int32 TimeToSet)
	{
		RemainingTime = TimeToSet;
	}

	int32 GetRoundCount() { return RoundCount; }
	void  SetRoundCount(int32 Count);
	void  ResetRemainingTime();
	void  CacheLastRoundCenter();
	UFUNCTION()
	void  OnRep_RoundCount();
	UFUNCTION()
	void  OnRep_RoundCenter();
};
