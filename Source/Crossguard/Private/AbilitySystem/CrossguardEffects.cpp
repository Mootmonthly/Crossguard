#include "AbilitySystem/CrossguardEffects.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardDamageExecution.h"
#include "AbilitySystem/CrossguardHealingExecution.h"

namespace CrossguardSetByCaller
{
	const FName Cooldown(TEXT("Cooldown"));
	const FName Duration(TEXT("Duration"));
	const FName HealPerTick(TEXT("HealPerTick"));
}

namespace
{
	FGameplayEffectModifierMagnitude SetByCallerMagnitude(FName DataName)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataName = DataName;
		return FGameplayEffectModifierMagnitude(SetByCaller);
	}

	// Attributes items can modify. The SetByCaller name is the attribute's name.
	const TArray<FGameplayAttribute>& EquipmentAttributes()
	{
		static const TArray<FGameplayAttribute> Attributes = {
			UCrossguardAttributeSet::GetStrengthAttribute(),
			UCrossguardAttributeSet::GetAgilityAttribute(),
			UCrossguardAttributeSet::GetIntelligenceAttribute(),
			UCrossguardAttributeSet::GetMaxHealthAttribute(),
			UCrossguardAttributeSet::GetMaxHealthBonusAttribute(),
			UCrossguardAttributeSet::GetAdditionalAttackDamageAttribute(),
			UCrossguardAttributeSet::GetAttackDamageBonusAttribute(),
			UCrossguardAttributeSet::GetAttackPenetrationAttribute(),
			UCrossguardAttributeSet::GetAttackResistanceAttribute(),
			UCrossguardAttributeSet::GetAdditionalMagicDamageAttribute(),
			UCrossguardAttributeSet::GetMagicDamageBonusAttribute(),
			UCrossguardAttributeSet::GetMagicResistanceAttribute(),
			UCrossguardAttributeSet::GetMagicPenetrationAttribute(),
			UCrossguardAttributeSet::GetTrueDamageAttribute(),
			UCrossguardAttributeSet::GetMoveSpeedAttribute(),
			UCrossguardAttributeSet::GetActionSpeedAttribute(),
			UCrossguardAttributeSet::GetDexterityAttribute(),
			UCrossguardAttributeSet::GetActiveCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetSpellCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetArtCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetActiveCastTimeReductionAttribute(),
			UCrossguardAttributeSet::GetSpellCastTimeReductionAttribute(),
			UCrossguardAttributeSet::GetArtCastTimeReductionAttribute(),
			UCrossguardAttributeSet::GetEmptyArmorSlotsAttribute(),
		};
		return Attributes;
	}
}

namespace
{
	// Attributes a passive skill can modify. The SetByCaller name is the attribute's name.
	const TArray<FGameplayAttribute>& SkillStatAttributes()
	{
		static const TArray<FGameplayAttribute> Attributes = {
			UCrossguardAttributeSet::GetStrengthAttribute(),
			UCrossguardAttributeSet::GetAgilityAttribute(),
			UCrossguardAttributeSet::GetIntelligenceAttribute(),
			UCrossguardAttributeSet::GetMaxHealthAttribute(),
			UCrossguardAttributeSet::GetMaxHealthBonusAttribute(),
			UCrossguardAttributeSet::GetAdditionalAttackDamageAttribute(),
			UCrossguardAttributeSet::GetAttackDamageBonusAttribute(),
			UCrossguardAttributeSet::GetAttackPenetrationAttribute(),
			UCrossguardAttributeSet::GetAttackResistanceAttribute(),
			UCrossguardAttributeSet::GetAdditionalMagicDamageAttribute(),
			UCrossguardAttributeSet::GetMagicDamageBonusAttribute(),
			UCrossguardAttributeSet::GetMagicResistanceAttribute(),
			UCrossguardAttributeSet::GetMagicPenetrationAttribute(),
			UCrossguardAttributeSet::GetAdditionalPhysicalHealingAttribute(),
			UCrossguardAttributeSet::GetPhysicalHealingBonusAttribute(),
			UCrossguardAttributeSet::GetAdditionalMagicalHealingAttribute(),
			UCrossguardAttributeSet::GetMagicalHealingBonusAttribute(),
			UCrossguardAttributeSet::GetMoveSpeedAttribute(),
			UCrossguardAttributeSet::GetActionSpeedAttribute(),
			UCrossguardAttributeSet::GetDexterityAttribute(),
			UCrossguardAttributeSet::GetActiveCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetSpellCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetArtCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetMeleeCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetRangedCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetMinionCooldownReductionAttribute(),
			UCrossguardAttributeSet::GetActiveCastTimeReductionAttribute(),
			UCrossguardAttributeSet::GetSpellCastTimeReductionAttribute(),
			UCrossguardAttributeSet::GetArtCastTimeReductionAttribute(),
		};
		return Attributes;
	}

	void AddSetByCallerModifiers(TArray<FGameplayModifierInfo>& Modifiers, bool bMultiply)
	{
		for (const FGameplayAttribute& Attribute : SkillStatAttributes())
		{
			FGameplayModifierInfo Modifier;
			Modifier.Attribute = Attribute;
			Modifier.ModifierOp = bMultiply ? EGameplayModOp::Multiplicitive : EGameplayModOp::Additive;
			Modifier.ModifierMagnitude = SetByCallerMagnitude(FName(*Attribute.GetName()));
			Modifiers.Add(Modifier);
		}
	}
}

UCrossguardSkillStatsEffect::UCrossguardSkillStatsEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	AddSetByCallerModifiers(Modifiers, false);
}

const TArray<FName>& UCrossguardSkillStatsEffect::GetStatNames()
{
	static const TArray<FName> Names = []
	{
		TArray<FName> Result;
		for (const FGameplayAttribute& Attribute : SkillStatAttributes())
		{
			Result.Add(FName(*Attribute.GetName()));
		}
		return Result;
	}();
	return Names;
}

UCrossguardStatusEffect::UCrossguardStatusEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = SetByCallerMagnitude(CrossguardSetByCaller::Duration);
}

UCrossguardTimedStatsEffect::UCrossguardTimedStatsEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = SetByCallerMagnitude(CrossguardSetByCaller::Duration);
	AddSetByCallerModifiers(Modifiers, false);
}

UCrossguardMuteEffect::UCrossguardMuteEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = SetByCallerMagnitude(CrossguardSetByCaller::Duration);
	Period = FScalableFloat(1.0f);
	bExecutePeriodicEffectOnApplication = false;
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UCrossguardDamageExecution::StaticClass();
	Executions.Add(Execution);
}

UCrossguardTimedMultiplierEffect::UCrossguardTimedMultiplierEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = SetByCallerMagnitude(CrossguardSetByCaller::Duration);
	AddSetByCallerModifiers(Modifiers, true);
}

UCrossguardMarkerEffect::UCrossguardMarkerEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
}

UCrossguardSkillMultiplierEffect::UCrossguardSkillMultiplierEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	AddSetByCallerModifiers(Modifiers, true);
}

UCrossguardDamageEffect::UCrossguardDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UCrossguardDamageExecution::StaticClass();
	Executions.Add(Execution);
}

UCrossguardHealingEffect::UCrossguardHealingEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UCrossguardHealingExecution::StaticClass();
	Executions.Add(Execution);
}

UCrossguardCooldownEffect::UCrossguardCooldownEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = SetByCallerMagnitude(CrossguardSetByCaller::Cooldown);
}

UCrossguardHealOverTimeEffect::UCrossguardHealOverTimeEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = SetByCallerMagnitude(CrossguardSetByCaller::Duration);
	Period = FScalableFloat(TickSeconds);
	bExecutePeriodicEffectOnApplication = false;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UCrossguardAttributeSet::GetIncomingHealingAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = SetByCallerMagnitude(CrossguardSetByCaller::HealPerTick);
	Modifiers.Add(Modifier);
}

void UCrossguardHealOverTimeEffect::SetHealing(FGameplayEffectSpec& Spec, float TotalHealing, float Seconds)
{
	const int32 Ticks = FMath::Max(1, FMath::RoundToInt(Seconds / TickSeconds));
	Spec.SetSetByCallerMagnitude(CrossguardSetByCaller::HealPerTick, TotalHealing / static_cast<float>(Ticks));
	// Half a tick of slack so the last tick lands before the effect expires.
	Spec.SetSetByCallerMagnitude(CrossguardSetByCaller::Duration, Ticks * TickSeconds + TickSeconds * 0.5f);
}

UCrossguardEquipmentEffect::UCrossguardEquipmentEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	for (const FGameplayAttribute& Attribute : EquipmentAttributes())
	{
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = SetByCallerMagnitude(FName(*Attribute.GetName()));
		Modifiers.Add(Modifier);
	}
}

const TArray<FName>& UCrossguardEquipmentEffect::GetStatNames()
{
	static const TArray<FName> Names = []
	{
		TArray<FName> Result;
		for (const FGameplayAttribute& Attribute : EquipmentAttributes())
		{
			Result.Add(FName(*Attribute.GetName()));
		}
		return Result;
	}();
	return Names;
}
