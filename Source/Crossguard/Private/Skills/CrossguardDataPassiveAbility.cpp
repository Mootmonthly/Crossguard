#include "Skills/CrossguardDataPassiveAbility.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystemComponent.h"
#include "Crossguard.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Skills/CrossguardSkillCatalog.h"

namespace
{
	using FAttribute = FGameplayAttribute;

	// The attributes a stat modifier changes. Empty: not a stat a passive effect can hold.
	TArray<FAttribute> AttributesFor(ECrossguardStat Stat, ECrossguardSkillFilter Filter)
	{
		using Set = UCrossguardAttributeSet;
		switch (Stat)
		{
		case ECrossguardStat::Strength: return {Set::GetStrengthAttribute()};
		case ECrossguardStat::Agility: return {Set::GetAgilityAttribute()};
		case ECrossguardStat::Intelligence: return {Set::GetIntelligenceAttribute()};
		case ECrossguardStat::AllAttributes: return {Set::GetStrengthAttribute(), Set::GetAgilityAttribute(), Set::GetIntelligenceAttribute()};
		case ECrossguardStat::MaxHealth: return {Set::GetMaxHealthAttribute()};
		case ECrossguardStat::MaxHealthBonus: return {Set::GetMaxHealthBonusAttribute()};
		case ECrossguardStat::AdditionalAttackDamage: return {Set::GetAdditionalAttackDamageAttribute()};
		case ECrossguardStat::AttackDamageBonus: return {Set::GetAttackDamageBonusAttribute()};
		case ECrossguardStat::AttackResistance: return {Set::GetAttackResistanceAttribute()};
		case ECrossguardStat::AttackPenetration: return {Set::GetAttackPenetrationAttribute()};
		case ECrossguardStat::Dexterity: return {Set::GetDexterityAttribute()};
		case ECrossguardStat::ActionSpeed: return {Set::GetActionSpeedAttribute()};
		case ECrossguardStat::MoveSpeed: return {Set::GetMoveSpeedAttribute()};
		case ECrossguardStat::AdditionalPhysicalHealing: return {Set::GetAdditionalPhysicalHealingAttribute()};
		case ECrossguardStat::PhysicalHealingBonus: return {Set::GetPhysicalHealingBonusAttribute()};
		case ECrossguardStat::AdditionalMagicDamage: return {Set::GetAdditionalMagicDamageAttribute()};
		case ECrossguardStat::MagicDamageBonus: return {Set::GetMagicDamageBonusAttribute()};
		case ECrossguardStat::MagicResistance: return {Set::GetMagicResistanceAttribute()};
		case ECrossguardStat::AdditionalMagicalHealing: return {Set::GetAdditionalMagicalHealingAttribute()};
		case ECrossguardStat::MagicalHealingBonus: return {Set::GetMagicalHealingBonusAttribute()};
		case ECrossguardStat::CooldownReduction:
			switch (Filter)
			{
			case ECrossguardSkillFilter::Melee: return {Set::GetMeleeCooldownReductionAttribute()};
			case ECrossguardSkillFilter::Ranged: return {Set::GetRangedCooldownReductionAttribute()};
			case ECrossguardSkillFilter::Minion: return {Set::GetMinionCooldownReductionAttribute()};
			case ECrossguardSkillFilter::Active: return {Set::GetActiveCooldownReductionAttribute()};
			case ECrossguardSkillFilter::Spell: return {Set::GetSpellCooldownReductionAttribute()};
			case ECrossguardSkillFilter::Art: return {Set::GetArtCooldownReductionAttribute()};
			case ECrossguardSkillFilter::None:
				return {Set::GetActiveCooldownReductionAttribute(), Set::GetSpellCooldownReductionAttribute(), Set::GetArtCooldownReductionAttribute()};
			}
			return {};
		case ECrossguardStat::CastTimeReduction:
			switch (Filter)
			{
			case ECrossguardSkillFilter::Active: return {Set::GetActiveCastTimeReductionAttribute()};
			case ECrossguardSkillFilter::Spell: return {Set::GetSpellCastTimeReductionAttribute()};
			case ECrossguardSkillFilter::Art: return {Set::GetArtCastTimeReductionAttribute()};
			case ECrossguardSkillFilter::None:
				return {Set::GetActiveCastTimeReductionAttribute(), Set::GetSpellCastTimeReductionAttribute(), Set::GetArtCastTimeReductionAttribute()};
			case ECrossguardSkillFilter::Melee:
			case ECrossguardSkillFilter::Ranged:
			case ECrossguardSkillFilter::Minion:
				return {};
			}
			return {};
		// Base amounts belong to a skill's own damage or healing, not to a stat.
		case ECrossguardStat::AttackDamage:
		case ECrossguardStat::MagicDamage:
		case ECrossguardStat::PhysicalHealing:
		case ECrossguardStat::MagicalHealing:
			return {};
		}
		return {};
	}
}

void UCrossguardDataPassiveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// The base class applies PassiveEffects (none here) and keeps the ability active.
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}

	const UCrossguardSkillGrant* Grant = Cast<UCrossguardSkillGrant>(GetCurrentSourceObject());
	PlayedSkillId = Grant != nullptr ? Grant->SkillId : NAME_None;

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (UCrossguardEquipmentComponent* Equipment = Avatar != nullptr ? Avatar->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr)
	{
		Equipment->OnEquipmentChanged.AddUniqueDynamic(this, &UCrossguardDataPassiveAbility::Refresh);
	}
	Refresh();
}

void UCrossguardDataPassiveAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (UCrossguardEquipmentComponent* Equipment = Avatar != nullptr ? Avatar->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr)
	{
		Equipment->OnEquipmentChanged.RemoveDynamic(this, &UCrossguardDataPassiveAbility::Refresh);
	}
	RemoveStatEffects();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCrossguardDataPassiveAbility::RemoveStatEffects()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		for (const FActiveGameplayEffectHandle& Applied : StatEffects)
		{
			ASC->RemoveActiveGameplayEffect(Applied);
		}
	}
	StatEffects.Reset();
}

void UCrossguardDataPassiveAbility::Refresh()
{
	RemoveStatEffects();

	const UCrossguardSkillGrant* Grant = Cast<UCrossguardSkillGrant>(GetCurrentSourceObject());
	const FCrossguardSkillRow* Row = Grant != nullptr && Grant->Catalog != nullptr ? Grant->Catalog->FindSkill(PlayedSkillId) : nullptr;
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const UCrossguardEquipmentComponent* Equipment = Avatar != nullptr ? Avatar->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
	if (Row == nullptr || ASC == nullptr || !IsActive())
	{
		return;
	}

	// Sum per attribute: additive values, and multipliers as GAS aggregates them (1 + sum of (m - 1)).
	TMap<FName, float> Added;
	TMap<FName, float> Multiplied;
	for (const FCrossguardStatModifier& Modifier : Row->StatModifiers)
	{
		float Scale = 1.0f;
		bool bApplies = Modifier.Target == ECrossguardModifierTarget::Self && Modifier.DurationSec <= 0.0f;
		switch (Modifier.Condition.Type)
		{
		case ECrossguardConditionType::None:
			break;
		case ECrossguardConditionType::Wielding:
			bApplies = bApplies && Equipment != nullptr && Equipment->SatisfiesWeaponRequirement(Modifier.Condition.Weapons);
			break;
		case ECrossguardConditionType::Armor:
			bApplies = bApplies && Equipment != nullptr && Equipment->SatisfiesArmorRequirement(Modifier.Condition.Armor);
			break;
		case ECrossguardConditionType::PerEmptyArmorSlot:
			Scale = Equipment != nullptr ? static_cast<float>(Equipment->GetEmptyArmorSlotCount()) : 0.0f;
			break;
		case ECrossguardConditionType::OnActivation:
		case ECrossguardConditionType::NextAttack:
		case ECrossguardConditionType::Other:
			bApplies = false;
			break;
		}
		const TArray<FAttribute> Attributes = AttributesFor(Modifier.Stat, Modifier.SkillFilter);
		if (Attributes.IsEmpty())
		{
			UE_LOG(LogCrossguard, Verbose, TEXT("%s: stat %d is not built from data"), *PlayedSkillId.ToString(), static_cast<int32>(Modifier.Stat));
			continue;
		}
		if (!bApplies)
		{
			continue;
		}
		for (const FAttribute& Attribute : Attributes)
		{
			const FName Name(*Attribute.GetName());
			if (Modifier.Op == ECrossguardModifierOp::Multiply)
			{
				Multiplied.FindOrAdd(Name, 0.0f) += (Modifier.Value - 1.0f) * Scale;
			}
			else
			{
				Added.FindOrAdd(Name, 0.0f) += Modifier.Value * Scale;
			}
		}
	}

	const auto Apply = [this, ASC](TSubclassOf<UGameplayEffect> EffectClass, const TMap<FName, float>& Values, float Neutral, float Offset)
	{
		if (Values.IsEmpty())
		{
			return;
		}
		const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(EffectClass, GetAbilityLevel());
		if (!Spec.IsValid())
		{
			return;
		}
		// Every SetByCaller name must be set; untouched attributes get the neutral value.
		for (const FName& Name : UCrossguardSkillStatsEffect::GetStatNames())
		{
			const float* Value = Values.Find(Name);
			Spec.Data->SetSetByCallerMagnitude(Name, Value != nullptr ? *Value + Offset : Neutral);
		}
		const FActiveGameplayEffectHandle Applied = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		if (Applied.IsValid())
		{
			StatEffects.Add(Applied);
		}
	};
	Apply(UCrossguardSkillStatsEffect::StaticClass(), Added, 0.0f, 0.0f);
	// Multipliers were summed as (m - 1); the effect takes the multiplier itself.
	Apply(UCrossguardSkillMultiplierEffect::StaticClass(), Multiplied, 1.0f, 1.0f);
}
