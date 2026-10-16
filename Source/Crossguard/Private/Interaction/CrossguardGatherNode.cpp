#include "Interaction/CrossguardGatherNode.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystem/CrossguardPassiveAbility.h"
#include "AbilitySystemComponent.h"
#include "Character/CrossguardCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Crossguard.h"
#include "CrossguardRules/Config.h"
#include "CrossguardRules/Gathering.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "GameplayEffectTypes.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardWorldItem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace CrossguardGatherNodeImpl
{
	// How often a channel checks for movement and range (damage and attacks arrive as events).
	constexpr float PollSeconds = 0.1f;

	const TCHAR* InterruptName(Crossguard::EGatherInterrupt Reason)
	{
		switch (Reason)
		{
		case Crossguard::EGatherInterrupt::Died: return TEXT("died");
		case Crossguard::EGatherInterrupt::Moved: return TEXT("moved");
		case Crossguard::EGatherInterrupt::Damaged: return TEXT("took damage");
		case Crossguard::EGatherInterrupt::Attacked: return TEXT("attacked");
		case Crossguard::EGatherInterrupt::OutOfRange: return TEXT("out of range");
		default: return TEXT("none");
		}
	}

	Crossguard::FGatherConfig MakeConfig(const ACrossguardGatherNode& Node)
	{
		Crossguard::FGatherConfig Config;
		Config.GatherSeconds = Node.GatherSeconds;
		Config.RespawnSeconds = Node.RespawnSeconds;
		Config.MoveTolerance = Node.MoveTolerance;
		return Config;
	}
}

ACrossguardGatherNode::ACrossguardGatherNode()
{
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	NodeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NodeMesh"));
	NodeMesh->SetupAttachment(GetRootComponent());
	// Placeholder: a half-metre sphere sitting on the ground. Found by the interact search, not by
	// collision; it still blocks movement like a rock or bush.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		NodeMesh->SetStaticMesh(Sphere.Object);
		NodeMesh->SetRelativeScale3D(FVector(0.5f));
		NodeMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 25.0f));
	}
}

void ACrossguardGatherNode::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACrossguardGatherNode, bDepleted);
	DOREPLIFETIME(ACrossguardGatherNode, Gatherer);
	DOREPLIFETIME(ACrossguardGatherNode, GatherStartTime);
}

void ACrossguardGatherNode::BeginPlay()
{
	Super::BeginPlay();
	// What the Blueprint set; restored on respawn.
	MeshCollision = NodeMesh->GetCollisionEnabled();
	ApplyDepleted();
}

void ACrossguardGatherNode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopGather();
	GetWorldTimerManager().ClearTimer(RespawnTimer);
	Super::EndPlay(EndPlayReason);
}

float ACrossguardGatherNode::GetGatherProgress() const
{
	if (Gatherer == nullptr || GetWorld() == nullptr)
	{
		return 0.0f;
	}
	if (GatherSeconds <= 0.0f)
	{
		return 1.0f;
	}
	const float Elapsed = static_cast<float>(GetWorld()->GetTimeSeconds() - GatherStartTime);
	return FMath::Clamp(Elapsed / GatherSeconds, 0.0f, 1.0f);
}

void ACrossguardGatherNode::Interact(ACrossguardCharacter* User)
{
	if (!HasAuthority() || User == nullptr)
	{
		return;
	}
	// F again stops one's own channel.
	if (Gatherer == User)
	{
		CancelGather(TEXT("stopped"));
		return;
	}
	StartGather(User);
}

void ACrossguardGatherNode::StartGather(ACrossguardCharacter* User)
{
	UAbilitySystemComponent* ASC = User->GetAbilitySystemComponent();
	const bool bDead = ASC != nullptr && ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
	const Crossguard::EGatherStartError Error = Crossguard::CanStartGather(bDepleted, Gatherer != nullptr, bDead);
	if (Error != Crossguard::EGatherStartError::None)
	{
		UE_LOG(LogCrossguard, Log, TEXT("%s cannot gather %s: %s"), *User->GetName(), *GetName(),
			Error == Crossguard::EGatherStartError::Depleted ? TEXT("depleted") : Error == Crossguard::EGatherStartError::Busy ? TEXT("busy") : TEXT("dead"));
		return;
	}

	Gatherer = User;
	GatherStartTime = GetWorld()->GetTimeSeconds();
	GatherStartLocation = User->GetActorLocation();
	PendingHealthLost = 0.0;
	bGathererAttacked = false;
	if (ASC != nullptr)
	{
		BoundAbilitySystem = ASC;
		ActivatedHandle = ASC->AbilityActivatedCallbacks.AddUObject(this, &ACrossguardGatherNode::OnGathererAbilityActivated);
		HealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UCrossguardAttributeSet::GetHealthAttribute())
			.AddUObject(this, &ACrossguardGatherNode::OnGathererHealthChanged);
	}
	GetWorldTimerManager().SetTimer(PollTimer, this, &ACrossguardGatherNode::PollGather, CrossguardGatherNodeImpl::PollSeconds, true);
	UE_LOG(LogCrossguard, Log, TEXT("%s started gathering %s (%.1f s)"), *User->GetName(), *GetName(), GatherSeconds);
	OnGatherNodeChanged.Broadcast();

	// A zero-second channel completes at once.
	if (GatherSeconds <= 0.0f)
	{
		PollGather();
	}
}

void ACrossguardGatherNode::PollGather()
{
	ACrossguardCharacter* User = Gatherer;
	if (User == nullptr || !IsValid(User))
	{
		CancelGather(TEXT("gatherer gone"));
		return;
	}
	const UAbilitySystemComponent* ASC = BoundAbilitySystem.Get();
	const bool bDead = ASC != nullptr && ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
	const double Moved = FVector::Dist(GatherStartLocation, User->GetActorLocation());
	const bool bInRange = FVector::Dist(User->GetActorLocation(), GetActorLocation()) <= InteractRange;
	const Crossguard::EGatherInterrupt Reason = Crossguard::CheckGatherInterrupt(CrossguardGatherNodeImpl::MakeConfig(*this), bDead, Moved,
		PendingHealthLost, bGathererAttacked, bInRange);
	PendingHealthLost = 0.0;
	if (Reason != Crossguard::EGatherInterrupt::None)
	{
		CancelGather(CrossguardGatherNodeImpl::InterruptName(Reason));
		return;
	}
	if (GetWorld()->GetTimeSeconds() - GatherStartTime >= GatherSeconds)
	{
		CompleteGather();
	}
}

void ACrossguardGatherNode::CompleteGather()
{
	ACrossguardCharacter* User = Gatherer;
	StopGather();
	if (User == nullptr)
	{
		return;
	}

	// The yield: rolled into the inventory; what does not fit drops on the ground here.
	TArray<FCrossguardInventoryItem> Yield;
	if (ACrossguardGameMode* GameMode = GetWorld()->GetAuthGameMode<ACrossguardGameMode>(); GameMode != nullptr && !LootTableId.IsNone())
	{
		Yield = GameMode->RollLoot(LootTableId);
	}
	UCrossguardInventoryComponent* Inventory = User->GetInventoryComponent();
	int32 Gathered = 0;
	int32 Dropped = 0;
	for (const FCrossguardInventoryItem& Item : Yield)
	{
		const int32 Left = Inventory != nullptr ? Inventory->AddItem(Item) : Item.Count;
		Gathered += Item.Count - Left;
		if (Left <= 0)
		{
			continue;
		}
		FCrossguardInventoryItem Rest = Item;
		Rest.Count = Left;
		Rest.bWasEquipped = false;
		const FTransform Transform(GetActorRotation(), GetActorLocation() + FVector(0.0f, 0.0f, 12.5f));
		const TSubclassOf<ACrossguardWorldItem> Class = Inventory != nullptr && Inventory->WorldItemClass != nullptr ? Inventory->WorldItemClass
			: TSubclassOf<ACrossguardWorldItem>(ACrossguardWorldItem::StaticClass());
		if (ACrossguardWorldItem* WorldItem = GetWorld()->SpawnActorDeferred<ACrossguardWorldItem>(Class, Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			WorldItem->SetItem(Rest);
			WorldItem->FinishSpawning(Transform);
			Dropped += Left;
		}
	}

	ACrossguardPlayerState* PlayerState = User->GetPlayerState<ACrossguardPlayerState>();
	if (PlayerState != nullptr)
	{
		PlayerState->AwardXp(ECrossguardXpSource::GatherNode);
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s gathered %s (%s): %d items, %d dropped (no room), +%d XP"), *User->GetName(), *GetName(),
		*LootTableId.ToString(), Gathered, Dropped, PlayerState != nullptr ? Crossguard::FXpConfig().GatherNode : 0);
	Deplete();
}

void ACrossguardGatherNode::CancelGather(const TCHAR* Reason)
{
	if (!HasAuthority() || Gatherer == nullptr)
	{
		return;
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s gathering %s interrupted: %s"), *GetNameSafe(Gatherer.Get()), *GetName(), Reason);
	StopGather();
}

void ACrossguardGatherNode::StopGather()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	if (UAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
	{
		ASC->AbilityActivatedCallbacks.Remove(ActivatedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UCrossguardAttributeSet::GetHealthAttribute()).Remove(HealthHandle);
	}
	ActivatedHandle.Reset();
	HealthHandle.Reset();
	BoundAbilitySystem.Reset();
	const bool bWasGathering = Gatherer != nullptr;
	Gatherer = nullptr;
	PendingHealthLost = 0.0;
	bGathererAttacked = false;
	if (bWasGathering)
	{
		OnGatherNodeChanged.Broadcast();
	}
}

void ACrossguardGatherNode::OnGathererAbilityActivated(UGameplayAbility* Ability)
{
	// Attacks, skills, the guard; passives switching on (equipping, auras) do not count.
	if (Ability != nullptr && !Ability->IsA<UCrossguardPassiveAbility>())
	{
		bGathererAttacked = true;
	}
}

void ACrossguardGatherNode::OnGathererHealthChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue < Data.OldValue)
	{
		PendingHealthLost += Data.OldValue - Data.NewValue;
	}
}

void ACrossguardGatherNode::Deplete()
{
	bDepleted = true;
	DepletedTime = GetWorld()->GetTimeSeconds();
	ApplyDepleted();
	OnGatherNodeChanged.Broadcast();
	if (RespawnSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &ACrossguardGatherNode::Respawn, RespawnSeconds, false);
	}
}

void ACrossguardGatherNode::Respawn()
{
	// The timer runs RespawnSeconds; the slack covers float rounding of game time.
	const double SinceDepleted = GetWorld()->GetTimeSeconds() - DepletedTime + 0.01;
	if (!HasAuthority() || !bDepleted || !Crossguard::HasGatherNodeRespawned(CrossguardGatherNodeImpl::MakeConfig(*this), SinceDepleted))
	{
		return;
	}
	bDepleted = false;
	ApplyDepleted();
	UE_LOG(LogCrossguard, Log, TEXT("%s respawned"), *GetName());
	OnGatherNodeChanged.Broadcast();
}

void ACrossguardGatherNode::OnRep_Depleted()
{
	ApplyDepleted();
	OnGatherNodeChanged.Broadcast();
}

void ACrossguardGatherNode::OnRep_Gatherer()
{
	OnGatherNodeChanged.Broadcast();
}

void ACrossguardGatherNode::ApplyDepleted()
{
	NodeMesh->SetVisibility(!bDepleted);
	NodeMesh->SetCollisionEnabled(bDepleted ? ECollisionEnabled::NoCollision : MeshCollision.GetValue());
}
