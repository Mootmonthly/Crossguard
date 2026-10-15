#include "AbilitySystem/CrossguardAbilitySystemComponent.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Crossguard.h"
#include "CrossguardRules/Combat.h"
#include "CrossguardRules/Progression.h"
#include "CrossguardRules/Stats.h"

void UCrossguardAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	if (IsOwnerActorAuthoritative())
	{
		BindDerivationInputs();
	}
}

void UCrossguardAbilitySystemComponent::BindDerivationInputs()
{
	if (bDerivationInputsBound)
	{
		return;
	}
	bDerivationInputsBound = true;

	for (const FGameplayAttribute& Attribute : {
			 UCrossguardAttributeSet::GetStrengthAttribute(),
			 UCrossguardAttributeSet::GetAgilityAttribute(),
			 UCrossguardAttributeSet::GetIntelligenceAttribute(),
			 UCrossguardAttributeSet::GetMaxHealthBonusAttribute(),
		 })
	{
		GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this, &UCrossguardAbilitySystemComponent::OnDerivationInputChanged);
	}
}

bool UCrossguardAbilitySystemComponent::InitializeAttributes(int32 Level, const FCrossguardAttributeAllocation& Allocation)
{
	if (!IsOwnerActorAuthoritative() || GetSet<UCrossguardAttributeSet>() == nullptr)
	{
		return false;
	}

	const Crossguard::FAttributes Attributes{Allocation.Strength, Allocation.Agility, Allocation.Intelligence};
	if (Crossguard::ValidateAllocation(ProgressionConfig, Level, Attributes) != Crossguard::EAllocationError::None)
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Invalid attribute allocation %d/%d/%d at level %d"),
			Allocation.Strength, Allocation.Agility, Allocation.Intelligence, Level);
		return false;
	}

	// Each change fires OnDerivationInputChanged; the explicit call below covers the case where
	// nothing changed.
	SetNumericAttributeBase(UCrossguardAttributeSet::GetStrengthAttribute(), Allocation.Strength);
	SetNumericAttributeBase(UCrossguardAttributeSet::GetAgilityAttribute(), Allocation.Agility);
	SetNumericAttributeBase(UCrossguardAttributeSet::GetIntelligenceAttribute(), Allocation.Intelligence);
	RecomputeDerivedStats();

	SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(), GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute()));
	return true;
}

void UCrossguardAbilitySystemComponent::OnDerivationInputChanged(const FOnAttributeChangeData& Data)
{
	RecomputeDerivedStats();
}

void UCrossguardAbilitySystemComponent::RecomputeDerivedStats()
{
	if (!IsOwnerActorAuthoritative() || GetSet<UCrossguardAttributeSet>() == nullptr)
	{
		return;
	}

	// Current values include skill and gear modifiers (e.g. Heroic Might, Unleash).
	const Crossguard::FAttributes Attributes{
		FMath::RoundToInt(GetNumericAttribute(UCrossguardAttributeSet::GetStrengthAttribute())),
		FMath::RoundToInt(GetNumericAttribute(UCrossguardAttributeSet::GetAgilityAttribute())),
		FMath::RoundToInt(GetNumericAttribute(UCrossguardAttributeSet::GetIntelligenceAttribute())),
	};
	const Crossguard::FDerivedStats Stats = Crossguard::DeriveStats(Attributes, ScalingConfig);
	const double Mhpb = GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthBonusAttribute());

	const TPair<FGameplayAttribute, double> BaseValues[] = {
		{UCrossguardAttributeSet::GetMaxHealthAttribute(), Crossguard::ApplyMaxHpBonus(Stats.MaxHp, Mhpb)},
		{UCrossguardAttributeSet::GetAttackDamageBonusAttribute(), Stats.Adb},
		{UCrossguardAttributeSet::GetAttackResistanceAttribute(), Stats.Ar},
		{UCrossguardAttributeSet::GetPhysicalHealingBonusAttribute(), Stats.Phb},
		{UCrossguardAttributeSet::GetActiveCastTimeReductionAttribute(), Stats.ActiveCastTimeReduction},
		{UCrossguardAttributeSet::GetActiveCooldownReductionAttribute(), Stats.ActiveCdr},
		{UCrossguardAttributeSet::GetMoveSpeedAttribute(), Stats.MoveSpeed},
		{UCrossguardAttributeSet::GetActionSpeedAttribute(), Stats.ActionSpeed},
		{UCrossguardAttributeSet::GetDexterityAttribute(), Stats.Dexterity},
		{UCrossguardAttributeSet::GetArtCastTimeReductionAttribute(), Stats.ArtCastTimeReduction},
		{UCrossguardAttributeSet::GetArtCooldownReductionAttribute(), Stats.ArtCdr},
		{UCrossguardAttributeSet::GetMagicDamageBonusAttribute(), Stats.Mdb},
		{UCrossguardAttributeSet::GetMagicResistanceAttribute(), Stats.Mr},
		{UCrossguardAttributeSet::GetMagicalHealingBonusAttribute(), Stats.Mhb},
		{UCrossguardAttributeSet::GetSpellCastTimeReductionAttribute(), Stats.SpellCastTimeReduction},
		{UCrossguardAttributeSet::GetSpellCooldownReductionAttribute(), Stats.SpellCdr},
	};
	for (const TPair<FGameplayAttribute, double>& Pair : BaseValues)
	{
		SetNumericAttributeBase(Pair.Key, static_cast<float>(Pair.Value));
	}
}

float UCrossguardAbilitySystemComponent::GetCooldownReduction(ECrossguardSkillCategory Category, const FGameplayTagContainer& SkillTags) const
{
	std::vector<double> Sources;
	switch (Category)
	{
	case ECrossguardSkillCategory::Active:
		Sources.push_back(GetNumericAttribute(UCrossguardAttributeSet::GetActiveCooldownReductionAttribute()));
		break;
	case ECrossguardSkillCategory::Spell:
		Sources.push_back(GetNumericAttribute(UCrossguardAttributeSet::GetSpellCooldownReductionAttribute()));
		break;
	case ECrossguardSkillCategory::Art:
		Sources.push_back(GetNumericAttribute(UCrossguardAttributeSet::GetArtCooldownReductionAttribute()));
		break;
	case ECrossguardSkillCategory::Passive:
		return 0.0f;
	}

	if (SkillTags.HasTag(CrossguardTags::Skill_Weapon_Melee))
	{
		Sources.push_back(GetNumericAttribute(UCrossguardAttributeSet::GetMeleeCooldownReductionAttribute()));
	}
	if (SkillTags.HasTag(CrossguardTags::Skill_Weapon_Ranged))
	{
		Sources.push_back(GetNumericAttribute(UCrossguardAttributeSet::GetRangedCooldownReductionAttribute()));
	}
	if (SkillTags.HasTag(CrossguardTags::Skill_Minion))
	{
		Sources.push_back(GetNumericAttribute(UCrossguardAttributeSet::GetMinionCooldownReductionAttribute()));
	}
	return static_cast<float>(Crossguard::CombineReductions(Sources, ScalingConfig.CdrCap));
}

float UCrossguardAbilitySystemComponent::GetCastTimeReduction(ECrossguardSkillCategory Category) const
{
	switch (Category)
	{
	case ECrossguardSkillCategory::Active:
		return GetNumericAttribute(UCrossguardAttributeSet::GetActiveCastTimeReductionAttribute());
	case ECrossguardSkillCategory::Spell:
		return GetNumericAttribute(UCrossguardAttributeSet::GetSpellCastTimeReductionAttribute());
	case ECrossguardSkillCategory::Art:
		return GetNumericAttribute(UCrossguardAttributeSet::GetArtCastTimeReductionAttribute());
	case ECrossguardSkillCategory::Passive:
		return 0.0f;
	}
	return 0.0f;
}
