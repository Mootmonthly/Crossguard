#include "Skills/CrossguardNativeSkills.h"

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "Skills/CrossguardArtSkills.h"
#include "Skills/CrossguardSpellSkills.h"

namespace CrossguardNativeSkills
{
	const TMap<FName, TSubclassOf<UCrossguardGameplayAbility>>& All()
	{
		static const TMap<FName, TSubclassOf<UCrossguardGameplayAbility>> Registry = []
		{
			TMap<FName, TSubclassOf<UCrossguardGameplayAbility>> Map;
			const auto Register = [&Map](TSubclassOf<UCrossguardGameplayAbility> Class)
			{
				Map.Add(Class->GetDefaultObject<UCrossguardGameplayAbility>()->SkillId, Class);
			};
			Register(UCrossguardFireballAbility::StaticClass());
			Register(UCrossguardHealAbility::StaticClass());
			Register(UCrossguardMuteAbility::StaticClass());
			Register(UCrossguardEldritchShieldAbility::StaticClass());
			Register(UCrossguardChopDownAbility::StaticClass());
			Register(UCrossguardSummonSkeletonsAbility::StaticClass());
			Register(UCrossguardShootingStarAbility::StaticClass());
			Register(UCrossguardDragonificationAbility::StaticClass());
			Register(UCrossguardFlareBreathAbility::StaticClass());
			Register(UCrossguardFlightAbility::StaticClass());
			return Map;
		}();
		return Registry;
	}

	TSubclassOf<UCrossguardGameplayAbility> Find(FName SkillId)
	{
		const TSubclassOf<UCrossguardGameplayAbility>* Found = All().Find(SkillId);
		return Found != nullptr ? *Found : nullptr;
	}
}
