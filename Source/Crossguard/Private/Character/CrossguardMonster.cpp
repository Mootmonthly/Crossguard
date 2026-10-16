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
	bWieldsWeapon = Row->bWieldsWeapon;
	WeaponType = Row->WeaponType;
	bTwoHandedWeapon = Row->bTwoHandedWeapon;
	ImpactPower = Row->ImpactPower;
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
	const float OldScale = MobScale;
	MobScale = FMath::Max(Row->Scale, 0.05f);
	OnRep_MobScale();
	// Feet stay on the ground: a bigger capsule grows into the floor otherwise.
	AddActorWorldOffset(FVector(0.0f, 0.0f, (MobScale - OldScale) * GetDefaultHalfHeight()));
	HomeLocation = GetActorLocation();
	// Possession already set Health and granted skills from the old values.
	GetCrossguardAbilitySystemComponent()->SetFixedMaxHealth(FixedHealth);
	RestoreFullHealth();
	SkillIds = Row->Skills;
	if (GameMode->MobSkillCatalog != nullptr)
	{
		SkillCatalog = GameMode->MobSkillCatalog;
	}
	GrantSkills();
	// Art: its own mesh from data (mixed spawners give every mob its look). Replicated to clients.
	if (!Row->Mesh.IsNull())
	{
		BodyVisual.Mesh = Row->Mesh;
		BodyVisual.AnimClass = Row->AnimClass;
		BodyVisual.MeshScale = Row->MeshScale;
		BodyVisual.MeshOffset = Row->MeshOffset;
		BodyVisual.MeshYaw = Row->MeshYaw;
		BodyVisual.IdleAnim = Row->IdleAnim;
		BodyVisual.MoveAnim = Row->MoveAnim;
		BodyVisual.AttackAnim = Row->AttackAnim;
		BodyVisual.DeathAnim = Row->DeathAnim;
		ApplyBodyVisual();
	}
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
