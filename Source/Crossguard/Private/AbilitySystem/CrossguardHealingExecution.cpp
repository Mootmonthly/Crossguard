#include "AbilitySystem/CrossguardHealingExecution.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "CrossguardRules/Combat.h"

namespace
{
	struct FHealingStatics
	{
		DECLARE_ATTRIBUTE_CAPTUREDEF(AdditionalPhysicalHealing);
		DECLARE_ATTRIBUTE_CAPTUREDEF(PhysicalHealingBonus);
		DECLARE_ATTRIBUTE_CAPTUREDEF(AdditionalMagicalHealing);
		DECLARE_ATTRIBUTE_CAPTUREDEF(MagicalHealingBonus);

		FHealingStatics()
		{
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, AdditionalPhysicalHealing, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, PhysicalHealingBonus, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, AdditionalMagicalHealing, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, MagicalHealingBonus, Source, false);
		}
	};

	const FHealingStatics& HealingStatics()
	{
		static FHealingStatics Statics;
		return Statics;
	}

	float CaptureHealingStat(const FGameplayEffectCustomExecutionParameters& Params, const FAggregatorEvaluateParameters& EvalParams,
		const FGameplayEffectAttributeCaptureDefinition& Definition)
	{
		float Value = 0.0f;
		Params.AttemptCalculateCapturedAttributeMagnitude(Definition, EvalParams, Value);
		return Value;
	}
}

UCrossguardHealingExecution::UCrossguardHealingExecution()
{
	const FHealingStatics& Statics = HealingStatics();
	RelevantAttributesToCapture.Add(Statics.AdditionalPhysicalHealingDef);
	RelevantAttributesToCapture.Add(Statics.PhysicalHealingBonusDef);
	RelevantAttributesToCapture.Add(Statics.AdditionalMagicalHealingDef);
	RelevantAttributesToCapture.Add(Statics.MagicalHealingBonusDef);
}

void UCrossguardHealingExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	const FHealingStatics& Statics = HealingStatics();

	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	const float Coefficient = Spec.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Coefficient, false, 1.0f);
	const float PhysicalBase = Spec.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Healing_Physical, false, 0.0f);
	const float MagicalBase = Spec.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Healing_Magical, false, 0.0f);

	double Total = 0.0;
	if (PhysicalBase > 0.0f)
	{
		Total += Crossguard::CalculateHealing(PhysicalBase,
			CaptureHealingStat(ExecutionParams, EvalParams, Statics.AdditionalPhysicalHealingDef),
			CaptureHealingStat(ExecutionParams, EvalParams, Statics.PhysicalHealingBonusDef), Coefficient);
	}
	if (MagicalBase > 0.0f)
	{
		Total += Crossguard::CalculateHealing(MagicalBase,
			CaptureHealingStat(ExecutionParams, EvalParams, Statics.AdditionalMagicalHealingDef),
			CaptureHealingStat(ExecutionParams, EvalParams, Statics.MagicalHealingBonusDef), Coefficient);
	}

	if (Total > 0.0)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			UCrossguardAttributeSet::GetIncomingHealingAttribute(), EGameplayModOp::Additive, static_cast<float>(Total)));
	}
}
