// Copyright 2026 Jack McGuigan. All rights reserved.
// Main menu ("Death's Office, night shift"), drawn with the HUD canvas (no widgets):
//   START REAPING (host the pending mission as a listen server), JOIN A FRIEND (type an IP), MISSIONS (map cards, hard mode),
//   REAPERS (gallery: hire / pick your reaper), UPGRADES (permanent meta shop), QUESTS, SETTINGS (the shared FSQSettingsPanel, same
//   as P in a run), HOW TO PLAY (the Training Shift tips + PLAY TRAINING SHIFT), CREDITS, QUIT.
// Your reaper's ID card on the right, key hints along the bottom, the 3D graveyard nook (SQMenuShowroom.h) behind it all.
// Keyboard (Up/Down/Left/Right, Enter, Esc), mouse and gamepad (D-pad / left stick, A, B).
// The /Game/Maps/MainMenu map (Scripts/build_menu_map.py) sets ASQMenuGameMode in its World Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "SQSave.h"
#include "SQMenu.generated.h"

class ASQMenuShowroom;
class UAudioComponent;
class UFont;
class UFontFace;
class USQSaveGame;
struct FKey;
struct FSQReaperDef;

/** Menu map game mode: no pawn, the menu controller and HUD. */
UCLASS()
class SOULQUOTA_API ASQMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASQMenuGameMode();

	/** Nobody spawns in the menu. */
	virtual void RestartPlayer(AController* NewPlayer) override {}
};

/** Shows the mouse cursor, game-and-UI input; the HUD reads the keys. */
UCLASS()
class SOULQUOTA_API ASQMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASQMenuPlayerController();
	virtual void BeginPlay() override;
};

enum class ESQMenuPage : uint8
{
	Main,
	Join,
	Reapers,
	Contracts,
	Settings,
	Credits,
	Quests,
	/** Nov 9: mission select (map cards, hard mode) and the permanent upgrade shop */
	Missions,
	Upgrades
};

UCLASS()
class SOULQUOTA_API ASQMenuHUD : public AHUD
{
	GENERATED_BODY()

public:
	ASQMenuHUD();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;

	/** The map CLOCK IN hosts (long package name), as a listen server. */
	UPROPERTY(EditAnywhere, Category="Menu")
	FString HostMap = TEXT("/Game/Maps/Graveyard");

private:
	// ---------------------------------------------------------------- drawing helpers (1080p design pixels)
	void LoadFonts();
	UFont* MakeFont(UFontFace* Face, int32 Size);
	UFont* DisplayFont() const;
	UFont* HeadFont() const;
	UFont* LabelFont(bool bBold = false) const;
	/** Text with its top at Y, Px = size at 1080p. Align 0 left, 1 centre, 2 right of X. Returns its width. */
	float Txt(const FString& Text, UFont* Font, float Px, float X, float Y, const FLinearColor& Colour, int32 Align = 0, bool bShadow = false);
	float TxtWidth(const FString& Text, UFont* Font, float Px);
	/** Largest size up to Px that fits MaxW. */
	float FitPx(const FString& Text, UFont* Font, float Px, float MaxW);
	/** Word-wrapped text; LineH in screen pixels. Returns the lines used (draws nothing when bDraw is false). */
	int32 TxtWrap(const FString& Text, UFont* Font, float Px, float X, float Y, float MaxW, const FLinearColor& Colour, float LineH, int32 MaxLines,
		bool bDraw = true);
	void Rect(const FLinearColor& Colour, float X, float Y, float W, float H);
	/** Filled rounded rectangle (radius R in screen pixels). */
	void Round(const FLinearColor& Colour, float X, float Y, float W, float H, float R);
	/** Soft glow around a rounded rectangle (Strength 0..1). */
	void GlowAround(const FLinearColor& Colour, float X, float Y, float W, float H, float R, float Strength);
	/** Page panel: shadow, line rim, panel fill. */
	void PanelBox(float X, float Y, float W, float H);
	/** A key cap with its label; returns its width. */
	float KeyCap(const FString& Key, float X, float Y);
	/** A tilted strip / clock hand (rotation around the tile's pivot). */
	void TiltTile(const FLinearColor& Colour, float X, float Y, float W, float H, float Deg, float PivotX, float PivotY);
	/** A padlock icon centred at CX, CY, Size tall. */
	void Padlock(float CX, float CY, float Size, const FLinearColor& Colour);
	/** A horizontal stat bar with its label and value. */
	void StatBar(const FString& Label, const FString& Value, float Fill, float X, float Y, float W, const FLinearColor& Colour);
	bool MouseIn(float X, float Y, float W, float H) const;

	// ---------------------------------------------------------------- input
	void ReadInput(APlayerController* PC);
	bool NavKey(APlayerController* PC, int32 Dir, const FKey* Keys, int32 NumKeys);
	void TypeAddress(APlayerController* PC);

	// ---------------------------------------------------------------- pages
	void OpenPage(ESQMenuPage NewPage);
	void DrawBackdrop();
	void DrawTitleBlock(float X, float Y);
	void DrawMainPage();
	bool DrawMainButton(int32 Index, const FString& Label, const FString& Sub, const FLinearColor& SubColour, float X, float Y, float W, float H,
		bool bPrimary, float Appear);
	bool DrawLinkButton(int32 Index, const FString& Label, float X, float Y, const FLinearColor& Colour, float Appear);
	/** Your reaper's ID card (right side). True when clicked. */
	bool DrawReaperCard(float X, float Y, float W, float Appear);
	/** Page frame: veil, tally strip, back button, breadcrumb and title. True when the back button is clicked. */
	bool DrawPageFrame(const FString& Title, float VeilAlpha);
	/** A rounded button for pages and pop-ups. Style 0 panel, 1 primary (teal with shine), 2 amber, 3 danger.
	 *  FocusIndex = this button's place in FocusVar's list (the mouse moves the focus there; Enter clicks the focused one), -1 = none. */
	bool DrawPageButton(const FString& Label, float X, float Y, float W, float H, int32 Style, bool bEnabled, int32 FocusIndex, int32& FocusVar);
	/** The slanted light sweep across a primary button. */
	void ShineSweep(float X, float Y, float W, float H, float Period, float Inset);
	void DrawJoinPage();
	void DrawReapersPage();
	/** One compact roster card on the REAPERS page: portrait (colour + initial), name, role tag, price / YOUR REAPER. */
	void DrawRosterCard(const FSQReaperDef& Def, int32 Index, float X, float Y, float W, float H, float Lift, bool bLocked, bool bMine, bool bAfford);
	/** The REAPERS page's page strip (only when the roster needs more than one page). Returns the page clicked, or -1. */
	int32 DrawRosterPager(int32 CurrentPage, int32 NumPages, float X, float Y, float W);
	void DrawContractsPage();
	void DrawSettingsPage();
	void DrawCreditsPage();
	/** PERFORMANCE REVIEWS (Quests.csv): category tabs, a two-column list with progress bars and rewards */
	void DrawQuestsPage();
	/** MISSIONS: one map card per mission (USQMissions::GetMissionList), hard mode toggle, sets the pending mission */
	void DrawMissionsPage();
	/** UPGRADES: Death's Office permanent upgrades (USQMissions::GetMetaUpgradeList / BuyMetaUpgrade) */
	void DrawUpgradesPage();
	void DrawQuitConfirm();
	void DrawBottomBar();
	void DrawToast();
	void DrawInkWipe();
	void DrawClockInOverlay();
	/** HOW TO PLAY: the 8 Training Shift tips (Tutorial preview board), PLAY TRAINING SHIFT (arms it and hosts a run) and CLOSE */
	void DrawTutorialLayer();
	/** The shared painter (SQTutorial.h) in this menu's units: design 1080p in, screen pixels out */
	void FillMenuPainter(struct FSQTutorialPainter& Painter);
	bool bHowToOpen = false;
	float HowToOpenedAt = -100.f;
	int32 HowToFocus = 0;
	/** SETTINGS page: the same panel the run opens with P */
	FSQSettingsPanel MenuSettings;

	// ---------------------------------------------------------------- actions and data
	USQSaveGame* GetSave();
	void StoreSave();
	FName PreferredReaper();
	bool IsReaperLocked(FName ReaperId);
	bool IsBiomeOpen(FName BiomeId);
	void ContractCount(int32& OutHave, int32& OutTotal);
	/** bJoin: join Address. Otherwise start the pending mission: solo straight into the run (no lobby), or bHostLobby =
	 *  host a co-op lobby friends can join before the start. */
	void StartTravel(bool bJoin, bool bHostLobby = false);
	void FireTravel();
	void ApplyAudio();
	void PlayUI(const TCHAR* SoundName, float Volume, float Pitch = 1.f);
	void ShowToast(const FString& Text, const FLinearColor& Colour);

	UPROPERTY(Transient)
	TObjectPtr<UFont> CreepsterFont;
	UPROPERTY(Transient)
	TObjectPtr<UFont> LilitaFont;
	UPROPERTY(Transient)
	TObjectPtr<UFont> RubikMedium;
	UPROPERTY(Transient)
	TObjectPtr<UFont> RubikBold;
	UPROPERTY(Transient)
	TObjectPtr<USQSaveGame> Profile;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MenuMusic;

	TWeakObjectPtr<ASQMenuShowroom> Showroom;
	/** Line height of each font at scale 1 (measured once). */
	TMap<const UFont*, float> FontLine;
	bool bFontsLoaded = false;

	/** Per frame: screen pixels per 1080p pixel, menu clock, frame time, and an alpha every helper multiplies in. */
	float UnitPx = 1.f;
	float TimeNow = 0.f;
	float DeltaT = 0.f;
	float DrawAlpha = 1.f;
	float MenuOpenedAt = -1.f;

	ESQMenuPage Page = ESQMenuPage::Main;
	ESQMenuPage PrevPage = ESQMenuPage::Main;
	float PageChangeTime = -100.f;
	bool bQuitAsk = false;
	float QuitAskTime = -100.f;
	int32 QuitFocus = 0;

	// input this frame
	float MouseX = -1.f;
	float MouseY = -1.f;
	bool bMouseMoved = false;
	bool bClick = false;
	bool bMouseHeld = false;
	bool bWheelUp = false;
	bool bWheelDown = false;
	bool bNavUp = false;
	bool bNavDown = false;
	bool bNavLeft = false;
	bool bNavRight = false;
	bool bConfirm = false;
	bool bBack = false;
	bool bUsingPad = false;
	float NavHold[4] = {};
	float BackspaceHold = 0.f;

	// main page
	int32 MainFocus = 0;
	float MainSlide[10] = {};
	int32 NoteFocus = -1;
	float NoteFocusTime = 0.f;
	float CardLift = 0.f;
	/** CLOCK IN's little time clock: the minute hand's angle (spins faster while it's picked). */
	float ClockSpin = 0.f;
	bool bCardHover = false;

	// pages
	int32 PageFocus = 0;
	/** Per reaper card hover / focus lift (sized to the roster every frame, any number of reapers). */
	TArray<float> ReaperLift;
	int32 ReaperHoverCard = -1; // card under the mouse: highlight only (the showroom changes on a click / keys)
	/** REAPERS roster paging: the page on screen (-1 = snap to the focused card's page), when it last changed and which way. */
	int32 ReaperPageShown = -1;
	float ReaperPageTime = -100.f;
	float ReaperPageDir = 0.f;
	/** The roster has more cards than fit on one page (the bottom bar shows the page keys). */
	bool bReaperPaged = false;
	float ContractLift[8] = {};
	/** MISSIONS page: card lift, the hard mode switch for the focused card */
	float MissionLift[8] = {};
	bool bMissionHard = false;
	/** UPGRADES page: row lift and the first visible row; when a purchase last flashed */
	TArray<float> UpgradeLift;
	int32 UpgradeRow = 0;
	int32 UpgradeFlash = -1;
	float UpgradeFlashTime = -100.f;
	bool bSettingsDirty = false;
	float CreditsScroll = 0.f;
	/** QUESTS page: the category tab (0 = all) and the first visible row */
	int32 QuestTab = 0;
	int32 QuestRow = 0;
	FString Address;

	// toast
	FString ToastText;
	FLinearColor ToastColour = FLinearColor::White;
	float ToastTime = -100.f;

	// clocking in (host) / joining: card, stamp, ink, then travel
	bool bTravelling = false;
	bool bTravelJoin = false;
	bool bTravelSolo = false;
	bool bTravelFired = false;
	float TravelStart = -100.f;
	bool bStampSoundPlayed = false;
	bool bTitleStampSound = false;
};
