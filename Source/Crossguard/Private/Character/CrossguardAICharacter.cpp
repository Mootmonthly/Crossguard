#include "Character/CrossguardAICharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemGlobals.h"
#include "Combat/CrossguardGuard.h"
#include "Combat/CrossguardProjectile.h"
#include "Combat/CrossguardSpellProjectile.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Crossguard.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/Loadout.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Skills/CrossguardDataPassiveAbility.h"
#include "Skills/CrossguardNativeSkills.h"
#include "Skills/CrossguardSkillAbility.h"
#include "Skills/CrossguardSkillCatalog.h"
#include "Skills/CrossguardSpellSkills.h"
#include "Skills/CrossguardSupportSkills.h"
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
		// Rules ids are lowercase (as the loadout's). Ids the rules catalog does not know stay as they
		// are, so native skills still work (and unknown ones still warn below).
		const Crossguard::FSkillCatalog Rules = SkillCatalog->BuildRulesCatalog();
		std::vector<std::string> Ids;
		Effective.Reset();
		for (const FName& SkillId : SkillIds)
		{
			const std::string Id(TCHAR_TO_UTF8(*SkillId.ToString().ToLower()));
			if (Rules.Find(Id) != nullptr)
			{
				Ids.push_back(Id);
			}
			else
			{
				Effective.AddUnique(SkillId);
			}
		}
		const std::unordered_set<std::string> Resolved = Crossguard::ResolveLoadoutEffects(Rules, Ids);
		std::vector<std::string> Sorted(Resolved.begin(), Resolved.end());
		std::sort(Sorted.begin(), Sorted.end());
		for (const std::string& Id : Sorted)
		{
			Effective.AddUnique(FName(UTF8_TO_TCHAR(Id.c_str())));
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
		// skills (a breath) keep aiming there; a failed try leaves the previous aim alone.
		const TWeakObjectPtr<AActor> PreviousAim = SkillAimTarget;
		SkillAimTarget = Skill->AIAimsAtSelf() ? static_cast<AActor*>(this) : Target;
		const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.0f, ToTarget.Rotation().Yaw, 0.0f));
		const bool bActivated = ASC->TryActivateAbility(Handle);
		// Used: still running, or its cooldown started. A skill that gave up without committing (Mute
		// with nobody in the line) must not starve the others.
		const FGameplayTagContainer* CooldownTags = Skill->GetCooldownTags();
		const FGameplayAbilitySpec* After = ASC->FindAbilitySpecFromHandle(Handle);
		if (bActivated && ((After != nullptr && After->IsActive()) || (CooldownTags != nullptr && ASC->HasAnyMatchingGameplayTags(*CooldownTags))))
		{
			return true;
		}
		SkillAimTarget = PreviousAim;
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
	for (const ECrossguardWeaponType RequiredType : AnyOf)
	{
		Required.push_back(static_cast<Crossguard::EWeaponType>(RequiredType));
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
	if (!HasAuthority() || !IsAlive() || ASC == nullptr || TargetASC == nullptr || Now < NextAttackTime
		|| ASC->HasMatchingGameplayTag(CrossguardTags::State_Guarding) || ASC->HasMatchingGameplayTag(CrossguardTags::State_Stunned))
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
		BreakOwnStealth();
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
	const FCrossguardRiderAttack Attack = bRanged ? FCrossguardRiderAttack::ForAIRanged(GetWieldedWeapon())
		: FCrossguardRiderAttack::ForAIMelee(GetWieldedWeapon());
	// Attacking gives it away, as a player's attack does.
	BreakOwnStealth();
	const FCrossguardAttackRiders Riders = UCrossguardWeaponAttackAbility::GatherRiders(Attack, ASC);
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack,
			AttackDamage * Riders.WeaponMultiplier + Riders.WeaponFlat + Riders.AttackAdd);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, Riders.MagicAdd);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_AttackBonus, Riders.AttackBonusAdd);
		if (bRanged)
		{
			ShootArrow(Target, Spec, Riders);
		}
		else
		{
			// Through the target's guard; AI in reach may raise theirs first.
			CrossguardGuard::NotifyMeleeAttack(this, AttackRange, Target);
			NoteMeleeSwing(ImpactPower, AttackDamage);
			FCrossguardHit Hit;
			Hit.Attacker = this;
			Hit.ImpactPower = ImpactPower;
			FRotator Unused;
			GetActorEyesViewPoint(Hit.Origin, Unused);
			if (CrossguardGuard::ApplyHit(ASC, Target, Spec, Hit) > 0.0f)
			{
				UCrossguardWeaponAttackAbility::ApplyRiders(Riders, TargetASC);
				UCrossguardWeaponAttackAbility::FinishMeleeHits(Riders, ASC, {Target});
			}
		}
	}
	NextAttackTime = Now + ActionSpeedInterval(AttackInterval);
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
		Arrow->ImpactPower = ImpactPower;
		Arrow->OnHitSpecs = Riders.OnHitSpecs;
		Arrow->bCancelSkillsOnHit = Riders.bCancelSkills;
		Arrow->FinishSpawning(SpawnTransform);
	}
}

void ACrossguardAICharacter::BreakOwnStealth()
{
	UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	if (ASC == nullptr || !ASC->HasMatchingGameplayTag(CrossguardTags::State_Stealthed))
	{
		return;
	}
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive())
		{
			if (UCrossguardStealthAbility* Stealth = Cast<UCrossguardStealthAbility>(Spec.GetPrimaryInstance()))
			{
				Stealth->BreakStealth();
			}
		}
	}
}

float ACrossguardAICharacter::ActionSpeedInterval(float Seconds) const
{
	// Action Speed (Quickshot, Unleash's Agility) shortens it as it does a weapon's.
	const UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	const float ActionSpeed = ASC != nullptr ? ASC->GetNumericAttribute(UCrossguardAttributeSet::GetActionSpeedAttribute()) : 0.0f;
	return static_cast<float>(Crossguard::AttackInterval(Crossguard::FItemConfig(), 1.0, Seconds, ActionSpeed));
}

void ACrossguardAICharacter::RaiseGuard(AActor* Attacker, float Seconds)
{
	UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	if (!HasAuthority() || !IsAlive() || ASC == nullptr || Attacker == nullptr || ASC->HasMatchingGameplayTag(CrossguardTags::State_Guarding))
	{
		return;
	}
	if (GuardAbilityClass == nullptr || !ASC->TryActivateAbilityByClass(GuardAbilityClass))
	{
		return;
	}
	// Faces the swing, and its guard keeps facing it for the duration.
	const FVector ToAttacker = Attacker->GetActorLocation() - GetActorLocation();
	SetActorRotation(FRotator(0.0f, ToAttacker.Rotation().Yaw, 0.0f));
	GuardDirection = ToAttacker.GetSafeNormal2D();
	GetWorldTimerManager().SetTimer(GuardTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (UCrossguardAbilitySystemComponent* GuardASC = GetCrossguardAbilitySystemComponent())
		{
			const FGameplayTagContainer GuardTag(CrossguardTags::Ability_Guard);
			GuardASC->CancelAbilities(&GuardTag);
		}
	}), Seconds, false);
}
