#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UCrossguardGameplayAbility;

// Skills implemented in C++, by skill id. UCrossguardSkillCatalog falls back to this when its
// AbilityClasses map has no entry, so these skills work without any editor setup. An entry in
// AbilityClasses (for example a Blueprint subclass with visuals) wins over the native class.
namespace CrossguardNativeSkills
{
	CROSSGUARD_API TSubclassOf<UCrossguardGameplayAbility> Find(FName SkillId);

	// Every registered id with its class, for validation.
	CROSSGUARD_API const TMap<FName, TSubclassOf<UCrossguardGameplayAbility>>& All();
}
