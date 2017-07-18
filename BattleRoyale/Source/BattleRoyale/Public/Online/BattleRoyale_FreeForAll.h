// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BattleRoyale_FreeForAll.generated.h"

class AShooterPlayerState;

UCLASS()
class ABattleRoyale_FreeForAll : public ABattleRoyaleMode
{
	GENERATED_UCLASS_BODY()

protected:

	/** best player */
	UPROPERTY(transient)
	AShooterPlayerState* WinnerPlayerState;

	/** check who won */
	virtual void DetermineMatchWinner() override;

	/** check if PlayerState is a winner */
	virtual bool IsWinner(AShooterPlayerState* PlayerState) const override;
};
