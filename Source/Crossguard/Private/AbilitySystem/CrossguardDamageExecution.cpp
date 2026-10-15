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
		DECLARE_ATTRIBUTE_CAPTUREDEF(MagicPenetration);
		DECLARE_ATTRIBUTE_CAPTUREDEF(TrueDamage);
		DECLARE_ATTRIBUTE_CAPTUREDEF(AttackShield);

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
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, MagicPenetration, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, TrueDamage, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UCrossguardAttributeSet, AttackShield, Target, false);
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
	RelevantAttributesToCapture.Add(Statics.AttackShieldDef);
	RelevantAttributesToCapture.Add(Statics.MagicPenetrationDef);
	RelevantAttributesToCapture.Add(Statics.TrueDamageDef);
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

	// True Damage is added once per hit, to whichever damage type comes first.
	const float TrueDamage = Capture(ExecutionParams, EvalParams, Statics.TrueDamageDef);

	double Total = 0.0;
	double AttackTotal = 0.0;
	if (AttackBase > 0.0f)
	{
		Crossguard::FDamageInput Input;
		Input.Base = AttackBase;
		Input.Flat = Capture(ExecutionParams, EvalParams, Statics.AdditionalAttackDamageDef);
		// Plus this hit's own bonus (a parry's riposte).
		Input.Bonus = Capture(ExecutionParams, EvalParams, Statics.AttackDamageBonusDef)
			+ Spec.GetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_AttackBonus, false, 0.0f);
		Input.Coefficient = Coefficient;
		Input.Resistance = Capture(ExecutionParams, EvalParams, Statics.AttackResistanceDef);
		Input.Penetration = Capture(ExecutionParams, EvalParams, Statics.AttackPenetrationDef);
		Input.TrueDamage = TrueDamage;
		AttackTotal = Crossguard::CalculateDamage(Input);
		Total += AttackTotal;
	}
	if (MagicBase > 0.0f)
	{
		Crossguard::FDamageInput Input;
		Input.Base = MagicBase;
		Input.Flat = Capture(ExecutionParams, EvalParams, Statics.AdditionalMagicDamageDef);
		Input.Bonus = Capture(ExecutionParams, EvalParams, Statics.MagicDamageBonusDef);
		Input.Coefficient = Coefficient;
		Input.Resistance = Capture(ExecutionParams, EvalParams, Statics.MagicResistanceDef);
		Input.Penetration = Capture(ExecutionParams, EvalParams, Statics.MagicPenetrationDef);
		Input.TrueDamage = AttackBase > 0.0f ? 0.0f : TrueDamage;
		Total += Crossguard::CalculateDamage(Input);
	}

	// A barrier ([Eldritch Shield]) absorbs Attack Damage first and is used up by it. True Damage
	// (added to the attack part above) goes through.
	const float Shield = Capture(ExecutionParams, EvalParams, Statics.AttackShieldDef);
	const double Absorbable = FMath::Max(AttackTotal - (AttackBase > 0.0f ? FMath::Max(static_cast<double>(TrueDamage), 0.0) : 0.0), 0.0);
	const double Absorbed = FMath::Min(Absorbable, static_cast<double>(FMath::Max(Shield, 0.0f)));
	if (Absorbed > 0.0)
	{
		Total -= Absorbed;
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			UCrossguardAttributeSet::GetAttackShieldAttribute(), EGameplayModOp::Additive, static_cast<float>(-Absorbed)));
	}

	if (Total > 0.0)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			UCrossguardAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, static_cast<float>(Total)));
	}
}
