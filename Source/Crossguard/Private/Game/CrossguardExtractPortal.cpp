#include "Game/CrossguardExtractPortal.h"

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Character/CrossguardCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardTeamLibrary.h"
#include "Net/UnrealNetwork.h"

static_assert(static_cast<int>(ECrossguardPortalState::Channeling) == static_cast<int>(Crossguard::EPortalState::Channeling), "Portal state enums out of sync");
static_assert(static_cast<int>(ECrossguardPortalState::Open) == static_cast<int>(Crossguard::EPortalState::Open), "Portal state enums out of sync");
static_assert(static_cast<int>(ECrossguardPortalState::Closed) == static_cast<int>(Crossguard::EPortalState::Closed), "Portal state enums out of sync");
static_assert(static_cast<int>(ECrossguardPortalState::Collapsed) == static_cast<int>(Crossguard::EPortalState::Collapsed), "Portal state enums out of sync");

namespace
{
	bool IsAlive(const ACrossguardCharacter* Character)
	{
		const UAbilitySystemComponent* ASC = Character != nullptr ? Character->GetAbilitySystemComponent() : nullptr;
		return ASC != nullptr && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
	}
}

ACrossguardExtractPortal::ACrossguardExtractPortal()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	// Keeper checks and the countdown do not need every frame.
	PrimaryActorTick.TickInterval = 0.25f;

	EnterArea = CreateDefaultSubobject<USphereComponent>(TEXT("EnterArea"));
	EnterArea->InitSphereRadius(120.0f);
	EnterArea->SetCollisionProfileName(TEXT("Trigger"));
	SetRootComponent(EnterArea);

	BeamMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeamMesh"));
	BeamMesh->SetupAttachment(EnterArea);
	BeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PortalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalMesh"));
	PortalMesh->SetupAttachment(EnterArea);
	PortalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	EnterArea->OnComponentBeginOverlap.AddDynamic(this, &ACrossguardExtractPortal::OnEnterAreaBeginOverlap);
}

void ACrossguardExtractPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACrossguardExtractPortal, State);
	DOREPLIFETIME(ACrossguardExtractPortal, SecondsLeft);
}

void ACrossguardExtractPortal::BeginPlay()
{
	Super::BeginPlay();

	Crossguard::FPortalConfig Config;
	Config.ChannelSeconds = ChannelSeconds;
	Config.OpenSeconds = OpenSeconds;
	Config.KeepAliveRadius = KeepAliveRadius;
	Timeline = Crossguard::FExtractPortal(Config);
	SecondsLeft = ChannelSeconds;
	ApplyVisuals();
}

void ACrossguardExtractPortal::Initialize(ACrossguardCharacter* InSummoner, int32 InTeamId)
{
	Summoner = InSummoner;
	TeamId = InTeamId;
}

bool ACrossguardExtractPortal::IsKeeperPresent() const
{
	for (TActorIterator<ACrossguardCharacter> It(GetWorld()); It; ++It)
	{
		const ACrossguardCharacter* Character = *It;
		const bool bOnTeam = TeamId >= 0 ? UCrossguardTeamLibrary::GetTeamId(Character) == TeamId : Character == Summoner.Get();
		if (bOnTeam && IsAlive(Character) && !Character->HasLeftRaid() && FVector::Dist(Character->GetActorLocation(), GetActorLocation()) <= KeepAliveRadius)
		{
			return true;
		}
	}
	return false;
}

void ACrossguardExtractPortal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}

	// Keepers only matter while channeling.
	const bool bKeeper = State != ECrossguardPortalState::Channeling || IsKeeperPresent();
	const ECrossguardPortalState NewState = static_cast<ECrossguardPortalState>(static_cast<uint8>(Timeline.Advance(DeltaSeconds, bKeeper)));
	SecondsLeft = static_cast<float>(Timeline.GetSecondsLeft());
	if (NewState != State)
	{
		SetState(NewState);
	}
}

void ACrossguardExtractPortal::SetState(ECrossguardPortalState NewState)
{
	State = NewState;
	ApplyVisuals();
	OnStateChanged.Broadcast(State);

	switch (State)
	{
	case ECrossguardPortalState::Open:
	{
		// Whoever already stands in it goes through.
		TArray<AActor*> Inside;
		EnterArea->GetOverlappingActors(Inside, ACrossguardCharacter::StaticClass());
		for (AActor* Actor : Inside)
		{
			TryExtract(Actor);
		}
		break;
	}
	case ECrossguardPortalState::Closed:
	case ECrossguardPortalState::Collapsed:
		SetActorTickEnabled(false);
		// Long enough for clients to see it close.
		SetLifeSpan(2.0f);
		break;
	case ECrossguardPortalState::Channeling:
		break;
	}
}

void ACrossguardExtractPortal::OnRep_State()
{
	ApplyVisuals();
	OnStateChanged.Broadcast(State);
}

void ACrossguardExtractPortal::ApplyVisuals()
{
	BeamMesh->SetVisibility(State == ECrossguardPortalState::Channeling);
	PortalMesh->SetVisibility(State == ECrossguardPortalState::Open);
}

void ACrossguardExtractPortal::OnEnterAreaBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryExtract(OtherActor);
}

void ACrossguardExtractPortal::TryExtract(AActor* Actor)
{
	ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(Actor);
	ACrossguardGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ACrossguardGameMode>() : nullptr;
	if (HasAuthority() && State == ECrossguardPortalState::Open && Character != nullptr && GameMode != nullptr)
	{
		// Anyone may use it. The game mode refuses the dead and those not in the raid.
		GameMode->HandleExtraction(Character);
	}
}
