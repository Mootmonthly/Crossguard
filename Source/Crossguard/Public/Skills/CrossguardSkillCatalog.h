#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Loadout.h"
#include "Engine/DataAsset.h"
#include "Skills/CrossguardSkillTypes.h"

#include "CrossguardSkillCatalog.generated.h"

class UCrossguardGameplayAbility;
class UDataTable;

// Every skill in the game: the generated data table plus the ability class implementing each one.
// Ability classes live here, not in the table, so re-importing the table never loses them.
UCLASS(BlueprintType)
class CROSSGUARD_API UCrossguardSkillCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Imported from Data/Skills/SkillTable.json. Row struct: CrossguardSkillRow.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skills", meta = (RequiredAssetDataTags = "RowStructure=/Script/Crossguard.CrossguardSkillRow"))
	TObjectPtr<UDataTable> SkillTable;

	// Skill id -> ability. Skills without an entry can be equipped but grant nothing yet.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skills")
	TMap<FName, TSubclassOf<UCrossguardGameplayAbility>> AbilityClasses;

	const FCrossguardSkillRow* FindSkill(FName SkillId) const;
	TSubclassOf<UCrossguardGameplayAbility> FindAbilityClass(FName SkillId) const;

	// The table in the form CrossguardRules validates loadouts with. Built on each call.
	Crossguard::FSkillCatalog BuildRulesCatalog() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
