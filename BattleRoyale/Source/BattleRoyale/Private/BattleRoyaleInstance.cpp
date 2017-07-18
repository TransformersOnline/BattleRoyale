// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BattleRoyaleInstance.cpp
=============================================================================*/

#include "BattleRoyale.h"
#include "BattleRoyaleInstance.h"
#include "ShooterMainMenu.h"
#include "ShooterWelcomeMenu.h"
#include "ShooterMessageMenu.h"
#include "BattleRoyaleLoadingScreen.h"
#include "OnlineKeyValuePair.h"
#include "ShooterStyle.h"
#include "ShooterMenuItemWidgetStyle.h"
#include "BattleRoyaleViewportClient.h"
#include "Player/ShooterPlayerController_Menu.h"
#include "Online/ShooterPlayerState.h"
#include "Online/BattleRoyaleSession.h"
#include "Online/ShooterOnlineSessionClient.h"


void SShooterWaitDialog::Construct(const FArguments& InArgs)
{
	const FShooterMenuItemStyle* ItemStyle = &FShooterStyle::Get().GetWidgetStyle<FShooterMenuItemStyle>("DefaultShooterMenuItemStyle");
	const FButtonStyle* ButtonStyle = &FShooterStyle::Get().GetWidgetStyle<FButtonStyle>("DefaultShooterButtonStyle");
	ChildSlot
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(20.0f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SBorder)
				.Padding(50.0f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.BorderImage(&ItemStyle->BackgroundBrush)
				.BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f))
				[
					SNew(STextBlock)
					.TextStyle(FShooterStyle::Get(), "BattleRoyale.MenuHeaderTextStyle")
					.ColorAndOpacity(this, &SShooterWaitDialog::GetTextColor)
					.Text(InArgs._MessageText)
					.WrapTextAt(500.0f)
				]
			]
		];

	//Setup a curve
	const float StartDelay = 0.0f;
	const float SecondDelay = 0.0f;
	const float AnimDuration = 2.0f;

	WidgetAnimation = FCurveSequence();
	TextColorCurve = WidgetAnimation.AddCurve(StartDelay + SecondDelay, AnimDuration, ECurveEaseFunction::QuadInOut);
	WidgetAnimation.Play(this->AsShared(), true);
}

FSlateColor SShooterWaitDialog::GetTextColor() const
{
	//instead of going from black -> white, go from white -> grey.
	float fAlpha = 1.0f - TextColorCurve.GetLerp();
	fAlpha = fAlpha * 0.5f + 0.5f;
	return FLinearColor(FColor(155, 164, 182, FMath::Clamp((int32)(fAlpha * 255.0f), 0, 255)));
}

namespace BattleRoyaleInstanceState
{
	const FName None = FName(TEXT("None"));
	const FName PendingInvite = FName(TEXT("PendingInvite"));
	const FName WelcomeScreen = FName(TEXT("WelcomeScreen"));
	const FName MainMenu = FName(TEXT("MainMenu"));
	const FName MessageMenu = FName(TEXT("MessageMenu"));
	const FName Playing = FName(TEXT("Playing"));
}


UBattleRoyaleInstance::UBattleRoyaleInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsOnline(true) // Default to online
	, bIsLicensed(true) // Default to licensed (should have been checked by OS on boot)
{
	CurrentState = BattleRoyaleInstanceState::None;
}

void UBattleRoyaleInstance::Init() 
{
	Super::Init();

	IgnorePairingChangeForControllerId = -1;
	CurrentConnectionStatus = EOnlineServerConnectionStatus::Connected;

	LocalPlayerOnlineStatus.InsertDefaulted(0, MAX_LOCAL_PLAYERS);

	// game requires the ability to ID users.
	const auto OnlineSub = IOnlineSubsystem::Get();
	check(OnlineSub);
	const auto IdentityInterface = OnlineSub->GetIdentityInterface();
	check(IdentityInterface.IsValid());

	const auto SessionInterface = OnlineSub->GetSessionInterface();
	check(SessionInterface.IsValid());

	// bind any OSS delegates we needs to handle
	for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i)
	{
		IdentityInterface->AddOnLoginStatusChangedDelegate_Handle(i, FOnLoginStatusChangedDelegate::CreateUObject(this, &UBattleRoyaleInstance::HandleUserLoginChanged));
	}

	IdentityInterface->AddOnControllerPairingChangedDelegate_Handle(FOnControllerPairingChangedDelegate::CreateUObject(this, &UBattleRoyaleInstance::HandleControllerPairingChanged));

	FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &UBattleRoyaleInstance::HandleAppWillDeactivate);

	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UBattleRoyaleInstance::HandleAppSuspend);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &UBattleRoyaleInstance::HandleAppResume);

	FCoreDelegates::OnSafeFrameChangedEvent.AddUObject(this, &UBattleRoyaleInstance::HandleSafeFrameChanged);
	FCoreDelegates::OnControllerConnectionChange.AddUObject(this, &UBattleRoyaleInstance::HandleControllerConnectionChange);
	FCoreDelegates::ApplicationLicenseChange.AddUObject(this, &UBattleRoyaleInstance::HandleAppLicenseUpdate);

	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UBattleRoyaleInstance::OnPreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UBattleRoyaleInstance::OnPostLoadMap);

	FCoreUObjectDelegates::PostDemoPlay.AddUObject(this, &UBattleRoyaleInstance::OnPostDemoPlay);

	bPendingEnableSplitscreen = false;

	OnlineSub->AddOnConnectionStatusChangedDelegate_Handle( FOnConnectionStatusChangedDelegate::CreateUObject( this, &UBattleRoyaleInstance::HandleNetworkConnectionStatusChanged ) );

	SessionInterface->AddOnSessionFailureDelegate_Handle( FOnSessionFailureDelegate::CreateUObject( this, &UBattleRoyaleInstance::HandleSessionFailure ) );
	
	OnEndSessionCompleteDelegate = FOnEndSessionCompleteDelegate::CreateUObject(this, &UBattleRoyaleInstance::OnEndSessionComplete);

	// Register delegate for ticker callback
	TickDelegate = FTickerDelegate::CreateUObject(this, &UBattleRoyaleInstance::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
}

void UBattleRoyaleInstance::Shutdown()
{
	Super::Shutdown();

	// Unregister ticker delegate
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
}

void UBattleRoyaleInstance::HandleNetworkConnectionStatusChanged(  EOnlineServerConnectionStatus::Type LastConnectionStatus, EOnlineServerConnectionStatus::Type ConnectionStatus )
{
	UE_LOG( LogOnlineGame, Warning, TEXT( "UBattleRoyaleInstance::HandleNetworkConnectionStatusChanged: %s" ), EOnlineServerConnectionStatus::ToString( ConnectionStatus ) );

#if SHOOTER_CONSOLE_UI
	// If we are disconnected from server, and not currently at (or heading to) the welcome screen
	// then display a message on consoles
	if (	bIsOnline && 
			PendingState != BattleRoyaleInstanceState::WelcomeScreen &&
			CurrentState != BattleRoyaleInstanceState::WelcomeScreen && 
			ConnectionStatus != EOnlineServerConnectionStatus::Connected )
	{
		UE_LOG( LogOnlineGame, Log, TEXT( "UBattleRoyaleInstance::HandleNetworkConnectionStatusChanged: Going to main menu" ) );

		// Display message on consoles
#if PLATFORM_XBOXONE
		const FText ReturnReason	= NSLOCTEXT( "NetworkFailures", "ServiceUnavailable", "Connection to Xbox LIVE has been lost." );
#elif PLATFORM_PS4
		const FText ReturnReason	= NSLOCTEXT( "NetworkFailures", "ServiceUnavailable", "Connection to \"PSN\" has been lost." );
#else
		const FText ReturnReason	= NSLOCTEXT( "NetworkFailures", "ServiceUnavailable", "Connection has been lost." );
#endif
		const FText OKButton		= NSLOCTEXT( "DialogButtons", "OKAY", "OK" );
		
		ShowMessageThenGotoState( ReturnReason, OKButton, FText::GetEmpty(), BattleRoyaleInstanceState::MainMenu );
	}

	CurrentConnectionStatus = ConnectionStatus;
#endif
}

void UBattleRoyaleInstance::HandleSessionFailure( const FUniqueNetId& NetId, ESessionFailure::Type FailureType )
{
	UE_LOG( LogOnlineGame, Warning, TEXT( "UBattleRoyaleInstance::HandleSessionFailure: %u" ), (uint32)FailureType );

#if SHOOTER_CONSOLE_UI
	// If we are not currently at (or heading to) the welcome screen then display a message on consoles
	if (	bIsOnline && 
			PendingState != BattleRoyaleInstanceState::WelcomeScreen &&
			CurrentState != BattleRoyaleInstanceState::WelcomeScreen )
	{
		UE_LOG( LogOnlineGame, Log, TEXT( "UBattleRoyaleInstance::HandleSessionFailure: Going to main menu" ) );

		// Display message on consoles
#if PLATFORM_XBOXONE
		const FText ReturnReason	= NSLOCTEXT( "NetworkFailures", "ServiceUnavailable", "Connection to Xbox LIVE has been lost." );
#elif PLATFORM_PS4
		const FText ReturnReason	= NSLOCTEXT( "NetworkFailures", "ServiceUnavailable", "Connection to PSN has been lost." );
#else
		const FText ReturnReason	= NSLOCTEXT( "NetworkFailures", "ServiceUnavailable", "Connection has been lost." );
#endif
		const FText OKButton		= NSLOCTEXT( "DialogButtons", "OKAY", "OK" );
		
		ShowMessageThenGotoState( ReturnReason, OKButton,  FText::GetEmpty(), BattleRoyaleInstanceState::MainMenu );
	}
#endif
}

void UBattleRoyaleInstance::OnPreLoadMap(const FString& MapName)
{
	if ( bPendingEnableSplitscreen )
	{
		// Allow splitscreen
		GetGameViewportClient()->SetDisableSplitscreenOverride( false );

		bPendingEnableSplitscreen = false;
	}
}

void UBattleRoyaleInstance::OnPostLoadMap(UWorld*)
{
	// Make sure we hide the loading screen when the level is done loading
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

	if ( ShooterViewport != NULL )
	{
		ShooterViewport->HideLoadingScreen();
	}
}

void UBattleRoyaleInstance::OnUserCanPlayInvite(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, uint32 PrivilegeResults)
{
	CleanupOnlinePrivilegeTask();
	if (WelcomeMenuUI.IsValid())
	{
		WelcomeMenuUI->LockControls(false);
	}

	if (PrivilegeResults == (uint32)IOnlineIdentity::EPrivilegeResults::NoFailures)	
	{
		if (UserId == *PendingInvite.UserId)
		{
			PendingInvite.bPrivilegesCheckedAndAllowed = true;
		}		
	}
	else
	{
		DisplayOnlinePrivilegeFailureDialogs(UserId, Privilege, PrivilegeResults);
		GotoState(BattleRoyaleInstanceState::WelcomeScreen);
	}
}

void UBattleRoyaleInstance::OnUserCanPlayTogether(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, uint32 PrivilegeResults)
{
	CleanupOnlinePrivilegeTask();
	if (WelcomeMenuUI.IsValid())
	{
		WelcomeMenuUI->LockControls(false);
	}

	if (PrivilegeResults == (uint32)IOnlineIdentity::EPrivilegeResults::NoFailures)
	{
		if (WelcomeMenuUI.IsValid())
		{
			WelcomeMenuUI->SetControllerAndAdvanceToMainMenu(PlayTogetherInfo.UserIndex);
		}
	}
	else
	{
		DisplayOnlinePrivilegeFailureDialogs(UserId, Privilege, PrivilegeResults);
		GotoState(BattleRoyaleInstanceState::WelcomeScreen);
	}
}

void UBattleRoyaleInstance::OnPostDemoPlay()
{
	GotoState( BattleRoyaleInstanceState::Playing );
}

void UBattleRoyaleInstance::HandleDemoPlaybackFailure( EDemoPlayFailure::Type FailureType, const FString& ErrorString )
{
	ShowMessageThenGotoState( FText::Format( NSLOCTEXT("UBattleRoyaleInstance", "DemoPlaybackFailedFmt", "Demo playback failed: {0}"), FText::FromString(ErrorString) ), NSLOCTEXT( "DialogButtons", "OKAY", "OK" ), FText::GetEmpty(), BattleRoyaleInstanceState::MainMenu );
}

void UBattleRoyaleInstance::StartGameInstance()
{
#if PLATFORM_PS4 == 0
	TCHAR Parm[4096] = TEXT("");

	const TCHAR* Cmd = FCommandLine::Get();

	// Catch the case where we want to override the map name on startup (used for connecting to other MP instances)
	if (FParse::Token(Cmd, Parm, ARRAY_COUNT(Parm), 0) && Parm[0] != '-')
	{
		// if we're 'overriding' with the default map anyway, don't set a bogus 'playing' state.
		if (!MainMenuMap.Contains(Parm))
		{
			FURL DefaultURL;
			DefaultURL.LoadURLConfig(TEXT("DefaultPlayer"), GGameIni);

			FURL URL(&DefaultURL, Parm, TRAVEL_Partial);

			if (URL.Valid)
			{
				UEngine* const Engine = GetEngine();

				FString Error;

				const EBrowseReturnVal::Type BrowseRet = Engine->Browse(*WorldContext, URL, Error);

				if (BrowseRet == EBrowseReturnVal::Success)
				{
					// Success, we loaded the map, go directly to playing state
					GotoState(BattleRoyaleInstanceState::Playing);
					return;
				}
				else if (BrowseRet == EBrowseReturnVal::Pending)
				{
					// Assume network connection
					LoadFrontEndMap(MainMenuMap);
					AddNetworkFailureHandlers();
					ShowLoadingScreen();
					GotoState(BattleRoyaleInstanceState::Playing);
					return;
				}
			}
		}
	}
#endif

	GotoInitialState();
}

FName UBattleRoyaleInstance::GetInitialState()
{
#if SHOOTER_CONSOLE_UI	
	// Start in the welcome screen state on consoles
	return BattleRoyaleInstanceState::WelcomeScreen;
#else
	// On PC, go directly to the main menu
	return BattleRoyaleInstanceState::MainMenu;
#endif
}

void UBattleRoyaleInstance::GotoInitialState()
{
	GotoState(GetInitialState());
}

void UBattleRoyaleInstance::ShowMessageThenGotoState( const FText& Message, const FText& OKButtonString, const FText& CancelButtonString, const FName& NewState, const bool OverrideExisting, TWeakObjectPtr< ULocalPlayer > PlayerOwner )
{
	UE_LOG( LogOnline, Log, TEXT( "ShowMessageThenGotoState: Message: %s, NewState: %s" ), *Message.ToString(), *NewState.ToString() );

	const bool bAtWelcomeScreen = PendingState == BattleRoyaleInstanceState::WelcomeScreen || CurrentState == BattleRoyaleInstanceState::WelcomeScreen;

	// Never override the welcome screen
	if ( bAtWelcomeScreen )
	{
		UE_LOG( LogOnline, Log, TEXT( "ShowMessageThenGotoState: Ignoring due to higher message priority in queue (at welcome screen)." ) );
		return;
	}

	const bool bAlreadyAtMessageMenu = PendingState == BattleRoyaleInstanceState::MessageMenu || CurrentState == BattleRoyaleInstanceState::MessageMenu;
	const bool bAlreadyAtDestState = PendingState == NewState || CurrentState == NewState;

	// If we are already going to the message menu, don't override unless asked to
	if ( bAlreadyAtMessageMenu && PendingMessage.NextState == NewState && !OverrideExisting )
	{
		UE_LOG( LogOnline, Log, TEXT( "ShowMessageThenGotoState: Ignoring due to higher message priority in queue (check 1)." ) );
		return;
	}

	// If we are already going to the message menu, and the next dest is welcome screen, don't override
	if ( bAlreadyAtMessageMenu && PendingMessage.NextState == BattleRoyaleInstanceState::WelcomeScreen )
	{
		UE_LOG( LogOnline, Log, TEXT( "ShowMessageThenGotoState: Ignoring due to higher message priority in queue (check 2)." ) );
		return;
	}

	// If we are already at the dest state, don't override unless asked
	if ( bAlreadyAtDestState && !OverrideExisting )
	{
		UE_LOG( LogOnline, Log, TEXT( "ShowMessageThenGotoState: Ignoring due to higher message priority in queue (check 3)" ) );
		return;
	}

	PendingMessage.DisplayString		= Message;
	PendingMessage.OKButtonString		= OKButtonString;
	PendingMessage.CancelButtonString	= CancelButtonString;
	PendingMessage.NextState			= NewState;
	PendingMessage.PlayerOwner			= PlayerOwner;

	if ( CurrentState == BattleRoyaleInstanceState::MessageMenu )
	{
		UE_LOG( LogOnline, Log, TEXT( "ShowMessageThenGotoState: Forcing new message" ) );
		EndMessageMenuState();
		BeginMessageMenuState();
	}
	else
	{
		GotoState(BattleRoyaleInstanceState::MessageMenu);
	}
}

void UBattleRoyaleInstance::ShowLoadingScreen()
{
	// This can be confusing, so here is what is happening:
	//	For LoadMap, we use the IBattleRoyaleLoadingScreenModule interface to show the load screen
	//  This is necessary since this is a blocking call, and our viewport loading screen won't get updated.
	//  We can't use IBattleRoyaleLoadingScreenModule for seamless travel though
	//  In this case, we just add a widget to the viewport, and have it update on the main thread
	//  To simplify things, we just do both, and you can't tell, one will cover the other if they both show at the same time
	IBattleRoyaleLoadingScreenModule* const LoadingScreenModule = FModuleManager::LoadModulePtr<IBattleRoyaleLoadingScreenModule>("BattleRoyaleLoadingScreen");
	if (LoadingScreenModule != nullptr)
	{
		LoadingScreenModule->StartInGameLoadingScreen();
	}

	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

	if ( ShooterViewport != NULL )
	{
		ShooterViewport->ShowLoadingScreen();
	}
}

bool UBattleRoyaleInstance::LoadFrontEndMap(const FString& MapName)
{
	bool bSuccess = true;

	// if already loaded, do nothing
	UWorld* const World = GetWorld();
	if (World)
	{
		FString const CurrentMapName = *World->PersistentLevel->GetOutermost()->GetName();
		//if (MapName.Find(TEXT("Highrise")) != -1)
		if (CurrentMapName == MapName)
		{
			return bSuccess;
		}
	}

	FString Error;
	EBrowseReturnVal::Type BrowseRet = EBrowseReturnVal::Failure;
	FURL URL(
		*FString::Printf(TEXT("%s"), *MapName)
		);

	if (URL.Valid && !HasAnyFlags(RF_ClassDefaultObject)) //CastChecked<UEngine>() will fail if using Default__BattleRoyaleInstance, so make sure that we're not default
	{
		BrowseRet = GetEngine()->Browse(*WorldContext, URL, Error);

		// Handle failure.
		if (BrowseRet != EBrowseReturnVal::Success)
		{
			UE_LOG(LogLoad, Fatal, TEXT("%s"), *FString::Printf(TEXT("Failed to enter %s: %s. Please check the log for errors."), *MapName, *Error));
			bSuccess = false;
		}
	}
	return bSuccess;
}

ABattleRoyaleSession* UBattleRoyaleInstance::GetGameSession() const
{
	UWorld* const World = GetWorld();
	if (World)
	{
		AGameModeBase* const Game = World->GetAuthGameMode();
		if (Game)
		{
			return Cast<ABattleRoyaleSession>(Game->GameSession);
		}
	}

	return nullptr;
}

void UBattleRoyaleInstance::TravelLocalSessionFailure(UWorld *World, ETravelFailure::Type FailureType, const FString& ReasonString)
{
	AShooterPlayerController_Menu* const FirstPC = Cast<AShooterPlayerController_Menu>(UGameplayStatics::GetPlayerController(GetWorld(), 0));
	if (FirstPC != nullptr)
	{
		FText ReturnReason = NSLOCTEXT("NetworkErrors", "JoinSessionFailed", "Join Session failed.");
		if (ReasonString.IsEmpty() == false)
		{
			ReturnReason = FText::Format(NSLOCTEXT("NetworkErrors", "JoinSessionFailedReasonFmt", "Join Session failed. {0}"), FText::FromString(ReasonString));
		}

		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
	}
}

void UBattleRoyaleInstance::ShowMessageThenGoMain(const FText& Message, const FText& OKButtonString, const FText& CancelButtonString)
{
	ShowMessageThenGotoState(Message, OKButtonString, CancelButtonString, BattleRoyaleInstanceState::MainMenu);
}

void UBattleRoyaleInstance::SetPendingInvite(const FShooterPendingInvite& InPendingInvite)
{
	PendingInvite = InPendingInvite;
}

void UBattleRoyaleInstance::GotoState(FName NewState)
{
	UE_LOG( LogOnline, Log, TEXT( "GotoState: NewState: %s" ), *NewState.ToString() );

	PendingState = NewState;
}

void UBattleRoyaleInstance::MaybeChangeState()
{
	if ( (PendingState != CurrentState) && (PendingState != BattleRoyaleInstanceState::None) )
	{
		FName const OldState = CurrentState;

		// end current state
		EndCurrentState(PendingState);

		// begin new state
		BeginNewState(PendingState, OldState);

		// clear pending change
		PendingState = BattleRoyaleInstanceState::None;
	}
}

void UBattleRoyaleInstance::EndCurrentState(FName NextState)
{
	// per-state custom ending code here
	if (CurrentState == BattleRoyaleInstanceState::PendingInvite)
	{
		EndPendingInviteState();
	}
	else if (CurrentState == BattleRoyaleInstanceState::WelcomeScreen)
	{
		EndWelcomeScreenState();
	}
	else if (CurrentState == BattleRoyaleInstanceState::MainMenu)
	{
		EndMainMenuState();
	}
	else if (CurrentState == BattleRoyaleInstanceState::MessageMenu)
	{
		EndMessageMenuState();
	}
	else if (CurrentState == BattleRoyaleInstanceState::Playing)
	{
		EndPlayingState();
	}

	CurrentState = BattleRoyaleInstanceState::None;
}

void UBattleRoyaleInstance::BeginNewState(FName NewState, FName PrevState)
{
	// per-state custom starting code here

	if (NewState == BattleRoyaleInstanceState::PendingInvite)
	{
		BeginPendingInviteState();
	}
	else if (NewState == BattleRoyaleInstanceState::WelcomeScreen)
	{
		BeginWelcomeScreenState();
	}
	else if (NewState == BattleRoyaleInstanceState::MainMenu)
	{
		BeginMainMenuState();
	}
	else if (NewState == BattleRoyaleInstanceState::MessageMenu)
	{
		BeginMessageMenuState();
	}
	else if (NewState == BattleRoyaleInstanceState::Playing)
	{
		BeginPlayingState();
	}

	CurrentState = NewState;
}

void UBattleRoyaleInstance::BeginPendingInviteState()
{	
	if (LoadFrontEndMap(MainMenuMap))
	{				
		StartOnlinePrivilegeTask(IOnlineIdentity::FOnGetUserPrivilegeCompleteDelegate::CreateUObject(this, &UBattleRoyaleInstance::OnUserCanPlayInvite), EUserPrivileges::CanPlayOnline, PendingInvite.UserId);
	}
	else
	{
		GotoState(BattleRoyaleInstanceState::WelcomeScreen);
	}
}

void UBattleRoyaleInstance::EndPendingInviteState()
{
	// cleanup in case the state changed before the pending invite was handled.
	CleanupOnlinePrivilegeTask();
}

void UBattleRoyaleInstance::BeginWelcomeScreenState()
{
	//this must come before split screen player removal so that the OSS sets all players to not using online features.
	SetIsOnline(false);

	// Remove any possible splitscren players
	RemoveSplitScreenPlayers();

	LoadFrontEndMap(WelcomeScreenMap);

	ULocalPlayer* const LocalPlayer = GetFirstGamePlayer();
	LocalPlayer->SetCachedUniqueNetId(nullptr);
	check(!WelcomeMenuUI.IsValid());
	WelcomeMenuUI = MakeShareable(new FShooterWelcomeMenu);
	WelcomeMenuUI->Construct( this );
	WelcomeMenuUI->AddToGameViewport();

	// Disallow splitscreen (we will allow while in the playing state)
	GetGameViewportClient()->SetDisableSplitscreenOverride( true );
}

void UBattleRoyaleInstance::EndWelcomeScreenState()
{
	if (WelcomeMenuUI.IsValid())
	{
		WelcomeMenuUI->RemoveFromGameViewport();
		WelcomeMenuUI = nullptr;
	}
}

void UBattleRoyaleInstance::SetPresenceForLocalPlayers(const FString& StatusStr, const FVariantData& PresenceData)
{
	const auto Presence = Online::GetPresenceInterface();
	if (Presence.IsValid())
	{
		for (int i = 0; i < LocalPlayers.Num(); ++i)
		{
			const TSharedPtr<const FUniqueNetId> UserId = LocalPlayers[i]->GetPreferredUniqueNetId();

			if (UserId.IsValid())
			{
				FOnlineUserPresenceStatus PresenceStatus;
				PresenceStatus.StatusStr = StatusStr;
				PresenceStatus.Properties.Add(DefaultPresenceKey, PresenceData);

				Presence->SetPresence(*UserId, PresenceStatus);
			}
		}
	}
}

void UBattleRoyaleInstance::BeginMainMenuState()
{
	// Make sure we're not showing the loadscreen
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

	if ( ShooterViewport != NULL )
	{
		ShooterViewport->HideLoadingScreen();
	}

	SetIsOnline(false);

	// Disallow splitscreen
	UGameViewportClient* GameViewportClient = GetGameViewportClient();
	
	if (GameViewportClient)
	{
		GetGameViewportClient()->SetDisableSplitscreenOverride(true);
	}

	// Remove any possible splitscren players
	RemoveSplitScreenPlayers();

	// Set presence to menu state for the owning player
	SetPresenceForLocalPlayers(FString(TEXT("In Menu")), FVariantData(FString(TEXT("OnMenu"))));

	// load startup map
	LoadFrontEndMap(MainMenuMap);

	// player 0 gets to own the UI
	ULocalPlayer* const Player = GetFirstGamePlayer();

	MainMenuUI = MakeShareable(new FShooterMainMenu());
	MainMenuUI->Construct(this, Player);
	MainMenuUI->AddMenuToGameViewport();

	// It's possible that a play together event was sent by the system while the player was in-game or didn't
	// have the application launched. The game will automatically go directly to the main menu state in those cases
	// so this will handle Play Together if that is why we transitioned here.
	if (PlayTogetherInfo.UserIndex != -1)
	{
		MainMenuUI->OnPlayTogetherEventReceived();
	}

#if !SHOOTER_CONSOLE_UI
	// The cached unique net ID is usually set on the welcome screen, but there isn't
	// one on PC/Mac, so do it here.
	if (Player != nullptr)
	{
		Player->SetControllerId(0);
		Player->SetCachedUniqueNetId(Player->GetUniqueNetIdFromCachedControllerId());
	}
#endif

	RemoveNetworkFailureHandlers();
}

void UBattleRoyaleInstance::EndMainMenuState()
{
	if (MainMenuUI.IsValid())
	{
		MainMenuUI->RemoveMenuFromGameViewport();
		MainMenuUI = nullptr;
	}
}

void UBattleRoyaleInstance::BeginMessageMenuState()
{
	if (PendingMessage.DisplayString.IsEmpty())
	{
		UE_LOG(LogOnlineGame, Warning, TEXT("UBattleRoyaleInstance::BeginMessageMenuState: Display string is empty"));
		GotoInitialState();
		return;
	}

	// Make sure we're not showing the loadscreen
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

	if ( ShooterViewport != NULL )
	{
		ShooterViewport->HideLoadingScreen();
	}

	check(!MessageMenuUI.IsValid());
	MessageMenuUI = MakeShareable(new FShooterMessageMenu);
	MessageMenuUI->Construct(this, PendingMessage.PlayerOwner, PendingMessage.DisplayString, PendingMessage.OKButtonString, PendingMessage.CancelButtonString, PendingMessage.NextState);

	PendingMessage.DisplayString = FText::GetEmpty();
}

void UBattleRoyaleInstance::EndMessageMenuState()
{
	if (MessageMenuUI.IsValid())
	{
		MessageMenuUI->RemoveFromGameViewport();
		MessageMenuUI = nullptr;
	}
}

void UBattleRoyaleInstance::BeginPlayingState()
{
	bPendingEnableSplitscreen = true;

	// Set presence for playing in a map
	SetPresenceForLocalPlayers(FString(TEXT("In Game")), FVariantData(FString(TEXT("InGame"))));

	// Make sure viewport has focus
	FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void UBattleRoyaleInstance::EndPlayingState()
{
	// Disallow splitscreen
	GetGameViewportClient()->SetDisableSplitscreenOverride( true );

	// Clear the players' presence information
	SetPresenceForLocalPlayers(FString(TEXT("In Menu")), FVariantData(FString(TEXT("OnMenu"))));

	UWorld* const World = GetWorld();
	ABattleRoyaleState* const GameState = World != NULL ? World->GetGameState<ABattleRoyaleState>() : NULL;

	if (GameState)
	{
		// Send round end events for local players
		for (int i = 0; i < LocalPlayers.Num(); ++i)
		{
			auto ShooterPC = Cast<AShooterPlayerController>(LocalPlayers[i]->PlayerController);
			if (ShooterPC)
			{
				// Assuming you can't win if you quit early
				ShooterPC->ClientSendRoundEndEvent(false, GameState->ElapsedTime);
			}
		}

		// Give the game state a chance to cleanup first
		GameState->RequestFinishAndExitToMainMenu();
	}
	else
	{
		// If there is no game state, make sure the session is in a good state
		CleanupSessionOnReturnToMenu();
	}
}

void UBattleRoyaleInstance::OnEndSessionComplete( FName SessionName, bool bWasSuccessful )
{
	UE_LOG(LogOnline, Log, TEXT("UBattleRoyaleInstance::OnEndSessionComplete: Session=%s bWasSuccessful=%s"), *SessionName.ToString(), bWasSuccessful ? TEXT("true") : TEXT("false") );

	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->ClearOnStartSessionCompleteDelegate_Handle  (OnStartSessionCompleteDelegateHandle);
			Sessions->ClearOnEndSessionCompleteDelegate_Handle    (OnEndSessionCompleteDelegateHandle);
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(OnDestroySessionCompleteDelegateHandle);
		}
	}

	// continue
	CleanupSessionOnReturnToMenu();
}

void UBattleRoyaleInstance::CleanupSessionOnReturnToMenu()
{
	bool bPendingOnlineOp = false;

	// end online game and then destroy it
	IOnlineSubsystem * OnlineSub = IOnlineSubsystem::Get();
	IOnlineSessionPtr Sessions = ( OnlineSub != NULL ) ? OnlineSub->GetSessionInterface() : NULL;

	if ( Sessions.IsValid() )
	{
		EOnlineSessionState::Type SessionState = Sessions->GetSessionState(GameSessionName);
		UE_LOG(LogOnline, Log, TEXT("Session %s is '%s'"), *GameSessionName.ToString(), EOnlineSessionState::ToString(SessionState));

		if ( EOnlineSessionState::InProgress == SessionState )
		{
			UE_LOG(LogOnline, Log, TEXT("Ending session %s on return to main menu"), *GameSessionName.ToString() );
			OnEndSessionCompleteDelegateHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
			Sessions->EndSession(GameSessionName);
			bPendingOnlineOp = true;
		}
		else if ( EOnlineSessionState::Ending == SessionState )
		{
			UE_LOG(LogOnline, Log, TEXT("Waiting for session %s to end on return to main menu"), *GameSessionName.ToString() );
			OnEndSessionCompleteDelegateHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
			bPendingOnlineOp = true;
		}
		else if ( EOnlineSessionState::Ended == SessionState || EOnlineSessionState::Pending == SessionState )
		{
			UE_LOG(LogOnline, Log, TEXT("Destroying session %s on return to main menu"), *GameSessionName.ToString() );				
			OnDestroySessionCompleteDelegateHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
			Sessions->DestroySession(GameSessionName);
			bPendingOnlineOp = true;
		}
		else if ( EOnlineSessionState::Starting == SessionState )
		{
			UE_LOG(LogOnline, Log, TEXT("Waiting for session %s to start, and then we will end it to return to main menu"), *GameSessionName.ToString() );
			OnStartSessionCompleteDelegateHandle = Sessions->AddOnStartSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
			bPendingOnlineOp = true;
		}
	}

	if ( !bPendingOnlineOp )
	{
		//GEngine->HandleDisconnect( GetWorld(), GetWorld()->GetNetDriver() );
	}
}

void UBattleRoyaleInstance::LabelPlayerAsQuitter(ULocalPlayer* LocalPlayer) const
{
	AShooterPlayerState* const PlayerState = LocalPlayer && LocalPlayer->PlayerController ? Cast<AShooterPlayerState>(LocalPlayer->PlayerController->PlayerState) : nullptr;	
	if(PlayerState)
	{
		PlayerState->SetQuitter(true);
	}
}

void UBattleRoyaleInstance::RemoveNetworkFailureHandlers()
{
	// Remove the local session/travel failure bindings if they exist
	if (GEngine->OnTravelFailure().IsBoundToObject(this) == true)
	{
		GEngine->OnTravelFailure().Remove(TravelLocalSessionFailureDelegateHandle);
	}
}

void UBattleRoyaleInstance::AddNetworkFailureHandlers()
{
	// Add network/travel error handlers (if they are not already there)
	if (GEngine->OnTravelFailure().IsBoundToObject(this) == false)
	{
		TravelLocalSessionFailureDelegateHandle = GEngine->OnTravelFailure().AddUObject(this, &UBattleRoyaleInstance::TravelLocalSessionFailure);
	}
}

TSubclassOf<UOnlineSession> UBattleRoyaleInstance::GetOnlineSessionClass()
{
	return UShooterOnlineSessionClient::StaticClass();
}

// starts playing a game as the host
bool UBattleRoyaleInstance::HostGame(ULocalPlayer* LocalPlayer, const FString& GameType, const FString& InTravelURL)
{
	if (!GetIsOnline())
	{
		//
		// Offline game, just go straight to map
		//

		ShowLoadingScreen();
		GotoState(BattleRoyaleInstanceState::Playing);

		// Travel to the specified match URL
		TravelURL = InTravelURL;
		GetWorld()->ServerTravel(TravelURL);
		return true;
	}

	//
	// Online game
	//

	ABattleRoyaleSession* const GameSession = GetGameSession();
	if (GameSession)
	{
		// add callback delegate for completion
		OnCreatePresenceSessionCompleteDelegateHandle = GameSession->OnCreatePresenceSessionComplete().AddUObject(this, &UBattleRoyaleInstance::OnCreatePresenceSessionComplete);

		TravelURL = InTravelURL;
		bool const bIsLanMatch = InTravelURL.Contains(TEXT("?bIsLanMatch"));

		//determine the map name from the travelURL
		const FString& MapNameSubStr = "/Game/Maps/";
		const FString& ChoppedMapName = TravelURL.RightChop(MapNameSubStr.Len());
		const FString& MapName = ChoppedMapName.LeftChop(ChoppedMapName.Len() - ChoppedMapName.Find("?game"));

		if (GameSession->HostSession(LocalPlayer->GetPreferredUniqueNetId(), GameSessionName, GameType, MapName, bIsLanMatch, true, ABattleRoyaleSession::DEFAULT_NUM_PLAYERS))
		{
			// If any error occured in the above, pending state would be set
			if ( (PendingState == CurrentState) || (PendingState == BattleRoyaleInstanceState::None) )
			{
				// Go ahead and go into loading state now
				// If we fail, the delegate will handle showing the proper messaging and move to the correct state
				ShowLoadingScreen();
				GotoState(BattleRoyaleInstanceState::Playing);
				return true;
			}
		}
	}

	return false;
}

bool UBattleRoyaleInstance::JoinSession(ULocalPlayer* LocalPlayer, int32 SessionIndexInSearchResults)
{
	// needs to tear anything down based on current state?

	ABattleRoyaleSession* const GameSession = GetGameSession();
	if (GameSession)
	{
		AddNetworkFailureHandlers();

		OnJoinSessionCompleteDelegateHandle = GameSession->OnJoinSessionComplete().AddUObject(this, &UBattleRoyaleInstance::OnJoinSessionComplete);
		if (GameSession->JoinSession(LocalPlayer->GetPreferredUniqueNetId(), GameSessionName, SessionIndexInSearchResults))
		{
			// If any error occured in the above, pending state would be set
			if ( (PendingState == CurrentState) || (PendingState == BattleRoyaleInstanceState::None) )
			{
				// Go ahead and go into loading state now
				// If we fail, the delegate will handle showing the proper messaging and move to the correct state
				ShowLoadingScreen();
				GotoState(BattleRoyaleInstanceState::Playing);
				return true;
			}
		}
	}

	return false;
}

bool UBattleRoyaleInstance::JoinSession(ULocalPlayer* LocalPlayer, const FOnlineSessionSearchResult& SearchResult)
{
	// needs to tear anything down based on current state?
	ABattleRoyaleSession* const GameSession = GetGameSession();
	if (GameSession)
	{
		AddNetworkFailureHandlers();

		OnJoinSessionCompleteDelegateHandle = GameSession->OnJoinSessionComplete().AddUObject(this, &UBattleRoyaleInstance::OnJoinSessionComplete);
		if (GameSession->JoinSession(LocalPlayer->GetPreferredUniqueNetId(), GameSessionName, SearchResult))
		{
			// If any error occured in the above, pending state would be set
			if ( (PendingState == CurrentState) || (PendingState == BattleRoyaleInstanceState::None) )
			{
				// Go ahead and go into loading state now
				// If we fail, the delegate will handle showing the proper messaging and move to the correct state
				ShowLoadingScreen();
				GotoState(BattleRoyaleInstanceState::Playing);
				return true;
			}
		}
	}

	return false;
}

bool UBattleRoyaleInstance::PlayDemo(ULocalPlayer* LocalPlayer, const FString& DemoName)
{
	ShowLoadingScreen();

	// Play the demo
	PlayReplay(DemoName);
	
	return true;
}

/** Callback which is intended to be called upon finding sessions */
void UBattleRoyaleInstance::OnJoinSessionComplete(EOnJoinSessionCompleteResult::Type Result)
{
	// unhook the delegate
	ABattleRoyaleSession* const GameSession = GetGameSession();
	if (GameSession)
	{
		GameSession->OnJoinSessionComplete().Remove(OnJoinSessionCompleteDelegateHandle);
	}

	// Add the splitscreen player if one exists
	if (Result == EOnJoinSessionCompleteResult::Success && LocalPlayers.Num() > 1)
	{
		auto Sessions = Online::GetSessionInterface();
		if (Sessions.IsValid() && LocalPlayers[1]->GetPreferredUniqueNetId().IsValid())
		{
			Sessions->RegisterLocalPlayer(*LocalPlayers[1]->GetPreferredUniqueNetId(), GameSessionName,
				FOnRegisterLocalPlayerCompleteDelegate::CreateUObject(this, &UBattleRoyaleInstance::OnRegisterJoiningLocalPlayerComplete));
		}
	}
	else
	{
		// We either failed or there is only a single local user
		FinishJoinSession(Result);
	}
}

void UBattleRoyaleInstance::FinishJoinSession(EOnJoinSessionCompleteResult::Type Result)
{
	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		FText ReturnReason;
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:
			ReturnReason = NSLOCTEXT("NetworkErrors", "JoinSessionFailed", "Game is full.");
			break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			ReturnReason = NSLOCTEXT("NetworkErrors", "JoinSessionFailed", "Game no longer exists.");
			break;
		default:
			ReturnReason = NSLOCTEXT("NetworkErrors", "JoinSessionFailed", "Join failed.");
			break;
		}

		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		RemoveNetworkFailureHandlers();
		ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
		return;
	}

	InternalTravelToSession(GameSessionName);
}

void UBattleRoyaleInstance::OnRegisterJoiningLocalPlayerComplete(const FUniqueNetId& PlayerId, EOnJoinSessionCompleteResult::Type Result)
{
	FinishJoinSession(Result);
}

void UBattleRoyaleInstance::InternalTravelToSession(const FName& SessionName)
{
	APlayerController * const PlayerController = GetFirstLocalPlayerController();

	if ( PlayerController == nullptr )
	{
		FText ReturnReason = NSLOCTEXT("NetworkErrors", "InvalidPlayerController", "Invalid Player Controller");
		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		RemoveNetworkFailureHandlers();
		ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
		return;
	}

	// travel to session
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();

	if ( OnlineSub == nullptr )
	{
		FText ReturnReason = NSLOCTEXT("NetworkErrors", "OSSMissing", "OSS missing");
		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		RemoveNetworkFailureHandlers();
		ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
		return;
	}

	FString URL;
	IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();

	if ( !Sessions.IsValid() || !Sessions->GetResolvedConnectString( SessionName, URL ) )
	{
		FText FailReason = NSLOCTEXT("NetworkErrors", "TravelSessionFailed", "Travel to Session failed.");
		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		ShowMessageThenGoMain(FailReason, OKButton, FText::GetEmpty());
		UE_LOG(LogOnlineGame, Warning, TEXT("Failed to travel to session upon joining it"));
		return;
	}

	PlayerController->ClientTravel(URL, TRAVEL_Absolute);
}

/** Callback which is intended to be called upon session creation */
void UBattleRoyaleInstance::OnCreatePresenceSessionComplete(FName SessionName, bool bWasSuccessful)
{
	ABattleRoyaleSession* const GameSession = GetGameSession();
	if (GameSession)
	{
		GameSession->OnCreatePresenceSessionComplete().Remove(OnCreatePresenceSessionCompleteDelegateHandle);

		// Add the splitscreen player if one exists
		if (bWasSuccessful && LocalPlayers.Num() > 1)
		{
			auto Sessions = Online::GetSessionInterface();
			if (Sessions.IsValid() && LocalPlayers[1]->GetPreferredUniqueNetId().IsValid())
			{
				Sessions->RegisterLocalPlayer(*LocalPlayers[1]->GetPreferredUniqueNetId(), GameSessionName,
					FOnRegisterLocalPlayerCompleteDelegate::CreateUObject(this, &UBattleRoyaleInstance::OnRegisterLocalPlayerComplete));
			}
		}
		else
		{
			// We either failed or there is only a single local user
			FinishSessionCreation(bWasSuccessful ? EOnJoinSessionCompleteResult::Success : EOnJoinSessionCompleteResult::UnknownError);
		}
	}
}

/** Initiates the session searching */
bool UBattleRoyaleInstance::FindSessions(ULocalPlayer* PlayerOwner, bool bIsDedicatedServer, bool bFindLAN)
{
	bool bResult = false;

	check(PlayerOwner != nullptr);
	if (PlayerOwner)
	{
		ABattleRoyaleSession* const GameSession = GetGameSession();
		if (GameSession)
		{
			GameSession->OnFindSessionsComplete().RemoveAll(this);
			OnSearchSessionsCompleteDelegateHandle = GameSession->OnFindSessionsComplete().AddUObject(this, &UBattleRoyaleInstance::OnSearchSessionsComplete);

			GameSession->FindSessions(PlayerOwner->GetPreferredUniqueNetId(), GameSessionName, bFindLAN, !bIsDedicatedServer);

			bResult = true;
		}
	}

	return bResult;
}

/** Callback which is intended to be called upon finding sessions */
void UBattleRoyaleInstance::OnSearchSessionsComplete(bool bWasSuccessful)
{
	ABattleRoyaleSession* const Session = GetGameSession();
	if (Session)
	{
		Session->OnFindSessionsComplete().Remove(OnSearchSessionsCompleteDelegateHandle);
	}
}

bool UBattleRoyaleInstance::Tick(float DeltaSeconds)
{
	// Dedicated server doesn't need to worry about game state
	if (IsRunningDedicatedServer() == true)
	{
		return true;
	}

	MaybeChangeState();

	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

	if (CurrentState != BattleRoyaleInstanceState::WelcomeScreen)
	{
		// If at any point we aren't licensed (but we are after welcome screen) bounce them back to the welcome screen
		if (!bIsLicensed && CurrentState != BattleRoyaleInstanceState::None && !ShooterViewport->IsShowingDialog())
		{
			const FText ReturnReason	= NSLOCTEXT( "ProfileMessages", "NeedLicense", "The signed in users do not have a license for this game. Please purchase BattleRoyale from the Xbox Marketplace or sign in a user with a valid license." );
			const FText OKButton		= NSLOCTEXT( "DialogButtons", "OKAY", "OK" );

			ShowMessageThenGotoState( ReturnReason, OKButton, FText::GetEmpty(), BattleRoyaleInstanceState::WelcomeScreen );
		}

		// Show controller disconnected dialog if any local players have an invalid controller
		if(ShooterViewport != NULL &&
			!ShooterViewport->IsShowingDialog())
		{
			for (int i = 0; i < LocalPlayers.Num(); ++i)
			{
				if (LocalPlayers[i] && LocalPlayers[i]->GetControllerId() == -1)
				{
					ShooterViewport->ShowDialog( 
						LocalPlayers[i],
						EShooterDialogType::ControllerDisconnected,
						FText::Format(NSLOCTEXT("ProfileMessages", "PlayerReconnectControllerFmt", "Player {0}, please reconnect your controller."), FText::AsNumber(i + 1)),
#if PLATFORM_PS4
						NSLOCTEXT("DialogButtons", "PS4_CrossButtonContinue", "Cross Button - Continue"),
#else
						NSLOCTEXT("DialogButtons", "AButtonContinue", "A - Continue"),
#endif
						FText::GetEmpty(),
						FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnControllerReconnectConfirm),
						FOnClicked()
					);
				}
			}
		}
	}

	// If we have a pending invite, and we are at the welcome screen, and the session is properly shut down, accept it
	if (PendingInvite.UserId.IsValid() && PendingInvite.bPrivilegesCheckedAndAllowed && CurrentState == BattleRoyaleInstanceState::PendingInvite)
	{
		IOnlineSubsystem * OnlineSub = IOnlineSubsystem::Get();
		IOnlineSessionPtr Sessions = (OnlineSub != NULL) ? OnlineSub->GetSessionInterface() : NULL;

		if (Sessions.IsValid())
		{
			EOnlineSessionState::Type SessionState = Sessions->GetSessionState(GameSessionName);

			if (SessionState == EOnlineSessionState::NoSession)
			{
				ULocalPlayer * NewPlayerOwner = GetFirstGamePlayer();

				if (NewPlayerOwner != nullptr)
				{
					NewPlayerOwner->SetControllerId(PendingInvite.ControllerId);
					NewPlayerOwner->SetCachedUniqueNetId(PendingInvite.UserId);
					SetIsOnline(true);
					JoinSession(NewPlayerOwner, PendingInvite.InviteResult);					
				}

				PendingInvite.UserId.Reset();
			}			
		}
	}

	return true;
}

bool UBattleRoyaleInstance::HandleOpenCommand(const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld)
{
	bool const bOpenSuccessful = Super::HandleOpenCommand(Cmd, Ar, InWorld);
	if (bOpenSuccessful)
	{
		GotoState(BattleRoyaleInstanceState::Playing);
	}

	return bOpenSuccessful;
}

void UBattleRoyaleInstance::HandleSignInChangeMessaging()
{
	// Master user signed out, go to initial state (if we aren't there already)
	if ( CurrentState != GetInitialState() )
	{
#if SHOOTER_CONSOLE_UI
		// Display message on consoles
		const FText ReturnReason	= NSLOCTEXT( "ProfileMessages", "SignInChange", "Sign in status change occurred." );
		const FText OKButton		= NSLOCTEXT( "DialogButtons", "OKAY", "OK" );

		ShowMessageThenGotoState(ReturnReason, OKButton, FText::GetEmpty(), GetInitialState());
#else								
		GotoInitialState();
#endif
	}
}

void UBattleRoyaleInstance::HandleUserLoginChanged(int32 GameUserIndex, ELoginStatus::Type PreviousLoginStatus, ELoginStatus::Type LoginStatus, const FUniqueNetId& UserId)
{
	// On Switch, accounts can play in LAN games whether they are signed in online or not. 
#if PLATFORM_SWITCH
	const bool bDowngraded = LoginStatus == ELoginStatus::NotLoggedIn;
#else
	const bool bDowngraded = (LoginStatus == ELoginStatus::NotLoggedIn && !GetIsOnline()) || (LoginStatus != ELoginStatus::LoggedIn && GetIsOnline());
#endif

	UE_LOG( LogOnline, Log, TEXT( "HandleUserLoginChanged: bDownGraded: %i" ), (int)bDowngraded );

	TSharedPtr<GenericApplication> GenericApplication = FSlateApplication::Get().GetPlatformApplication();
	bIsLicensed = GenericApplication->ApplicationLicenseValid();

	// Find the local player associated with this unique net id
	ULocalPlayer * LocalPlayer = FindLocalPlayerFromUniqueNetId( UserId );

	LocalPlayerOnlineStatus[GameUserIndex] = LoginStatus;

	// If this user is signed out, but was previously signed in, punt to welcome (or remove splitscreen if that makes sense)
	if ( LocalPlayer != NULL )
	{
		if (bDowngraded)
		{
			UE_LOG( LogOnline, Log, TEXT( "HandleUserLoginChanged: Player logged out: %s" ), *UserId.ToString() );

			LabelPlayerAsQuitter(LocalPlayer);

			// Check to see if this was the master, or if this was a split-screen player on the client
			if ( LocalPlayer == GetFirstGamePlayer() || GetIsOnline() )
			{
				HandleSignInChangeMessaging();
			}
			else
			{
				// Remove local split-screen players from the list
				RemoveExistingLocalPlayer( LocalPlayer );
			}
		}
	}
}

void UBattleRoyaleInstance::HandleAppWillDeactivate()
{
	if (CurrentState == BattleRoyaleInstanceState::Playing)
	{
		// Just have the first player controller pause the game.
		UWorld* const GameWorld = GetWorld();
		if (GameWorld)
		{
			// protect against a second pause menu loading on top of an existing one if someone presses the Jewel / PS buttons.
			bool bNeedsPause = true;
			for (FConstControllerIterator It = GameWorld->GetControllerIterator(); It; ++It)
			{
				AShooterPlayerController* Controller = Cast<AShooterPlayerController>(*It);
				if (Controller && (Controller->IsPaused() || Controller->IsGameMenuVisible()))
				{
					bNeedsPause = false;
					break;
				}
			}

			if (bNeedsPause)
			{
				AShooterPlayerController* const Controller = Cast<AShooterPlayerController>(GameWorld->GetFirstPlayerController());
				if (Controller)
				{
					Controller->ShowInGameMenu();
				}
			}
		}
	}
}

void UBattleRoyaleInstance::HandleAppSuspend()
{
	// Players will lose connection on resume. However it is possible the game will exit before we get a resume, so we must kick off round end events here.
	UE_LOG( LogOnline, Warning, TEXT( "UBattleRoyaleInstance::HandleAppSuspend" ) );
	UWorld* const World = GetWorld(); 
	ABattleRoyaleState* const GameState = World != NULL ? World->GetGameState<ABattleRoyaleState>() : NULL;

	if ( CurrentState != BattleRoyaleInstanceState::None && CurrentState != GetInitialState() )
	{
		UE_LOG( LogOnline, Warning, TEXT( "UBattleRoyaleInstance::HandleAppSuspend: Sending round end event for players" ) );

		// Send round end events for local players
		for (int i = 0; i < LocalPlayers.Num(); ++i)
		{
			auto ShooterPC = Cast<AShooterPlayerController>(LocalPlayers[i]->PlayerController);
			if (ShooterPC)
			{
				// Assuming you can't win if you quit early
				ShooterPC->ClientSendRoundEndEvent(false, GameState->ElapsedTime);
			}
		}
	}
}

void UBattleRoyaleInstance::HandleAppResume()
{
	UE_LOG( LogOnline, Log, TEXT( "UBattleRoyaleInstance::HandleAppResume" ) );

	if ( CurrentState != BattleRoyaleInstanceState::None && CurrentState != GetInitialState() )
	{
		UE_LOG( LogOnline, Warning, TEXT( "UBattleRoyaleInstance::HandleAppResume: Attempting to sign out players" ) );

		for ( int32 i = 0; i < LocalPlayers.Num(); ++i )
		{
			if ( LocalPlayers[i]->GetCachedUniqueNetId().IsValid() && LocalPlayerOnlineStatus[i] == ELoginStatus::LoggedIn && !IsLocalPlayerOnline( LocalPlayers[i] ) )
			{
				UE_LOG( LogOnline, Log, TEXT( "UBattleRoyaleInstance::HandleAppResume: Signed out during resume." ) );
				HandleSignInChangeMessaging();
				break;
			}
		}
	}
}

void UBattleRoyaleInstance::HandleAppLicenseUpdate()
{
	TSharedPtr<GenericApplication> GenericApplication = FSlateApplication::Get().GetPlatformApplication();
	bIsLicensed = GenericApplication->ApplicationLicenseValid();
}

void UBattleRoyaleInstance::HandleSafeFrameChanged()
{
	UCanvas::UpdateAllCanvasSafeZoneData();
}

void UBattleRoyaleInstance::RemoveExistingLocalPlayer(ULocalPlayer* ExistingPlayer)
{
	check(ExistingPlayer);
	if (ExistingPlayer->PlayerController != NULL)
	{
		// Kill the player
		AShooterCharacter* MyPawn = Cast<AShooterCharacter>(ExistingPlayer->PlayerController->GetPawn());
		if ( MyPawn )
		{
			MyPawn->KilledBy(NULL);
		}
	}

	// Remove local split-screen players from the list
	RemoveLocalPlayer( ExistingPlayer );
}

void UBattleRoyaleInstance::RemoveSplitScreenPlayers()
{
	// if we had been split screen, toss the extra players now
	// remove every player, back to front, except the first one
	while (LocalPlayers.Num() > 1)
	{
		ULocalPlayer* const PlayerToRemove = LocalPlayers.Last();
		RemoveExistingLocalPlayer(PlayerToRemove);
	}
}

FReply UBattleRoyaleInstance::OnPairingUsePreviousProfile()
{
	// Do nothing (except hide the message) if they want to continue using previous profile
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

	if ( ShooterViewport != nullptr )
	{
		ShooterViewport->HideDialog();
	}

	return FReply::Handled();
}

FReply UBattleRoyaleInstance::OnPairingUseNewProfile()
{
	HandleSignInChangeMessaging();
	return FReply::Handled();
}

void UBattleRoyaleInstance::HandleControllerPairingChanged( int GameUserIndex, const FUniqueNetId& PreviousUser, const FUniqueNetId& NewUser )
{
	UE_LOG(LogOnlineGame, Log, TEXT("UBattleRoyaleInstance::HandleControllerPairingChanged GameUserIndex %d PreviousUser '%s' NewUser '%s'"),
		GameUserIndex, *PreviousUser.ToString(), *NewUser.ToString());
	
	if ( CurrentState == BattleRoyaleInstanceState::WelcomeScreen )
	{
		// Don't care about pairing changes at welcome screen
		return;
	}

#if SHOOTER_CONSOLE_UI && PLATFORM_XBOXONE
	if ( IgnorePairingChangeForControllerId != -1 && GameUserIndex == IgnorePairingChangeForControllerId )
	{
		// We were told to ignore
		IgnorePairingChangeForControllerId = -1;	// Reset now so there there is no chance this remains in a bad state
		return;
	}

	if ( PreviousUser.IsValid() && !NewUser.IsValid() )
	{
		// Treat this as a disconnect or signout, which is handled somewhere else
		return;
	}

	if ( !PreviousUser.IsValid() && NewUser.IsValid() )
	{
		// Treat this as a signin
		ULocalPlayer * ControlledLocalPlayer = FindLocalPlayerFromControllerId( GameUserIndex );

		if ( ControlledLocalPlayer != NULL && !ControlledLocalPlayer->GetCachedUniqueNetId().IsValid() )
		{
			// If a player that previously selected "continue without saving" signs into this controller, move them back to welcome screen
			HandleSignInChangeMessaging();
		}
		
		return;
	}

	// Find the local player currently being controlled by this controller
	ULocalPlayer * ControlledLocalPlayer	= FindLocalPlayerFromControllerId( GameUserIndex );

	// See if the newly assigned profile is in our local player list
	ULocalPlayer * NewLocalPlayer			= FindLocalPlayerFromUniqueNetId( NewUser );

	// If the local player being controlled is not the target of the pairing change, then give them a chance 
	// to continue controlling the old player with this controller
	if ( ControlledLocalPlayer != nullptr && ControlledLocalPlayer != NewLocalPlayer )
	{
		UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

		if ( ShooterViewport != nullptr )
		{
			ShooterViewport->ShowDialog( 
				nullptr,
				EShooterDialogType::Generic,
				NSLOCTEXT("ProfileMessages", "PairingChanged", "Your controller has been paired to another profile, would you like to switch to this new profile now? Selecting YES will sign out of the previous profile."),
				NSLOCTEXT("DialogButtons", "YES", "A - YES"),
				NSLOCTEXT("DialogButtons", "NO", "B - NO"),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnPairingUseNewProfile),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnPairingUsePreviousProfile)
			);
		}
	}
#endif
}

void UBattleRoyaleInstance::HandleControllerConnectionChange( bool bIsConnection, int32 Unused, int32 GameUserIndex )
{
	UE_LOG(LogOnlineGame, Log, TEXT("UBattleRoyaleInstance::HandleControllerConnectionChange bIsConnection %d GameUserIndex %d"),
		bIsConnection, GameUserIndex);

	if(!bIsConnection)
	{
		// Controller was disconnected

		// Find the local player associated with this user index
		ULocalPlayer * LocalPlayer = FindLocalPlayerFromControllerId( GameUserIndex );

		if ( LocalPlayer == NULL )
		{
			return;		// We don't care about players we aren't tracking
		}

		// Invalidate this local player's controller id.
		LocalPlayer->SetControllerId(-1);
	}
}

FReply UBattleRoyaleInstance::OnControllerReconnectConfirm()
{
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());
	if(ShooterViewport)
	{
		ShooterViewport->HideDialog();
	}

	return FReply::Handled();
}

TSharedPtr< const FUniqueNetId > UBattleRoyaleInstance::GetUniqueNetIdFromControllerId( const int ControllerId )
{
	IOnlineIdentityPtr OnlineIdentityInt = Online::GetIdentityInterface();

	if ( OnlineIdentityInt.IsValid() )
	{
		TSharedPtr<const FUniqueNetId> UniqueId = OnlineIdentityInt->GetUniquePlayerId( ControllerId );

		if ( UniqueId.IsValid() )
		{
			return UniqueId;
		}
	}

	return nullptr;
}

void UBattleRoyaleInstance::SetIsOnline(bool bInIsOnline)
{
	bIsOnline = bInIsOnline;

#if !PLATFORM_SWITCH
	// The Switch has different timings for when we're considered to be using multiplayer features.
	UpdateUsingMultiplayerFeatures(bIsOnline);
#endif
}

void UBattleRoyaleInstance::UpdateUsingMultiplayerFeatures(bool bIsUsingMultiplayerFeatures)
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();

	if (OnlineSub)
	{
		for (int32 i = 0; i < LocalPlayers.Num(); ++i)
		{
			ULocalPlayer* LocalPlayer = LocalPlayers[i];

			TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId();
			if (PlayerId.IsValid())
			{
				OnlineSub->SetUsingMultiplayerFeatures(*PlayerId, bIsUsingMultiplayerFeatures);
			}
		}
	}
}

void UBattleRoyaleInstance::TravelToSession(const FName& SessionName)
{
	// Added to handle failures when joining using quickmatch (handles issue of joining a game that just ended, i.e. during game ending timer)
	AddNetworkFailureHandlers();
	ShowLoadingScreen();
	GotoState(BattleRoyaleInstanceState::Playing);
	InternalTravelToSession(SessionName);
}

void UBattleRoyaleInstance::SetIgnorePairingChangeForControllerId( const int32 ControllerId )
{
	IgnorePairingChangeForControllerId = ControllerId;
}

bool UBattleRoyaleInstance::IsLocalPlayerOnline(ULocalPlayer* LocalPlayer)
{
	if (LocalPlayer == NULL)
	{
		return false;
	}
	const auto OnlineSub = IOnlineSubsystem::Get();
	if(OnlineSub)
	{
		const auto IdentityInterface = OnlineSub->GetIdentityInterface();
		if(IdentityInterface.IsValid())
		{
			auto UniqueId = LocalPlayer->GetCachedUniqueNetId();
			if (UniqueId.IsValid())
			{
				const auto LoginStatus = IdentityInterface->GetLoginStatus(*UniqueId);
				if(LoginStatus == ELoginStatus::LoggedIn)
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool UBattleRoyaleInstance::IsLocalPlayerSignedIn(ULocalPlayer* LocalPlayer)
{
	if (LocalPlayer == NULL)
	{
		return false;
	}

	const auto OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		const auto IdentityInterface = OnlineSub->GetIdentityInterface();
		if (IdentityInterface.IsValid())
		{
			auto UniqueId = LocalPlayer->GetCachedUniqueNetId();
			if (UniqueId.IsValid())
			{
				return true;
			}
		}
	}

	return false;
}

bool UBattleRoyaleInstance::ValidatePlayerForOnlinePlay(ULocalPlayer* LocalPlayer)
{
	// Get the viewport
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

#if PLATFORM_XBOXONE
	if (CurrentConnectionStatus != EOnlineServerConnectionStatus::Connected)
	{
		// Don't let them play online if they aren't connected to Xbox LIVE
		if (ShooterViewport != NULL)
		{
			const FText Msg				= NSLOCTEXT("NetworkFailures", "ServiceDisconnected", "You must be connected to the Xbox LIVE service to play online.");
			const FText OKButtonString	= NSLOCTEXT("DialogButtons", "OKAY", "OK");

			ShooterViewport->ShowDialog( 
				NULL,
				EShooterDialogType::Generic,
				Msg,
				OKButtonString,
				FText::GetEmpty(),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric)
			);
		}

		return false;
	}
#endif

	if (!IsLocalPlayerOnline(LocalPlayer))
	{
		// Don't let them play online if they aren't online
		if (ShooterViewport != NULL)
		{
			const FText Msg				= NSLOCTEXT("NetworkFailures", "MustBeSignedIn", "You must be signed in to play online");
			const FText OKButtonString	= NSLOCTEXT("DialogButtons", "OKAY", "OK");

			ShooterViewport->ShowDialog( 
				NULL,
				EShooterDialogType::Generic,
				Msg,
				OKButtonString,
				FText::GetEmpty(),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric)
			);
		}

		return false;
	}

	return true;
}

bool UBattleRoyaleInstance::ValidatePlayerIsSignedIn(ULocalPlayer* LocalPlayer)
{
	// Get the viewport
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());

	if (!IsLocalPlayerSignedIn(LocalPlayer))
	{
		// Don't let them play online if they aren't online
		if (ShooterViewport != NULL)
		{
			const FText Msg = NSLOCTEXT("NetworkFailures", "MustBeSignedIn", "You must be signed in to play online");
			const FText OKButtonString = NSLOCTEXT("DialogButtons", "OKAY", "OK");

			ShooterViewport->ShowDialog(
				NULL,
				EShooterDialogType::Generic,
				Msg,
				OKButtonString,
				FText::GetEmpty(),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric)
			);
		}

		return false;
	}

	return true;
}


FReply UBattleRoyaleInstance::OnConfirmGeneric()
{
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());
	if(ShooterViewport)
	{
		ShooterViewport->HideDialog();
	}

	return FReply::Handled();
}

void UBattleRoyaleInstance::StartOnlinePrivilegeTask(const IOnlineIdentity::FOnGetUserPrivilegeCompleteDelegate& Delegate, EUserPrivileges::Type Privilege, TSharedPtr< const FUniqueNetId > UserId)
{
	WaitMessageWidget = SNew(SShooterWaitDialog)
		.MessageText(NSLOCTEXT("NetworkStatus", "CheckingPrivilegesWithServer", "Checking privileges with server.  Please wait..."));

	if (GEngine && GEngine->GameViewport)
	{
		UGameViewportClient* const GVC = GEngine->GameViewport;
		GVC->AddViewportWidgetContent(WaitMessageWidget.ToSharedRef());
	}

	auto Identity = Online::GetIdentityInterface();
	if (Identity.IsValid() && UserId.IsValid())
	{		
		Identity->GetUserPrivilege(*UserId, Privilege, Delegate);
	}
	else
	{
		// Can only get away with faking the UniqueNetId here because the delegates don't use it
		Delegate.ExecuteIfBound(FUniqueNetIdString(), Privilege, (uint32)IOnlineIdentity::EPrivilegeResults::NoFailures);
	}
}

void UBattleRoyaleInstance::CleanupOnlinePrivilegeTask()
{
	if (GEngine && GEngine->GameViewport && WaitMessageWidget.IsValid())
	{
		UGameViewportClient* const GVC = GEngine->GameViewport;
		GVC->RemoveViewportWidgetContent(WaitMessageWidget.ToSharedRef());
	}
}

void UBattleRoyaleInstance::DisplayOnlinePrivilegeFailureDialogs(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, uint32 PrivilegeResults)
{	
	// Show warning that the user cannot play due to age restrictions
	UBattleRoyaleViewportClient * ShooterViewport = Cast<UBattleRoyaleViewportClient>(GetGameViewportClient());
	TWeakObjectPtr<ULocalPlayer> OwningPlayer;
	if (GEngine)
	{
		for (auto It = GEngine->GetLocalPlayerIterator(GetWorld()); It; ++It)
		{
			TSharedPtr<const FUniqueNetId> OtherId = (*It)->GetPreferredUniqueNetId();
			if (OtherId.IsValid())
			{
				if (UserId == (*OtherId))
				{
					OwningPlayer = *It;
				}
			}
		}
	}
	
	if (ShooterViewport != NULL && OwningPlayer.IsValid())
	{
		if ((PrivilegeResults & (uint32)IOnlineIdentity::EPrivilegeResults::AccountTypeFailure) != 0)
		{
			IOnlineExternalUIPtr ExternalUI = Online::GetExternalUIInterface();
			if (ExternalUI.IsValid())
			{
				ExternalUI->ShowAccountUpgradeUI(UserId);
			}
		}
		else if ((PrivilegeResults & (uint32)IOnlineIdentity::EPrivilegeResults::RequiredSystemUpdate) != 0)
		{
			ShooterViewport->ShowDialog(
				OwningPlayer.Get(),
				EShooterDialogType::Generic,
				NSLOCTEXT("OnlinePrivilegeResult", "RequiredSystemUpdate", "A required system update is available.  Please upgrade to access online features."),
				NSLOCTEXT("DialogButtons", "OKAY", "OK"),
				FText::GetEmpty(),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric)
				);
		}
		else if ((PrivilegeResults & (uint32)IOnlineIdentity::EPrivilegeResults::RequiredPatchAvailable) != 0)
		{
			ShooterViewport->ShowDialog(
				OwningPlayer.Get(),
				EShooterDialogType::Generic,
				NSLOCTEXT("OnlinePrivilegeResult", "RequiredPatchAvailable", "A required game patch is available.  Please upgrade to access online features."),
				NSLOCTEXT("DialogButtons", "OKAY", "OK"),
				FText::GetEmpty(),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric)
				);
		}
		else if ((PrivilegeResults & (uint32)IOnlineIdentity::EPrivilegeResults::AgeRestrictionFailure) != 0)
		{
			ShooterViewport->ShowDialog(
				OwningPlayer.Get(),
				EShooterDialogType::Generic,
				NSLOCTEXT("OnlinePrivilegeResult", "AgeRestrictionFailure", "Cannot play due to age restrictions!"),
				NSLOCTEXT("DialogButtons", "OKAY", "OK"),
				FText::GetEmpty(),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric)
				);
		}
		else if ((PrivilegeResults & (uint32)IOnlineIdentity::EPrivilegeResults::UserNotFound) != 0)
		{
			ShooterViewport->ShowDialog(
				OwningPlayer.Get(),
				EShooterDialogType::Generic,
				NSLOCTEXT("OnlinePrivilegeResult", "UserNotFound", "Cannot play due invalid user!"),
				NSLOCTEXT("DialogButtons", "OKAY", "OK"),
				FText::GetEmpty(),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric)
				);
		}
		else if ((PrivilegeResults & (uint32)IOnlineIdentity::EPrivilegeResults::GenericFailure) != 0)
		{
			ShooterViewport->ShowDialog(
				OwningPlayer.Get(),
				EShooterDialogType::Generic,
				NSLOCTEXT("OnlinePrivilegeResult", "GenericFailure", "Cannot play online.  Check your network connection."),
				NSLOCTEXT("DialogButtons", "OKAY", "OK"),
				FText::GetEmpty(),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric),
				FOnClicked::CreateUObject(this, &UBattleRoyaleInstance::OnConfirmGeneric)
				);
		}
	}
}

void UBattleRoyaleInstance::OnRegisterLocalPlayerComplete(const FUniqueNetId& PlayerId, EOnJoinSessionCompleteResult::Type Result)
{
	FinishSessionCreation(Result);
}

void UBattleRoyaleInstance::FinishSessionCreation(EOnJoinSessionCompleteResult::Type Result)
{
	if (Result == EOnJoinSessionCompleteResult::Success)
	{
		// This will send any Play Together invites if necessary, or do nothing.
		SendPlayTogetherInvites();

		// Travel to the specified match URL
		GetWorld()->ServerTravel(TravelURL);
	}
	else
	{
		FText ReturnReason = NSLOCTEXT("NetworkErrors", "CreateSessionFailed", "Failed to create session.");
		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
	}
}

void UBattleRoyaleInstance::BeginHostingQuickMatch()
{
	ShowLoadingScreen();
	GotoState(BattleRoyaleInstanceState::Playing);

	// Travel to the specified match URL
	GetWorld()->ServerTravel(TEXT("/Game/Maps/Highrise?game=TDM?listen"));	
}

void UBattleRoyaleInstance::OnPlayTogetherEventReceived(const int32 UserIndex, const TArray<TSharedPtr<const FUniqueNetId>>& UserIdList)
{
	PlayTogetherInfo = FShooterPlayTogetherInfo(UserIndex, UserIdList);

	const IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get();
	check(OnlineSub);

	const IOnlineSessionPtr SessionInterface = OnlineSub->GetSessionInterface();
	check(SessionInterface.IsValid());

	// If we have available slots to accomedate the whole party in our current sessions, we should send invites to the existing one
	// instead of a new one according to Sony's best practices.
	const FNamedOnlineSession* const Session = SessionInterface->GetNamedSession(GameSessionName);
	if (Session != nullptr && Session->NumOpenPrivateConnections + Session->NumOpenPublicConnections >= UserIdList.Num())
	{
		SendPlayTogetherInvites();
	}
	// Always handle Play Together in the main menu since the player has session customization options.
	else if (CurrentState == BattleRoyaleInstanceState::MainMenu)
	{
		MainMenuUI->OnPlayTogetherEventReceived();
	}
	else if (CurrentState == BattleRoyaleInstanceState::WelcomeScreen)
	{
		StartOnlinePrivilegeTask(IOnlineIdentity::FOnGetUserPrivilegeCompleteDelegate::CreateUObject(this, &UBattleRoyaleInstance::OnUserCanPlayTogether), EUserPrivileges::CanPlayOnline, PendingInvite.UserId);
	}
	else
	{
		GotoState(BattleRoyaleInstanceState::MainMenu);
	}
}

void UBattleRoyaleInstance::SendPlayTogetherInvites()
{
	const IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get();
	check(OnlineSub);

	const IOnlineSessionPtr SessionInterface = OnlineSub->GetSessionInterface();
	check(SessionInterface.IsValid());

	if (PlayTogetherInfo.UserIndex != -1)
	{
		for (const ULocalPlayer* LocalPlayer : LocalPlayers)
		{
			if (LocalPlayer->GetControllerId() == PlayTogetherInfo.UserIndex)
			{
				TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId();
				if (PlayerId.IsValid())
				{
					// Automatically send invites to friends in the player's PS4 party to conform with Play Together requirements
					for (const TSharedPtr<const FUniqueNetId>& FriendId : PlayTogetherInfo.UserIdList)
					{
						SessionInterface->SendSessionInviteToFriend(*PlayerId.ToSharedRef(), GameSessionName, *FriendId.ToSharedRef());
					}
				}

			}
		}

		PlayTogetherInfo = FShooterPlayTogetherInfo();
	}
}