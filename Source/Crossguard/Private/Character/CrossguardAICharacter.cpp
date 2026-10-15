#include "Character/CrossguardAICharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemGlobals.h"
#include "Combat/CrossguardProjectile.h"
#include "Combat/CrossguardSpellProjectile.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Crossguard.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/Loadout.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Skills/CrossguardDataPassiveAbility.h"
#include "Skills/CrossguardNativeSkills.h"
#include "Skills/CrossguardSkillAbility.h"
#include "Skills/CrossguardSkillCatalog.h"
#include "Skills/CrossguardSpellSkills.h"
#include "UObject/ConstructorHelpers.h"

#include <algorithm>

namespace
{
	// How often an AI looks for a skill to use.
	constexpr double AISkillCheckSeconds = 0.25;
}

ACrossguardAICharacter::ACrossguardAICharacter()
{
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	FireballClass = ACrossguardSpellProjectile::StaticClass();
	ArrowClass = ACrossguardProjectile::StaticClass();
	// AI turns toward where it moves.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	// The base character hides its mesh from its owner (first person); an AI character's owner may
	// be a player (a summoner), who must see it.
	GetMesh()->SetOwnerNoSee(false);

	// Visible without any art: a cylinder the size of the capsule.
	PlaceholderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderMesh"));
	PlaceholderMesh->SetupAttachment(GetCapsuleComponent());
	PlaceholderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		PlaceholderMesh->SetStaticMesh(Cylinder.Object);
		// The engine cylinder is 100 cm wide and 100 cm tall, centered.
		const float Radius = GetCapsuleComponent()->GetUnscaledCapsuleRadius();
		const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		PlaceholderMesh->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, HalfHeight / 50.0f));
	}
}

void ACrossguardAICharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// Fixed Health instead of level-based attributes, kept when buffs change the attributes.
	GetCrossguardAbilitySystemComponent()->SetFixedMaxHealth(FixedHealth);
	RestoreFullHealth();
	GrantSkills();
}

void ACrossguardAICharacter::ClearSkills()
{
	if (UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent())
	{
		for (const FGameplayAbilitySpecHandle& Handle : GrantedSkills)
		{
			// Cancel first so an active passive runs EndAbility and removes its effects.
			ASC->CancelAbilityHandle(Handle);
			ASC->ClearAbility(Handle);
		}
	}
	GrantedSkills.Reset();
	SkillGrants.Reset();
}

void ACrossguardAICharacter::GrantSkills()
{
	UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	if (!HasAuthority() || ASC == nullptr)
	{
		return;
	}
	ClearSkills();
	// Bundles ([The Mithril Knight]) grant the skills they include, as in a player's loadout; needs
	// the catalog's data.
	TArray<FName> Effective = SkillIds;
	if (SkillCatalog != nullptr)
	{
		std::vector<std::string> Ids;
		for (const FName& SkillId : SkillIds)
		{
			Ids.push_back(std::string(TCHAR_TO_UTF8(*SkillId.ToString())));
		}
		const std::unordered_set<std::string> Resolved = Crossguard::ResolveLoadoutEffects(SkillCatalog->BuildRulesCatalog(), Ids);
		std::vector<std::string> Sorted(Resolved.begin(), Resolved.end());
		std::sort(Sorted.begin(), Sorted.end());
		Effective.Reset();
		for (const std::string& Id : Sorted)
		{
			Effective.Add(FName(UTF8_TO_TCHAR(Id.c_str())));
		}
	}
	for (const FName& SkillId : Effective)
	{
		const TSubclassOf<UCrossguardGameplayAbility> AbilityClass = SkillCatalog != nullptr ? SkillCatalog->FindAbilityClass(SkillId)
			: CrossguardNativeSkills::Find(SkillId);
		if (AbilityClass != nullptr)
		{
			GrantedSkills.Add(ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this)));
			continue;
		}
		// Passives without a class are played from their data row (as a player's loadout does).
		const FCrossguardSkillRow* Row = SkillCatalog != nullptr ? SkillCatalog->FindSkill(SkillId) : nullptr;
		if (Row != nullptr && Row->Category == ECrossguardSkillCategory::Passive)
		{
			UCrossguardSkillGrant* Grant = NewObject<UCrossguardSkillGrant>(this);
			Grant->SkillId = SkillId;
			Grant->Catalog = SkillCatalog;
			SkillGrants.Add(Grant);
			GrantedSkills.Add(ASC->GiveAbility(FGameplayAbilitySpec(UCrossguardDataPassiveAbility::StaticClass(), 1, INDEX_NONE, Grant)));
			continue;
		}
		UE_LOG(LogCrossguard, Warning, TEXT("%s: skill '%s' has no ability class%s"), *GetName(), *SkillId.ToString(),
			SkillCatalog == nullptr ? TEXT(" (no skill catalog: native skills only)") : TEXT(""));
	}
}

bool ACrossguardAICharacter::TryUseSkills(AActor* Target)
{
	UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	const double Now = GetWorld()->GetTimeSeconds();
	if (!HasAuthority() || !IsAlive() || ASC == nullptr || Target == nullptr || Now < NextSkillCheckTime)
	{
		return false;
	}
	NextSkillCheckTime = Now + AISkillCheckSeconds;
	// Every skill it has, including ones granted by its skills (Dragonification's Flare Breath).
	// Handles first: activating can grant abilities.
	TArray<FGameplayAbilitySpecHandle> Handles;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability != nullptr && Spec.Ability->IsA<UCrossguardSkillAbility>())
		{
			Handles.Add(Spec.Handle);
		}
	}
	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		const UCrossguardSkillAbility* Skill = Spec != nullptr ? Cast<UCrossguardSkillAbility>(Spec->Ability) : nullptr;
		if (Skill == nullptr || Spec->IsActive() || !Skill->ShouldAIUse(*this, *Target)
			|| !Skill->CanActivateAbility(Handle, ASC->AbilityActorInfo.Get()))
		{
			continue;
		}
		// Skills aim at Target (or itself), and dashes and leaps go where the character faces. Timed
		// skills (a breath) keep aiming there.
		SkillAimTarget = Skill->AIAimsAtSelf() ? static_cast<AActor*>(this) : Target;
		const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.0f, ToTarget.Rotation().Yaw, 0.0f));
		if (ASC->TryActivateAbility(Handle))
		{
			return true;
		}
	}
	return false;
}

TOptional<ECrossguardWeaponType> ACrossguardAICharacter::GetWieldedWeapon() const
{
	if (bWieldsWeapon)
	{
		return WeaponType;
	}
	// Ranged without a weapon set: a bow.
	return CombatRole == ECrossguardCombatRole::Ranged ? TOptional<ECrossguardWeaponType>(ECrossguardWeaponType::Bow)
		: TOptional<ECrossguardWeaponType>();
}

bool ACrossguardAICharacter::WieldsAnyOf(const TArray<ECrossguardWeaponType>& AnyOf) const
{
	if (AnyOf.IsEmpty())
	{
		return true;
	}
	const TOptional<ECrossguardWeaponType> Held = GetWieldedWeapon();
	if (!Held.IsSet())
	{
		return false;
	}
	const ECrossguardWeaponType Type = Held.GetValue();
	// The enums match (static_assert in CrossguardEquipmentComponent.cpp).
	Crossguard::FEquipment Equipment;
	Crossguard::FEquippedWeapon Weapon;
	Weapon.Type = static_cast<Crossguard::EWeaponType>(Type);
	Weapon.Handedness = bTwoHandedWeapon || (!bWieldsWeapon && CombatRole == ECrossguardCombatRole::Ranged)
		? Crossguard::EHandedness::TwoHanded : Crossguard::EHandedness::OneHanded;
	Weapon.bMelee = Type != ECrossguardWeaponType::Bow && Type != ECrossguardWeaponType::Crossbow && Type != ECrossguardWeaponType::Sling
		&& Type != ECrossguardWeaponType::Wand && Type != ECrossguardWeaponType::MagicStaff;
	Equipment.MainHand = Weapon;
	std::vector<Crossguard::EWeaponType> Required;
	for (const ECrossguardWeaponType Type : AnyOf)
	{
		Required.push_back(static_cast<Crossguard::EWeaponType>(Type));
	}
	return Crossguard::SatisfiesWeaponRequirement(Required, Equipment);
}

void ACrossguardAICharacter::RestoreFullHealth()
{
	UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	if (HasAuthority() && ASC != nullptr && IsAlive())
	{
		ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(),
			ASC->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute()));
	}
}

bool ACrossguardAICharacter::IsAlive() const
{
	const UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	return !IsActorBeingDestroyed() && ASC != nullptr && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
}

bool ACrossguardAICharacter::TryAttack(AActor* Target)
{
	UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	const double Now = GetWorld()->GetTimeSeconds();
	if (!HasAuthority() || !IsAlive() || ASC == nullptr || TargetASC == nullptr || Now < NextAttackTime)
	{
		return false;
	}

	if (CombatRole == ECrossguardCombatRole::Mage)
	{
		// Fireball is a skill: not while Muted or Silenced.
		if (ASC->HasMatchingGameplayTag(CrossguardTags::State_Muted) || ASC->HasMatchingGameplayTag(CrossguardTags::State_Silenced))
		{
			return false;
		}
		FVector Eyes;
		FRotator Unused;
		GetActorEyesViewPoint(Eyes, Unused);
		const FVector Direction = (Target->GetActorLocation() - Eyes).GetSafeNormal();
		UCrossguardFireballAbility::LaunchFireball(ASC, FireballClass, Eyes, Direction);
		NextAttackTime = Now + FireballInterval;
		return true;
	}

	if (FVector::Dist(GetActorLocation(), Target->GetActorLocation()) > AttackRange)
	{
		return false;
	}
	// Readied next-attack skills ride on the attack, as on a weapon attack; AttackDamage stands in
	// for the weapon's damage.
	const bool bRanged = CombatRole == ECrossguardCombatRole::Ranged;
	const FCrossguardRiderAttack Attack = bRanged ? FCrossguardRiderAttack::ForAIRanged() : FCrossguardRiderAttack::ForAIMelee(GetWieldedWeapon());
	const FCrossguardAttackRiders Riders = UCrossguardWeaponAttackAbility::GatherRiders(Attack, ASC);
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack,
			AttackDamage * Riders.WeaponMultiplier + Riders.WeaponFlat + Riders.AttackAdd);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, Riders.MagicAdd);
		if (bRanged)
		{
			ShootArrow(Target, Spec, Riders);
		}
		else
		{
			ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
			UCrossguardWeaponAttackAbility::ApplyRiders(Riders, TargetASC);
			UCrossguardWeaponAttackAbility::FinishMeleeHits(Riders, ASC, {Target});
		}
	}
	NextAttackTime = Now + AttackInterval;
	return true;
}

void ACrossguardAICharacter::ShootArrow(AActor* Target, const FGameplayEffectSpecHandle& Spec, const FCrossguardAttackRiders& Riders)
{
	UWorld* World = GetWorld();
	if (World == nullptr || ArrowClass == nullptr)
	{
		return;
	}
	// From the eyes straight at the target's middle (arrows fly without gravity).
	FVector Eyes;
	FRotator Unused;
	GetActorEyesViewPoint(Eyes, Unused);
	const FRotator Aim = (Target->GetActorLocation() - Eyes).Rotation();
	const FTransform SpawnTransform(Aim, Eyes + Aim.Vector() * 50.0f);
	ACrossguardProjectile* Arrow = World->SpawnActorDeferred<ACrossguardProjectile>(ArrowClass, SpawnTransform, this, this,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Arrow != nullptr)
	{
		Arrow->DamageSpec = Spec;
		Arrow->OnHitSpecs = Riders.OnHitSpecs;
		Arrow->bCancelSkillsOnHit = Riders.bCancelSkills;
		Arrow->FinishSpawning(SpawnTransform);
	}
}
