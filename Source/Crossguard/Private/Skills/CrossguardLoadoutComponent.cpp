#include "Skills/CrossguardLoadoutComponent.h"

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Crossguard.h"
#include "CrossguardRules/Loadout.h"
#include "CrossguardRules/Progression.h"
#include "Game/CrossguardPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Skills/CrossguardDataPassiveAbility.h"
#include "Skills/CrossguardSkillCatalog.h"

#include <algorithm>

namespace
{
	ECrossguardLoadoutError ToLoadoutError(Crossguard::ELoadoutError Error)
	{
		switch (Error)
		{
		case Crossguard::ELoadoutError::None: return ECrossguardLoadoutError::None;
		case Crossguard::ELoadoutError::UnknownSkill: return ECrossguardLoadoutError::UnknownSkill;
		case Crossguard::ELoadoutError::NotLearned: return ECrossguardLoadoutError::NotLearned;
		case Crossguard::ELoadoutError::DuplicateSkill: return ECrossguardLoadoutError::DuplicateSkill;
		case Crossguard::ELoadoutError::SupersededSkill: return ECrossguardLoadoutError::SupersededSkill;
		case Crossguard::ELoadoutError::OverCapacity: return ECrossguardLoadoutError::OverCapacity;
		}
		return ECrossguardLoadoutError::NotReady;
	}

	// FName compares case-insensitively, the rules library does not; skill ids are lowercase.
	std::string ToRulesId(FName Name)
	{
		return std::string(TCHAR_TO_UTF8(*Name.ToString().ToLower()));
	}
}

UCrossguardLoadoutComponent::UCrossguardLoadoutComponent()
{
	SetIsReplicatedByDefault(true);
}

void UCrossguardLoadoutComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Other players see what you have equipped (for building teams).
	DOREPLIFETIME(UCrossguardLoadoutComponent, EquippedSkills);
}

UCrossguardAbilitySystemComponent* UCrossguardLoadoutComponent::GetAbilitySystem() const
{
	return Cast<UCrossguardAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()));
}

int32 UCrossguardLoadoutComponent::GetCapacity() const
{
	const ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(GetOwner());
	const UCrossguardAbilitySystemComponent* ASC = GetAbilitySystem();
	if (Character == nullptr || ASC == nullptr)
	{
		return 0;
	}
	return Crossguard::SkillCapacityAtLevel(ASC->GetProgressionConfig(), Character->Level);
}

FCrossguardLoadoutResult UCrossguardLoadoutComponent::ValidateLoadout(const TArray<FName>& Skills) const
{
	FCrossguardLoadoutResult Result;
	if (SkillCatalog == nullptr || GetAbilitySystem() == nullptr)
	{
		Result.Error = ECrossguardLoadoutError::NotReady;
		return Result;
	}

	std::vector<std::string> Ids;
	Ids.reserve(Skills.Num());
	for (const FName& Skill : Skills)
	{
		Ids.push_back(ToRulesId(Skill));
	}

	// Players can only equip skills in their Skill Library. AI pawns have no player state and no
	// library check.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const ACrossguardPlayerState* Progression = Pawn != nullptr ? Pawn->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	Crossguard::FSkillLibrary Library;
	if (Progression != nullptr)
	{
		for (const FName& Learned : Progression->GetSkillLibrary())
		{
			Library.Add(ToRulesId(Learned));
		}
	}

	Result.Capacity = GetCapacity();
	const Crossguard::FLoadoutResult Rules = Crossguard::ValidateLoadout(
		SkillCatalog->BuildRulesCatalog(), Ids, Result.Capacity, Progression != nullptr ? &Library : nullptr);
	Result.Error = ToLoadoutError(Rules.Error);
	Result.SkillId = Rules.SkillId.empty() ? NAME_None : FName(UTF8_TO_TCHAR(Rules.SkillId.c_str()));
	Result.OtherSkillId = Rules.OtherSkillId.empty() ? NAME_None : FName(UTF8_TO_TCHAR(Rules.OtherSkillId.c_str()));
	Result.TotalCost = Rules.TotalCost;
	return Result;
}

void UCrossguardLoadoutComponent::RequestLoadout(const TArray<FName>& Skills)
{
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		ApplyLoadout(Skills);
	}
	else
	{
		ServerRequestLoadout(Skills);
	}
}

void UCrossguardLoadoutComponent::ServerRequestLoadout_Implementation(const TArray<FName>& Skills)
{
	ApplyLoadout(Skills);
}

void UCrossguardLoadoutComponent::ClientLoadoutRejected_Implementation(const FCrossguardLoadoutResult& Result)
{
	OnLoadoutRejected.Broadcast(Result);
}

void UCrossguardLoadoutComponent::OnRep_EquippedSkills()
{
	OnLoadoutChanged.Broadcast();
}

void UCrossguardLoadoutComponent::ApplyLoadout(const TArray<FName>& Skills)
{
	const FCrossguardLoadoutResult Result = ValidateLoadout(Skills);
	UCrossguardAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!Result.IsValid() || ASC == nullptr)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Loadout rejected for %s: error %d (%s / %s)"), *GetNameSafe(GetOwner()),
			static_cast<int32>(Result.Error), *Result.SkillId.ToString(), *Result.OtherSkillId.ToString());
		// Reaches the owning client; on a listen server host it runs locally.
		ClientLoadoutRejected(Result);
		return;
	}

	ClearGrantedAbilities(*ASC);

	std::vector<std::string> Ids;
	Ids.reserve(Skills.Num());
	for (const FName& Skill : Skills)
	{
		Ids.push_back(ToRulesId(Skill));
	}

	// Bundle components are granted as their own abilities; a component shared by two bundles once.
	// Sorted so grant order is deterministic.
	const std::unordered_set<std::string> EffectiveSet = Crossguard::ResolveLoadoutEffects(SkillCatalog->BuildRulesCatalog(), Ids);
	std::vector<std::string> EffectiveIds(EffectiveSet.begin(), EffectiveSet.end());
	std::sort(EffectiveIds.begin(), EffectiveIds.end());
	for (const std::string& Effective : EffectiveIds)
	{
		const FName SkillId(UTF8_TO_TCHAR(Effective.c_str()));
		const TSubclassOf<UCrossguardGameplayAbility> AbilityClass = SkillCatalog->FindAbilityClass(SkillId);
		if (AbilityClass == nullptr)
		{
			const FCrossguardSkillRow* Row = SkillCatalog->FindSkill(SkillId);
			if (Row != nullptr && Row->Category == ECrossguardSkillCategory::Passive)
			{
				// Played from its data row.
				UCrossguardSkillGrant* Grant = NewObject<UCrossguardSkillGrant>(this);
				Grant->SkillId = SkillId;
				Grant->Catalog = SkillCatalog;
				SkillGrants.Add(Grant);
				GrantedAbilities.Add(ASC->GiveAbility(FGameplayAbilitySpec(UCrossguardDataPassiveAbility::StaticClass(), 1, INDEX_NONE, Grant)));
				continue;
			}
			UE_LOG(LogCrossguard, Verbose, TEXT("Skill %s has no ability class yet"), *SkillId.ToString());
			continue;
		}
		GrantedAbilities.Add(ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this)));
	}

	EquippedSkills = Skills;
	OnLoadoutChanged.Broadcast();
}

void UCrossguardLoadoutComponent::ClearGrantedAbilities(UCrossguardAbilitySystemComponent& ASC)
{
	for (const FGameplayAbilitySpecHandle& Handle : GrantedAbilities)
	{
		// Cancel first so an active passive runs EndAbility and removes its effects.
		ASC.CancelAbilityHandle(Handle);
		ASC.ClearAbility(Handle);
	}
	GrantedAbilities.Reset();
	SkillGrants.Reset();
}
