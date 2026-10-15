#include "Skills/CrossguardNativeSkills.h"

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "Skills/CrossguardArtSkills.h"
#include "Skills/CrossguardAuraSkills.h"
#include "Skills/CrossguardSpellSkills.h"
#include "Skills/CrossguardSupportSkills.h"
#include "Skills/CrossguardWeaponSkills.h"

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
			Register(UCrossguardSummonTigerAbility::StaticClass());
			Register(UCrossguardSummonGreaterDemonAbility::StaticClass());
			Register(UCrossguardShootingStarAbility::StaticClass());
			Register(UCrossguardDragonificationAbility::StaticClass());
			Register(UCrossguardFlareBreathAbility::StaticClass());
			Register(UCrossguardFlightAbility::StaticClass());
			Register(UCrossguardSlashAbility::StaticClass());
			Register(UCrossguardMortalStrikeAbility::StaticClass());
			Register(UCrossguardShadeBlowAbility::StaticClass());
			Register(UCrossguardGougeAbility::StaticClass());
			Register(UCrossguardPurgeShotAbility::StaticClass());
			Register(UCrossguardNeckbiteAbility::StaticClass());
			Register(UCrossguardQuickshotAbility::StaticClass());
			Register(UCrossguardArcaneArrowsAbility::StaticClass());
			Register(UCrossguardCleaveAbility::StaticClass());
			Register(UCrossguardTripleThrustAbility::StaticClass());
			Register(UCrossguardPartingTheLandAbility::StaticClass());
			Register(UCrossguardHuntersLeapAbility::StaticClass());
			Register(UCrossguardLikeALionAbility::StaticClass());
			Register(UCrossguardGreaterHealAbility::StaticClass());
			Register(UCrossguardBenedictionAbility::StaticClass());
			Register(UCrossguardUnleashAbility::StaticClass());
			Register(UCrossguardLightAbility::StaticClass());
			Register(UCrossguardWallAbility::StaticClass());
			Register(UCrossguardStealthAbility::StaticClass());
			Register(UCrossguardImmutableAbility::StaticClass());
			Register(UCrossguardSplinterOfWinterAbility::StaticClass());
			Register(UCrossguardMultiscaleAbility::StaticClass());
			Register(UCrossguardHuntersTrackingAbility::StaticClass());
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
