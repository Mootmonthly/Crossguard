#include "AbilitySystem/CrossguardDamageExecution.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "CrossguardRules/Combat.h"

namespace
{
	struct FDamageStatics
	{
		DECLARE_ATTRIBUTE_CAPTUREDEF(AdditionalAttackDamage);
		DECLARE_ATTRIBUTE_CAPTUREDEF(AttackDamageBonus);
		DECLARE_ATTRIBUTE_CAPTUREDEF(AttackPenetration);
		DECLARE_ATTRIBUTE_CAPTUREDEF(AttackResistance);
		DECLARE_ATTRIBUTE_CAPTUREDEF(AdditionalMagicDamage);
		DECLARE_ATTRIBUTE_CAPTUREDEF(MagicDamageBonus);
		DECLARE_ATTRIBUTE_CAPTUREDEF(MagicResistance);

		FDamageStatics()
		{
			// Source stats are read when the effect executes, not when the spec is made.
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, AdditionalAttackDamage, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, AttackDamageBonus, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, AttackPenetration, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, AttackResistance, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, AdditionalMagicDamage, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, MagicDamageBonus, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, MagicResistance, Target, false);
		}
	};

	const FDamageStatics& DamageStatics()
	{
		static FDamageStatics Statics;
		return Statics;
	}

	float Capture(const FGameplayEffectCustomExecutionParameters& Params, const FAggregatorEvaluateParameters& EvalParams,
		const FGameplayEffectAttributeCaptureDefinition& Definition)
	{
		float Value = 0.0f;
		Params.AttemptCalculateCapturedAttributeMagnitude(Definition, EvalParams, Value);
		return Value;
	}
}

UCrossguardDamageExecution::UCrossguardDamageExecution()
{
	const FDamageStatics& Statics = DamageStatics();
	RelevantAttributesToCapture.Add(Statics.AdditionalAttackDamageDef);
	RelevantAttributesToCapture.Add(Statics.AttackDamageBonusDef);
	RelevantAttributesToCapture.Add(Statics.AttackPenetrationDef);
	RelevantAttributesToCapture.Add(Statics.AttackResistanceDef);
	RelevantAttributesToCapture.Add(Statics.AdditionalMagicDamageDef);
	RelevantAttributesToCapture.Add(Statics.MagicDamageBonusDef);
	RelevantAttributesToCapture.Add(Statics.MagicResistanceDef);
}

void UCrossguardDamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	const FDamageStatics& Statics = DamageStatics();

	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	const float Coefficient = Spec.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Coefficient, false, 1.0f);
	const float AttackBase = Spec.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, false, 0.0f);
	const float MagicBase = Spec.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, false, 0.0f);

	double Total = 0.0;
	if (AttackBase > 0.0f)
	{
		Crossguard::FDamageInput Input;
		Input.Base = AttackBase;
		Input.Flat = Capture(ExecutionParams, EvalParams, Statics.AdditionalAttackDamageDef);
		Input.Bonus = Capture(ExecutionParams, EvalParams, Statics.AttackDamageBonusDef);
		Input.Coefficient = Coefficient;
		Input.Resistance = Capture(ExecutionParams, EvalParams, Statics.AttackResistanceDef);
		Input.Penetration = Capture(ExecutionParams, EvalParams, Statics.AttackPenetrationDef);
		Total += Crossguard::CalculateDamage(Input);
	}
	if (MagicBase > 0.0f)
	{
		// There is no magic penetration stat.
		Crossguard::FDamageInput Input;
		Input.Base = MagicBase;
		Input.Flat = Capture(ExecutionParams, EvalParams, Statics.AdditionalMagicDamageDef);
		Input.Bonus = Capture(ExecutionParams, EvalParams, Statics.MagicDamageBonusDef);
		Input.Coefficient = Coefficient;
		Input.Resistance = Capture(ExecutionParams, EvalParams, Statics.MagicResistanceDef);
		Total += Crossguard::CalculateDamage(Input);
	}

	if (Total > 0.0)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			UCrossguardAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, static_cast<float>(Total)));
	}
}
