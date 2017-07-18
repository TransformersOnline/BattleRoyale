// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#ifndef __BATTLEROYALELOADINGSCREEN_H__
#define __BATTLEROYALELOADINGSCREEN_H__

#include "ModuleInterface.h"


/** Module interface for this game's loading screens */
class IBattleRoyaleLoadingScreenModule : public IModuleInterface
{
public:
	/** Kicks off the loading screen for in game loading (not startup) */
	virtual void StartInGameLoadingScreen() = 0;
};

#endif // __BATTLEROYALELOADINGSCREEN_H__
