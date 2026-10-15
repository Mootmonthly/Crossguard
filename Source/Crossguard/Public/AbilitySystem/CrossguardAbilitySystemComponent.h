#pragma once

#include "AbilitySystemComponent.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Config.h"

#include "CrossguardAbilitySystemComponent.generated.h"

UENUM(BlueprintType)
enum class ECrossguardSkillCategory : uint8
{
	Active,
	Spell,
	Art,
	Passive,
};

// A character's free attribute allocation (docs/design-decisions.md, "Levels").
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardAttributeAllocation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	int32 Strength = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	int32 Agility = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes", meta = (ClampMin = "0"))
	int32 Intelligence = 10;
};

// Owns the link between GAS attributes and the CrossguardRules formulas.
UCLASS()
class CROSSGUARD_API UCrossguardAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;

	// Server only. Validates the allocation for Level, sets STR/AGI/INT base values, derives every
	// other stat and fills Health. Returns false (and changes nothing) if the allocation is invalid.
	bool InitializeAttributes(int32 Level, const FCrossguardAttributeAllocation& Allocation);

	// Total CDR for an ability: its category's CDR plus every matching filter
	// (Melee, Ranged, Minion), summed and capped at CdrCap.
	float GetCooldownReduction(ECrossguardSkillCategory Category, const FGameplayTagContainer& SkillTags) const;

	// Cast time reduction for a category. Uncapped.
	float GetCastTimeReduction(ECrossguardSkillCategory Category) const;

	const Crossguard::FProgressionConfig& GetProgressionConfig() const { return ProgressionConfig; }
	const Crossguard::FScalingConfig& GetScalingConfig() const { return ScalingConfig; }

private:
	// Rewrites the base value of every attribute-derived stat from current STR/AGI/INT and MHPB.
	// Server only; clients receive the results through replication.
	void RecomputeDerivedStats();
	void OnDerivationInputChanged(const FOnAttributeChangeData& Data);
	void BindDerivationInputs();

	Crossguard::FProgressionConfig ProgressionConfig;
	Crossguard::FScalingConfig ScalingConfig;
	bool bDerivationInputsBound = false;
};
