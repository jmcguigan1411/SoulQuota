// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SQSave.h"
#include "SQHUD.generated.h"

class UFont;
class UFontFace;
class ASQGameState;
class ASQPlayerState;
class ASQCharacter;
class ASQPlayerController;
class USQData;
class UTexture;
class UTextureRenderTarget2D;
class UTexture2D;
class ASQArena;
class ASQMenuShowroom;
struct FSQUpgradeDef;
struct FSQReaperDef;
struct FSQOwned;
struct FSQAbilityView;

/** A small "+N" number that floats up from the quota bar when souls are banked. */
struct FSQHudFloater
{
	FString Label;
	FLinearColor Colour = FLinearColor::White;
	float Born = 0.f;
	float Jitter = 0.f;
};

/**
 * In-game Canvas HUD, in the same visual language as the main menu (SQUIStyle.h palette,
 * runtime Lilita One / Rubik / Creepster fonts, rounded panels, glow, eased animation).
 * Everything is authored in a 1080p design space and multiplied by UIScale when drawn.
 *
 * Layout (Nov 9 remaster, top-down; the middle of the screen stays clear for walking):
 *   bottom-centre  the ability bar (HUD preview gen2.py): portrait in a gold ring that fills with the Ultimate charge, HP (ghost
 *                  trail) + XP / level, the signature weapon and weapon slots (level pips), the tome slots, then the action slots
 *                  L REAP / E / R (charge %) / SPC DASH, each with its key and cooldown; owned augments as gold diamonds above it
 *   centre         MANUAL aim only: a small crosshair; the "xN REAP STREAK" counter sits under the screen centre (the old
 *                  ground cone and the cooldown ring round the cursor went with the overhead camera, Nov 12 2026)
 *   top-centre     the mission / stage timer pill (boss bar under it during a boss), alert toasts below
 *   top-right      objective tracker (ASQGameState::Objectives; the old quota as one objective when there are none)
 *   top-left       gold counter, co-op teammate mini-cards (a downed teammate: "DOWN - reap N near them" + progress)
 *   overlay        SETTINGS (P): FSQSettingsPanel over a veil (pauses in solo, see ASQPlayerController::SetSettingsOpen)
 *   screen edge    arrows to active objectives that are off screen
 *   centre         level-up picking (rarity cards) and AUGMENT offers (gold-framed cards), victory / defeat with run stats
 *   lobby          full-screen reaper select (TAB folds it to a pill to see the lobby): a 3D centre-stage preview (scene
 *                  capture of an ASQMenuShowroom studio far above the arena), the kit panel (weapon / passive / E / R) and
 *                  stat bars on the left, mission + crew + READY on the right, portrait cards along the bottom
 */
UCLASS()
class SOULQUOTA_API ASQHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;

	/** The cursor is over something clickable the HUD drew last frame (cards, tutorial skips, tips, settings): left-click won't reap */
	bool IsMouseOverClickable() const;

private:
	// ---------------- audio director
	void UpdateAudio();
	void Play(const TCHAR* Name, float Volume = 1.f, float PitchJitter = 0.08f);

	UPROPERTY() TObjectPtr<class UAudioComponent> Music;
	uint8 PrevPhase = 255;
	int32 PrevLevel = 1;
	int32 PrevCarried = 0;
	int32 PrevBanked = 0;
	int32 PrevDeaths = 0;
	float PrevHitFlash = 0.f;
	float PrevDash = 0.f;
	float LastSoulSound = 0.f;
	float LastKillSound = 0.f;

	// ---------------- frame state
	/** Design-space -> pixel multiplier, recomputed every DrawHUD. */
	float UIScale = 1.f;
	/** Design-space screen size. */
	float HudVW = 1920.f;
	float HudVH = 1080.f;
	/** Real time (keeps animating when paused) and a clamped frame delta. */
	float HudNow = 0.f;
	float HudDelta = 0.f;
	/** Multiplies every colour's alpha (fades whole groups). */
	float HudAlpha = 1.f;

	// ---------------- fonts (runtime UFonts from the imported font faces, like the menu)
	UPROPERTY() TObjectPtr<UFont> HudCreepster;
	UPROPERTY() TObjectPtr<UFont> HudLilita;
	UPROPERTY() TObjectPtr<UFont> HudRubikMedium;
	UPROPERTY() TObjectPtr<UFont> HudRubikBold;
	TMap<const UFont*, float> HudFontLine;
	bool bHudFontsLoaded = false;

	// ---------------- smoothed numbers
	float HPShown = -1.f;
	float HPGhost = -1.f;
	float HPGhostHold = 0.f;
	float HPLast = -1.f;
	float SoulsShown = 0.f;
	float SoulsPulseAt = -10.f;
	int32 SoulsSeen = 0;
	float QuotaShown = 0.f;
	float XPShown = 0.f;
	float BossShown = -1.f;
	float BossGhost = -1.f;
	float BossGhostHold = 0.f;
	float BossLast = -1.f;
	int32 XPLevelSeen = -1;

	// ---------------- floaters and banners
	TArray<FSQHudFloater> Floaters;
	int32 FloaterBanked = -1;
	float QuotaAnchorX = 960.f;
	float QuotaAnchorY = 100.f;
	uint8 HudPhaseSeen = 255;
	float PhaseBannerAt = -100.f;
	/** 0 none, 1 a night starts, 2 midnight (boss). */
	uint8 PhaseBannerKind = 0;
	/** When the lobby turned into the first night locally (the "REAP!" flash). */
	float ReapAt = -100.f;
	FString GimmickSeen;
	float GimmickSeenAt = -100.f;

	// ---------------- level-up cards
	uint32 OfferSignature = 0;
	float OffersShownAt = -100.f;
	int32 PickFlashIndex = -1;
	float PickFlashAt = -100.f;
	float OfferLift[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	float RerollLift = 0.f;
	float SkipLift = 0.f;
	float RerollFlashAt = -100.f;
	float SkipFlashAt = -100.f;
	/** Clicks are ignored until then (one pick per click, no double sends) */
	float PickClickLockUntil = -100.f;
	/** 0..1 world dim behind the cards / the waiting panel */
	float PickVeil = 0.f;
	float WaitShownAt = -100.f;
	/** 0 no pause, 1 paused and you're choosing, 2 paused and you're waiting on teammates (panel fade-in) */
	uint8 PickWaitMode = 0;

	// ---------------- shop
	int32 ShopFlashIndex = -1;
	float ShopFlashAt = -100.f;
	float ShopShownAt = -100.f;
	float TeamPanelBottom = 0.f;

	// ---------------- lobby dock
	bool bLobbyExpanded = true;
	float LobbyOpen = 1.f;
	float LobbyShownAt = -100.f;
	/** Reaper grid: the tile with keyboard / pad / mouse focus (-1 = snap to your reaper next frame) */
	int32 LobbyFocus = -1;
	int32 LobbyHover = -1; // card under the mouse: highlight only (focus / showroom change on click or keys)
	/** Your reaper when the focus last snapped to it (a quick pick or a server change moves the focus along) */
	FName LobbyMineSeen;
	/** Mouse position last frame in design units (hover moves the focus only when the mouse moves) */
	float LobbyMouseX = -1.f;
	float LobbyMouseY = -1.f;
	/** A locked reaper waiting for a second Enter / click to confirm spending coins on them (-1 none) */
	int32 LobbyHireArmed = -1;
	float LobbyHireArmedAt = -100.f;
	FString LobbyInviteCode;
	FString LobbyInviteAddress;
	float LobbyInviteCopiedAt = -100.f;
	/** Per tile focus lift (sized to the roster, any number of reapers) */
	TArray<float> LobbyTileLift;

	// ---------------- end screen
	float EndShownAt = -100.f;
	/** RETRY / CHANGE REAPER hover lifts, and the choice already sent (no double sends) */
	float EndBtnLift[2] = { 0.f, 0.f };
	// The Long Haunt (Nov 24 2026): the cash out / keep reaping card after a boss
	void DrawLoopChoice(const ASQGameState* GS, const USQData* D);
	float LoopHoldFor[2] = { 0.f, 0.f };
	float LoopChoiceShownAt = -1.f;
	bool bLoopChoiceSent = false;
	/** End screen: how long RETRY / CHANGE REAPER keys have been held (hold to confirm) */
	float EndHoldFor[2] = { 0.f, 0.f };
	bool bEndChoiceSent = false;
	/** Unlock strip: quests just finished (NEW) then the closest unfinished ones; rebuilt once per end screen */
	struct FEndUnlock { FString Name; FString Reward; float Frac = 0.f; bool bNew = false; };
	TArray<FEndUnlock> EndUnlocks;
	float EndUnlocksBuiltFor = -200.f;
	void BuildEndUnlocks(const USQData* D);
	/** Solo PLAY / RETRY: a short card in place of the lobby */
	void DrawSkipLobby(const ASQGameState* GS);

	// ---------------- sections
	/** Mission / stage timer pill (old night clock as a fallback), the boss bar under it; returns the bottom */
	float DrawTopCentre(const ASQGameState* GS, const USQData* D);
	/** Nov 25 2026 run structure: overflow banner, soul rift marker + prompt, passage card, endless kill counter (Docs/RunStructure.md) */
	float DrawRunOverlay(const ASQGameState* GS, const ASQCharacter* C, float Y);
	/** Boss name + health + phase pips; returns the bottom */
	float DrawBossBar(const ASQGameState* GS, const USQData* D, float Y);
	float DrawToast(const FString& Msg, float Y, const FLinearColor& Accent, float Age, float Life);
	void DrawBanking(const ASQCharacter* C, bool bCardsUp);
	void DrawPhaseBanner(const ASQGameState* GS);
	void DrawLevelUp(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D, ASQPlayerController* SQPC);
	/** Co-op level-up strip (no freeze): a pulsing pill mid-fight, 3 small cards at the bottom when expanded */
	void DrawLevelUpStrip(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D, ASQPlayerController* SQPC);
	uint32 StripSignature = 0;
	float StripShownAt = -100.f;
	float StripOpenAt = -100.f;
	bool bStripWasOpen = false;
	int32 StripFlashIndex = -1;
	float StripFlashAt = -100.f;
	void DrawPickStatus(const ASQGameState* GS, const ASQPlayerState* PS, bool bCompact);
	void DrawPickCountdown(const ASQGameState* GS);
	void DrawShop(const ASQGameState* GS, const ASQPlayerState* PS, const USQData* D);
	void DrawEndScreen(const ASQGameState* GS, const ASQPlayerState* PS);
	void DrawLobby(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D, ASQPlayerController* SQPC);
	void DrawLobbyHints();
	/** Lobby grid pick (Enter / Space / A / click): picks a hired reaper; a locked one you can afford asks for a second press, then hires */
	void LobbyPickReaper(int32 Index, const USQData* D, ASQPlayerController* SQPC);
	void DrawFloaters();
	/** Floating damage numbers (white / yellow crit / red huge / green heal) from the ASQSparks pool, projected to the screen */
	void DrawDamageNumbers();
	bool bShowDamageNumbers = true;
	float DamageNumbersToggledAt = -100.f;
	void DrawStartCountdown(const ASQGameState* GS);
	void DrawScreenEdges(const ASQCharacter* C);

	// ---------------- economy pass (Nov 7): gold, chests, Appointment Bell, timeline beats, Final Swarm, quests, clauses
	/** Under the reaper card: your gold, the price of your next chest, your Skeleton Key chance */
	void DrawGoldPanel(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, float X, float Y);
	/** World-space chest prices and the bell marker (edge arrow when off screen), plus the E prompt for the nearest one */
	void DrawWorldMarkers(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D);
	/** Big centre banner for a timeline beat (miniboss / swarm / Final Swarm / bell ready) */
	void DrawBeatBanner(const ASQGameState* GS);
	/** Name + health bar of the living timeline miniboss; returns the height used */
	float DrawMinibossBar(const ASQGameState* GS, float Y);
	/** Run events (content item 12): the running event's name, time left and hold bar under the miniboss bar. Returns the height used. */
	float DrawEventTracker(const ASQGameState* GS, float Y);
	/** Quest-complete / chest / notice cards (local to this PC), right side under the team panel */
	void DrawLocalToasts(const ASQPlayerState* PS);
	/** Lobby: the Contract Clauses (challenge modifiers) list; the host clicks to toggle */
	void DrawClausePanel(const ASQGameState* GS, ASQPlayerState* PS, float X, float Y);

	// ---------------- tutorial (SQTutorial.h): the Training Shift card and one-time tips in a run (drawn last, on top)
	/** Hands the tutorial this HUD's drawing helpers and anchors (reaper, nearest foe, portrait, objective) and runs it */
	void DrawTutorialLayer(bool bLobby);
	/** Fills the painter callbacks every HUD-drawn shared panel uses (tutorial, settings) */
	void FillPainter(struct FSQTutorialPainter& Painter);

	// ---------------- Nov 9 UI pass: reap reticle, settings overlay, click zones
	/** Reap cone (ground, toward the cursor), cooldown ring round the cursor, "xN REAP STREAK" */
	void DrawReapReticle(const ASQGameState* GS, const ASQCharacter* C, ASQPlayerController* SQPC);
	/** P: the settings panel over a veil */
	void DrawSettingsOverlay(ASQPlayerController* SQPC);
	/** A straight line in design units (a thin rotated tile) */
	void HudLine(float X0, float Y0, float X1, float Y1, float Thick, const FLinearColor& Colour);
	void AddClickZone(float X, float Y, float W, float H) { ClickZones.Add(FVector4f(X, Y, W, H)); }
	/** Clickable rects (design units) drawn by the last DrawHUD (input runs before the next one, so it reads the frame on screen) */
	TArray<FVector4f> ClickZones;
	FSQSettingsPanel SettingsPanel;
public:
	/** SETTINGS > CONTROLS is waiting for a key: the controller must not treat Esc / P / B as "close" */
	bool IsCapturingKey() const { return SettingsPanel.IsCapturingKey(); }
private:
	bool bSettingsWasOpen = false;
	float MusicVolumeSeen = -1.f;
	/** Tutorial anchors found while drawing (design units; < 0 = none this frame) */
	float PortraitAnchorX = -1.f, PortraitAnchorY = -1.f;
	float ObjectiveAnchorX = -1.f, ObjectiveAnchorY = -1.f;
	float StripAnchorX = -1.f, StripAnchorY = -1.f;
	/** Ultimate ring: when it last became full (pulse) */
	float UltFullAt = -100.f;
	bool bUltWasFull = false;
	float GoldShown = 0.f;
	int32 GoldSeen = 0;
	float GoldPulseAt = -10.f;
	int32 LocalToastsSeen = 0;
	double LastToastTimeSeen = 0.0;
	float BellPromptLift = 0.f;

	// ---------------- Nov 9 remaster: ability bar, objectives, teammates, augments, lobby studio
	/** Bottom-centre ability bar; returns the design height it takes from the bottom of the screen */
	float DrawAbilityBar(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D);
	void DrawLoadoutChip(const FSQOwned* Own, const USQData* D, float X, float Y, float ChipS, bool bSignature);
	/**
	 * One square action slot (HUD preview): key letter (or the icon when key hints are off), a small status line (REAP / 3s / 72% / DASH),
	 * accent rim when ready, a dark cooldown curtain, a lock + "LV n" until unlocked. FlashSlot 0..3 = which slot (ready flash).
	 */
	void DrawActionSlot(const FString& KeyName, const FString& IconText, const FString& SubText, float X, float Y, float S, const FLinearColor& Accent,
		bool bReady, bool bLocked, float CooldownLeft01, int32 FlashSlot, bool bEvolved);
	/** Top-right objective tracker; returns its bottom */
	float DrawObjectives(const ASQGameState* GS, const USQData* D);
	void DrawObjectiveArrows(const ASQGameState* GS, const ASQCharacter* C);
	/** Co-op pass (Nov 23 2026): pings (marker / edge arrow), the ping wheel, the COMBO callout, the ghost-shift hint, bonds + Close Ranks chips */
	void DrawCoop(const ASQGameState* GS, const ASQCharacter* C, const class ASQPlayerController* SQPC, const USQData* D);

	// --- Map revamp (Nov 10 2026): minimap (bottom-right, north-up = the fixed camera's screen-up, fog of war) and a
	// full map on M with district names and a legend. The map image is rasterised once per arena build into a small
	// transient texture; the fog is re-composed only when someone reveals new ground (at most 4x a second).
	void DrawMinimap(const ASQGameState* GS, const ASQCharacter* C, const USQData* D, bool bShop);
	void MinimapRebuildBase(const ASQArena* Arena, const USQData* D);
	void MinimapUpdateFog(const ASQGameState* GS, bool bForce);
	void MinimapCompose();
	void MinimapUpload();
	void MinimapRefreshActors(const UWorld* World);
	/** World (cm) -> map UV (0..1, north-up). */
	FVector2D MinimapUV(float X, float Y) const;
	/** Icons for everything the map knows, inside a panel showing UV window [UV0, UV1]. bFull = full map (names, more detail). */
	void MinimapDrawIcons(const ASQGameState* GS, const ASQCharacter* C, const USQData* D, float PX, float PY, float PW, float PH, const FVector2D& UV0, const FVector2D& UV1, bool bFull);
	/** Skate-game style lower-third when the local reaper enters a district. */
	void DrawDistrictBanner(const ASQGameState* GS, const ASQCharacter* C);
	UPROPERTY(Transient) TObjectPtr<UTexture2D> MinimapTex;
	TArray<FColor> MinimapBase;
	TArray<FColor> MinimapPixels;
	TArray<uint8> MinimapFog;
	TArray<TWeakObjectPtr<AActor>> MinimapActors;
	TWeakObjectPtr<const ASQArena> MinimapArena;
	int32 MinimapSerial = -1;
	float MinimapHalf = 11200.f;
	float MinimapYaw = 0.f;
	float MinimapFogTimer = 0.f;
	float MinimapActorTimer = 0.f;
	bool bMinimapDirty = false;
	bool bFullMap = false;
	float FullMapOpen = 0.f;
	int32 DistrictSeen = INDEX_NONE;
	float DistrictBannerAt = -100.f;
	FString DistrictBannerName;
	FLinearColor DistrictBannerColour = FLinearColor::White;
	/** Top-left teammate mini-cards (not you); returns the bottom */
	float DrawTeamCards(const ASQGameState* GS, const ASQPlayerState* PS, const USQData* D, float X, float Y);
	/** AUGMENT offer: gold-framed cards (ASQPlayerState::AugmentOffers, ServerChooseAugment) */
	void DrawAugmentOffer(const ASQGameState* GS, ASQPlayerState* PS, const USQData* D);
	/** Lobby pieces */
	void DrawLobbyStage(const FSQReaperDef* Def, float X, float Y, float W, float H, const FLinearColor& Accent);
	float DrawKitPanel(const FSQReaperDef* Def, const USQData* D, float X, float Y, float W, const FLinearColor& Accent);
	float DrawStatBars(const FSQReaperDef* Def, const USQData* D, float X, float Y, float W, const FLinearColor& Accent);
	float DrawMissionPanel(const ASQGameState* GS, const USQData* D, ASQPlayerController* SQPC, float X, float Y, float W);
	float DrawCrewPanel(const ASQGameState* GS, const ASQPlayerState* PS, const USQData* D, float X, float Y, float W, float MaxH);
	/** Spawns / feeds / removes the lobby studios (3D stage + portrait captures) */
	void LobbyStudioUpdate(bool bLobby, FName FocusReaper, const FLinearColor& Accent, const USQData* D);
	void LobbyStudioShutdown();
	UTextureRenderTarget2D* HudMakeTarget(int32 TargetW, int32 TargetH);

	UPROPERTY(Transient) TObjectPtr<ASQMenuShowroom> StageStudio;
	UPROPERTY(Transient) TObjectPtr<ASQMenuShowroom> PortraitStudio;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> StageTarget;
	/** One portrait per reaper, captured once (kept for the whole session: the HUD reuses them for crew cards) */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UTextureRenderTarget2D>> PortraitTargets;
	TSet<FName> PortraitsReady;
	FName PortraitPending;
	int32 PortraitWait = 0;
	bool bPortraitsDone = false;
	FName StageShown;
	float StageSwapAt = -100.f;

	/** Design height the bottom ability bar takes (the co-op level-up strip sits above it) */
	float BottomReserve = 0.f;
	float ObjectivesBottom = 0.f;
	int32 ObjectivesDoneSeen = -1;
	float ObjectiveFlashAt = -100.f;
	float AbilityFlashAt[4] = { -100.f, -100.f, -100.f, -100.f };
	bool bAbilityWasReady[4] = { false, false, false, false };
	uint32 AugmentSignature = 0;
	float AugmentShownAt = -100.f;
	float AugmentLift[4] = { 0.f, 0.f, 0.f, 0.f };
	int32 AugmentFlashIndex = -1;
	float AugmentFlashAt = -100.f;
	float AugmentClickLockUntil = -100.f;
	float AugmentVeil = 0.f;

	// lobby
	float LobbyFocusAt = -100.f;
	int32 LobbyRosterRow = 0;
	float LobbyReadyLift = 0.f;
	float LobbyReadyFlashAt = -100.f;
	bool bClausesOpen = false;
	float ClauseRowLift = 0.f;

	// ---------------- draw helpers (design units; converted with UIScale)
	/** A texture (render target) in a design-space box, UV-cropped. Opaque: fades darken toward black. */
	void HudTexture(UTexture* Tex, float X, float Y, float W, float H, const FLinearColor& Tint, float U0 = 0.f, float V0 = 0.f, float U1 = 1.f, float V1 = 1.f);
	/** A reaper portrait (captured in the lobby) filling a box; a colour tile with the initial until it exists. Grey 0..1 = locked look */
	void HudPortrait(FName ReaperId, const USQData* D, float X, float Y, float W, float H, float Grey);
	/** Clockwise arc from 12 o'clock, 0..1 */
	void HudArc(float CX, float CY, float Radius, float Thick, float From01, float To01, const FLinearColor& Colour);
	/** A chevron arrow whose tip is at TipX/TipY, pointing along AngleDeg (screen space, 0 = right, 90 = down) */
	void HudArrow(float TipX, float TipY, float AngleDeg, float ArrowSize, const FLinearColor& Colour);
	/** Layered premium panel: soft multi-layer shadow, accent rim and top strip, deep body with a darker lower half */
	void HudLayerPanel(float X, float Y, float W, float H, float Radius, const FLinearColor& Accent, float FillAlpha = 0.95f);
	/** Text from FString / FText / FName fields alike (other agents' view structs) */
	static FString HudStr(const FString& In) { return In; }
	static FString HudStr(const FText& In) { return In.ToString(); }
	static FString HudStr(FName In) { return In.IsNone() ? FString() : In.ToString(); }
	/** "SoulHarvest" / "Soul_Harvest" -> "Soul Harvest" */
	static FString HudPretty(FName Id);
	static FString HudTimeText(float Seconds);

	void HudLoadFonts();
	UFont* HudMakeFont(UFontFace* Face, int32 Size);
	UFont* HudDisplayFont() const;
	UFont* HudHeadFont() const;
	UFont* HudLabelFont(bool bBold) const;

	/** Mouse in design units (viewport pixels minus this view's origin - split screen / locked aspect bars - then / UIScale).
	 *  Only valid while drawing. False when there is no mouse position. */
	bool HudMouseDesign(float& OutX, float& OutY) const;

	float HudTxtW(const FString& Str, UFont* InFont, float Px);
	float HudTxt(const FString& Str, UFont* InFont, float Px, float X, float Y, const FLinearColor& Colour, int32 Align = 0, bool bShadow = true);
	void HudGlowTxt(const FString& Str, UFont* InFont, float Px, float X, float Y, const FLinearColor& Colour, const FLinearColor& GlowColour, int32 Align);
	float HudFitPx(const FString& Str, UFont* InFont, float Px, float MaxW);
	int32 HudWrap(const FString& Str, UFont* InFont, float Px, float X, float Y, float MaxW, const FLinearColor& Colour, float LineStep, int32 MaxLines,
		bool bDraw = true, int32 Align = 0);

	void HudRectPx(const FLinearColor& Colour, float PX, float PY, float PW, float PH);
	void HudRect(const FLinearColor& Colour, float X, float Y, float W, float H);
	void HudRound(const FLinearColor& Colour, float X, float Y, float W, float H, float Radius);
	void HudDisc(const FLinearColor& Colour, float CX, float CY, float Radius);
	void HudGlow(const FLinearColor& Colour, float X, float Y, float W, float H, float Radius, float Strength);
	void HudPanel(float X, float Y, float W, float H, float Radius, const FLinearColor& Edge, float FillAlpha = 0.94f);
	void HudShine(float X, float Y, float W, float H, float Period, float Inset, float Offset = 0.f);
	void HudTilt(const FLinearColor& Colour, float X, float Y, float W, float H, float Deg);
	void HudBar(float X, float Y, float W, float H, float Fill, float Ghost, const FLinearColor& Colour);
	float HudKeyCap(const FString& KeyNameIn, float X, float Y, float CapH = 26.f);
	float HudPill(const FString& Label, float X, float Y, const FLinearColor& Fill, const FLinearColor& Ink, float Px, int32 Align = 0);
	void HudSoulIcon(float CX, float CY, float IconSize, const FLinearColor& Colour, float GlowAmount);
	void HudGlyph(int32 GlyphKind, float CX, float CY, float GlyphSize, const FLinearColor& Colour);
	void HudPadlock(float CX, float CY, float LockSize, const FLinearColor& Colour);
	void HudChevron(float X, float Y, float ChevH, const FLinearColor& Colour);
	float HudPickButton(const FString& Label, const FString& KeyName, const FString& SubLabel, float X, float Y, float W, float H,
		const FLinearColor& Accent, bool bEnabled, float Lift);
	void HudSparkle(float CX, float CY, float SparkSize, const FLinearColor& Colour);
	FLinearColor HudKindColour(const FSQUpgradeDef* Def) const;
	FString HudInitials(const FString& Name) const;
	FString ClockText(float Progress) const;

	static float HudEaseOut(float T);
	static float HudBackOut(float T);
	static float HudPulse(float Time, float Speed) { return 0.5f + 0.5f * FMath::Sin(Time * Speed); }
};
