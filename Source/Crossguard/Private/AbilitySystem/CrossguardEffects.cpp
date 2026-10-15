#include "AbilitySystem/CrossguardEffects.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardDamageExecution.h"
#include "AbilitySystem/CrossguardHealingExecution.h"

namespace CrossguardSetByCaller
{
	const FName Cooldown(TEXT("Cooldown"));
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
		};
		return Attributes;
	}
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
