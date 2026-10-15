#include "Skills/CrossguardSkillCatalog.h"

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "Engine/DataTable.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "CrossguardSkillCatalog"

const FCrossguardSkillRow* UCrossguardSkillCatalog::FindSkill(FName SkillId) const
{
	if (SkillTable == nullptr)
	{
		return nullptr;
	}
	return SkillTable->FindRow<FCrossguardSkillRow>(SkillId, TEXT("UCrossguardSkillCatalog::FindSkill"), false);
}

TSubclassOf<UCrossguardGameplayAbility> UCrossguardSkillCatalog::FindAbilityClass(FName SkillId) const
{
	const TSubclassOf<UCrossguardGameplayAbility>* Found = AbilityClasses.Find(SkillId);
	return Found != nullptr ? *Found : nullptr;
}

Crossguard::FSkillCatalog UCrossguardSkillCatalog::BuildRulesCatalog() const
{
	Crossguard::FSkillCatalog Catalog;
	if (SkillTable == nullptr)
	{
		return Catalog;
	}

	SkillTable->ForeachRow<FCrossguardSkillRow>(TEXT("UCrossguardSkillCatalog::BuildRulesCatalog"),
		[&Catalog](const FName& RowName, const FCrossguardSkillRow& Row)
		{
			Crossguard::FSkillDefinition Definition;
			// Lowercase: the rules library compares ids case-sensitively.
			Definition.Id = TCHAR_TO_UTF8(*RowName.ToString().ToLower());
			Definition.CapacityCost = Row.CapacityCost;
			for (const FName& Included : Row.Includes)
			{
				Definition.Includes.emplace_back(TCHAR_TO_UTF8(*Included.ToString().ToLower()));
			}
			for (const FName& Granted : Row.GrantsWhileActive)
			{
				Definition.GrantsWhileActive.emplace_back(TCHAR_TO_UTF8(*Granted.ToString().ToLower()));
			}
			Catalog.Add(Definition);
		});
	return Catalog;
}

#if WITH_EDITOR
EDataValidationResult UCrossguardSkillCatalog::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (SkillTable == nullptr)
	{
		Context.AddError(LOCTEXT("NoTable", "SkillTable is not set."));
		return EDataValidationResult::Invalid;
	}

	for (const TPair<FName, TSubclassOf<UCrossguardGameplayAbility>>& Pair : AbilityClasses)
	{
		if (FindSkill(Pair.Key) == nullptr)
		{
			Context.AddError(FText::Format(LOCTEXT("UnknownSkill", "AbilityClasses has '{0}', which is not a row in SkillTable."),
				FText::FromName(Pair.Key)));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (Pair.Value == nullptr)
		{
			Context.AddError(FText::Format(LOCTEXT("NoClass", "AbilityClasses['{0}'] has no class."), FText::FromName(Pair.Key)));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		const UCrossguardGameplayAbility* Ability = Pair.Value->GetDefaultObject<UCrossguardGameplayAbility>();
		if (Ability->SkillId != Pair.Key)
		{
			Context.AddError(FText::Format(LOCTEXT("IdMismatch", "{0} has SkillId '{1}' but is registered as '{2}'."),
				FText::FromString(Pair.Value->GetName()), FText::FromName(Ability->SkillId), FText::FromName(Pair.Key)));
			Result = EDataValidationResult::Invalid;
		}
		const FCrossguardSkillRow* Row = FindSkill(Pair.Key);
		if (Ability->SkillCategory != Row->Category)
		{
			Context.AddError(FText::Format(LOCTEXT("CategoryMismatch", "{0}'s SkillCategory does not match the '{1}' row."),
				FText::FromString(Pair.Value->GetName()), FText::FromName(Pair.Key)));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
