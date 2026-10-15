#include "Character/CrossguardMonster.h"

#include "Character/CrossguardMobRow.h"
#include "Character/CrossguardMonsterController.h"
#include "Crossguard.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Net/UnrealNetwork.h"

ACrossguardMonster::ACrossguardMonster()
{
	AIControllerClass = ACrossguardMonsterController::StaticClass();
	KillXpSource = ECrossguardXpSource::BasicMonster;
	LootTableId = TEXT("basic_monster");
	// Placeholder numbers.
	FixedHealth = 80.0f;
	AttackDamage = 12.0f;
	AttackInterval = 1.5f;
}

void ACrossguardMonster::OnDamaged(AActor* Attacker, float Damage)
{
	if (ACrossguardMonsterController* MonsterController = GetController<ACrossguardMonsterController>())
	{
		MonsterController->NotifyAttacked(Attacker);
	}
}

void ACrossguardMonster::BeginPlay()
{
	Super::BeginPlay();
	HomeLocation = GetActorLocation();
	if (HasAuthority())
	{
		ApplyMobData();
	}
}

void ACrossguardMonster::ApplyMobData()
{
	if (MobId.IsNone())
	{
		return;
	}
	const ACrossguardGameMode* GameMode = GetWorld()->GetAuthGameMode<ACrossguardGameMode>();
	const FCrossguardMobRow* Row = GameMode != nullptr && GameMode->MobTable != nullptr
		? GameMode->MobTable->FindRow<FCrossguardMobRow>(MobId, TEXT("Monster"), false)
		: nullptr;
	if (Row == nullptr)
	{
		UE_LOG(LogCrossguard, Warning, TEXT("%s: no mob '%s' (is the game mode's Mob Table set?)"), *GetName(), *MobId.ToString());
		return;
	}
	CombatRole = Row->CombatRole;
	FixedHealth = Row->Health;
	AttackDamage = Row->AttackDamage;
	AttackInterval = Row->AttackInterval;
	AttackRange = Row->AttackRange;
	FireballInterval = Row->FireballInterval;
	KeepDistance = Row->KeepDistance;
	AggroRange = Row->AggroRange;
	LeashRange = Row->LeashRange;
	KillXpSource = Row->Kind == ECrossguardMobKind::Boss ? ECrossguardXpSource::BossMonster : ECrossguardXpSource::BasicMonster;
	LootTableId = Row->LootTableId;
	SetBaseWalkSpeed(Row->MoveSpeed);
	MobScale = FMath::Max(Row->Scale, 0.05f);
	OnRep_MobScale();
	// Possession already set Health from the old value.
	GetCrossguardAbilitySystemComponent()->SetFixedMaxHealth(FixedHealth);
	RestoreFullHealth();
}

void ACrossguardMonster::OnRep_MobScale()
{
	SetActorScale3D(FVector(MobScale));
}

void ACrossguardMonster::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACrossguardMonster, MobScale);
}
