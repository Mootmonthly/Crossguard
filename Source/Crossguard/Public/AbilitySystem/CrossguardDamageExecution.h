#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"

#include "CrossguardDamageExecution.generated.h"

// Damage formula from docs/design-decisions.md, via Crossguard::CalculateDamage.
//
// Inputs (SetByCaller on the effect spec):
// - SetByCaller.Damage.Attack: base Attack Damage (weapon AD or the skill's amount). Optional.
// - SetByCaller.Damage.Magic: base Magic Damage. Optional.
// - SetByCaller.Coefficient: fraction of ADB/MDB that applies. Optional, default 1.
// Both damage types may be set; they are resolved separately and summed.
// Output: IncomingDamage on the target.
UCLASS()
class CROSSGUARD_API UCrossguardDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UCrossguardDamageExecution();

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
