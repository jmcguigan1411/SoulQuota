// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQEventSite.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SQArena.h"
#include "SQArt.h"
#include "SQData.h"
#include "SQFx.h"
#include "SQInteractCue.h"
#include "SQQuests.h"

ASQEventSite::ASQEventSite()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	SiteRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SiteRoot"));
	RootComponent = SiteRoot;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(SiteRoot);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);

	Cap = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cap"));
	Cap->SetupAttachment(SiteRoot);
	Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cap->SetCanEverAffectNavigation(false);

	Cue = CreateDefaultSubobject<USQInteractCue>(TEXT("Cue"));
	Cue->SetupAttachment(SiteRoot);
}

void ASQEventSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQEventSite, EventId);
	DOREPLIFETIME(ASQEventSite, Radius);
	DOREPLIFETIME(ASQEventSite, State);
}

void ASQEventSite::BeginPlay()
{
	Super::BeginPlay();
	if (!EventId.IsNone()) BuildVisuals();
}

void ASQEventSite::InitSite(FName InEventId, float InRadius)
{
	if (!HasAuthority()) return;
	EventId = InEventId;
	Radius = FMath::Clamp(InRadius, 120.f, 600.f);
	BuildVisuals();
	ForceNetUpdate();
}

void ASQEventSite::Finish(bool bDone)
{
	if (!HasAuthority() || State != 0) return;
	State = bDone ? 1 : 2;
	ShowState();
	ForceNetUpdate();
	// Long enough for the finish to replicate and play; then the floor is clear again
	SetLifeSpan(3.f);
}

void ASQEventSite::OnRep_Event()
{
	if (HasActorBegunPlay()) BuildVisuals();
}

void ASQEventSite::OnRep_State()
{
	ShowState();
}

void ASQEventSite::BuildVisuals()
{
	if (bVisualsBuilt || GetNetMode() == NM_DedicatedServer || !Body || !Cap) return;
	const FSQEventDef* Def = SQQuests::FindEvent(EventId);
	if (!Def) return;
	bVisualsBuilt = true;
	const float H = Def->Height;
	UStaticMesh* Art = Def->Model.IsNone() ? nullptr : SQArt::Prop(Def->Model);
	if (Art)
	{
		const float Fit = ASQArena::FitMeshHeight(Art, H);
		const FBoxSphereBounds ArtBounds = Art->GetBounds();
		Body->SetStaticMesh(Art);
		Body->SetRelativeScale3D(FVector(Fit));
		Body->SetRelativeLocation(FVector(-ArtBounds.Origin.X * Fit, -ArtBounds.Origin.Y * Fit, -(ArtBounds.Origin.Z - ArtBounds.BoxExtent.Z) * Fit));
		SiteMID = SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(Def->Color, 0.35f));
		if (SiteMID) SiteMID->SetScalarParameterValue(TEXT("Glow"), 0.6f);
		SQArt::SetAllMaterials(Body, SiteMID);
		Cap->SetVisibility(false);
	}
	else
	{
		// Primitive marker: a short plinth with a glowing orb on top
		SiteMID = ASQArena::SurfaceMID(this, Def->Color, 0.8f);
		Body->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
		Body->SetRelativeScale3D(FVector(H * 0.006f, H * 0.006f, H * 0.006f));
		Body->SetRelativeLocation(FVector(0.f, 0.f, H * 0.3f));
		Body->SetMaterial(0, SiteMID);
		Cap->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
		Cap->SetRelativeScale3D(FVector(H * 0.004f));
		Cap->SetRelativeLocation(FVector(0.f, 0.f, H * 0.85f));
		Cap->SetMaterial(0, SiteMID);
		Cap->SetVisibility(true);
	}
	if (Cue)
	{
		Cue->Setup(Def->Reward.Contains(TEXT("Chest")) ? ESQCueKind::FreeChest : ESQCueKind::Shrine, Radius, H + 80.f, Def->Name.ToUpper());
		Cue->SetUrgent(true);
	}
	// Arrival beat: a column of the event's colour so the spot reads from a distance
	SQFx::Shockwave(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 12.f), Radius * 1.4f, Def->Color, 0.7f, 26.f);
	SQFx::Burst(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, H * 0.5f), Def->Color, 14, 420.f, 0.9f, 16.f, 300.f);
	ShowState();
}

void ASQEventSite::ShowState()
{
	if (GetNetMode() == NM_DedicatedServer || State == ShownState || State == 0) return;
	ShownState = State;
	const FSQEventDef* Def = SQQuests::FindEvent(EventId);
	const FLinearColor Colour = Def ? Def->Color : FLinearColor::White;
	if (Cue) Cue->SetUsed(true);
	if (State == 1)
	{
		// Completed: bright layered impact, then a rising tail; the prop goes with it
		SQFx::Flash(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 90.f), Colour, 260.f, 0.16f);
		SQFx::Shockwave(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 12.f), Radius * 2.f, Colour, 0.55f, 34.f);
		SQFx::Burst(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 80.f), Colour, 26, 620.f, 1.1f, 18.f, 240.f, 0.05f);
	}
	else
	{
		SQFx::Burst(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 60.f), SQArt::SoftTint(Colour, 0.6f), 10, 260.f, 0.8f, 12.f, 200.f);
	}
	if (Body) Body->SetVisibility(false);
	if (Cap) Cap->SetVisibility(false);
}
