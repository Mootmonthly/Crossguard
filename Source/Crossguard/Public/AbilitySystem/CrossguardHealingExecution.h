#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"

#include "CrossguardHealingExecution.generated.h"

// Healing formula from docs/design-decisions.md, via Crossguard::CalculateHealing.
//
// Inputs (SetByCaller on the effect spec):
// - SetByCaller.Healing.Physical: base Physical Healing. Optional.
// - SetByCaller.Healing.Magical: base Magical Healing. Optional.
// - SetByCaller.Coefficient: fraction of PHB/MHB that applies. Optional, default 1.
// Bonuses come from the source (the healer). Output: IncomingHealing on the target.
UCLASS()
class CROSSGUARD_API UCrossguardHealingExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UCrossguardHealingExecution();

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
