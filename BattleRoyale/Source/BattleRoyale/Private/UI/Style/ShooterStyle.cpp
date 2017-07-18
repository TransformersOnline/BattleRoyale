// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "BattleRoyale.h"
#include "ShooterStyle.h"
#include "SlateGameResources.h"

TSharedPtr< FSlateStyleSet > FShooterStyle::ShooterStyleInstance = NULL;

void FShooterStyle::Initialize()
{
	if ( !ShooterStyleInstance.IsValid() )
	{
		ShooterStyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle( *ShooterStyleInstance );
	}
}

void FShooterStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle( *ShooterStyleInstance );
	ensure( ShooterStyleInstance.IsUnique() );
	ShooterStyleInstance.Reset();
}

FName FShooterStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("ShooterStyle"));
	return StyleSetName;
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( FPaths::GameContentDir() / "Slate"/ RelativePath + TEXT(".png"), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( FPaths::GameContentDir() / "Slate"/ RelativePath + TEXT(".png"), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( FPaths::GameContentDir() / "Slate"/ RelativePath + TEXT(".png"), __VA_ARGS__ )
#define TTF_FONT( RelativePath, ... ) FSlateFontInfo( FPaths::GameContentDir() / "Slate"/ RelativePath + TEXT(".ttf"), __VA_ARGS__ )
#define OTF_FONT( RelativePath, ... ) FSlateFontInfo( FPaths::GameContentDir() / "Slate"/ RelativePath + TEXT(".otf"), __VA_ARGS__ )

TSharedRef< FSlateStyleSet > FShooterStyle::Create()
{
	TSharedRef<FSlateStyleSet> StyleRef = FSlateGameResources::New(FShooterStyle::GetStyleSetName(), "/Game/UI/Styles", "/Game/UI/Styles");
	FSlateStyleSet& Style = StyleRef.Get();

	// Load the speaker icon to be used for displaying when a user is talking
	Style.Set("BattleRoyale.Speaker", new IMAGE_BRUSH("Images/SoundCue_SpeakerIcon", FVector2D(32, 32)));

	// The border image used to draw the replay timeline bar
	Style.Set("BattleRoyale.ReplayTimelineBorder", new BOX_BRUSH("Images/ReplayTimeline", FMargin(3.0f / 8.0f)));

	// The border image used to draw the replay timeline bar
	Style.Set("BattleRoyale.ReplayTimelineIndicator", new IMAGE_BRUSH("Images/ReplayTimelineIndicator", FVector2D(4.0f, 26.0f)));

	// The image used to draw the replay pause button
	Style.Set("BattleRoyale.ReplayPauseIcon", new IMAGE_BRUSH("Images/ReplayPause", FVector2D(32.0f, 32.0f)));

	// Fonts still need to be specified in code for now
	Style.Set("BattleRoyale.MenuServerListTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Black", 14))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FIntPoint(-1,1))
		);

	Style.Set("BattleRoyale.ScoreboardListTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Black", 14))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FIntPoint(-1,1))
		);

	Style.Set("BattleRoyale.MenuProfileNameStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Black", 18))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FIntPoint(-1,1))
		);

	Style.Set("BattleRoyale.MenuTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Black", 20))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FIntPoint(-1,1))
		);

	Style.Set("BattleRoyale.MenuHeaderTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Black", 26))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FIntPoint(-1,1))
		);

	Style.Set("BattleRoyale.WelcomeScreen.WelcomeTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Medium", 32))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FIntPoint(-1,1))
		);

	Style.Set("BattleRoyale.DefaultScoreboard.Row.HeaderTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Black", 24))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FVector2D(0,1))
		);

	Style.Set("BattleRoyale.DefaultScoreboard.Row.StatTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Regular", 18))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FVector2D(0,1))
		);

	Style.Set("BattleRoyale.SplitScreenLobby.StartMatchTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Regular", 16))
		.SetColorAndOpacity(FLinearColor::Green)
		.SetShadowOffset(FVector2D(0,1))
		);

	Style.Set("BattleRoyale.DemoListCheckboxTextStyle", FTextBlockStyle()
		.SetFont(TTF_FONT("Fonts/Roboto-Black", 12))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FIntPoint(-1,1))
		);

	Style.Set("BattleRoyale.Switch.Left", FInlineTextImageStyle()
		.SetImage(IMAGE_BRUSH("Images/SwitchButtonLeft", FVector2D(32, 32)))
		);

	Style.Set("BattleRoyale.Switch.Right", FInlineTextImageStyle()
		.SetImage(IMAGE_BRUSH("Images/SwitchButtonRight", FVector2D(32, 32)))
		);

	Style.Set("BattleRoyale.Switch.Up", FInlineTextImageStyle()
		.SetImage(IMAGE_BRUSH("Images/SwitchButtonUp", FVector2D(32, 32)))
		);

	Style.Set("BattleRoyale.Switch.Down", FInlineTextImageStyle()
		.SetImage(IMAGE_BRUSH("Images/SwitchButtonDown", FVector2D(32, 32)))
		);

	return StyleRef;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT

void FShooterStyle::ReloadTextures()
{
	FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}

const ISlateStyle& FShooterStyle::Get()
{
	return *ShooterStyleInstance;
}
