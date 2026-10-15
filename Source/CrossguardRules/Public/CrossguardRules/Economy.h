// Gold, vendor prices and lockpicking.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/RulesApi.h"

namespace Crossguard
{
	// Gold a vendor pays for one item: SellValue + SellPerRarity × rarity level. 0 if the item's
	// SellValue is 0 (vendors do not buy it).
	CROSSGUARDRULES_API int SellPrice(int SellValue, int SellPerRarity, ERarity Rarity);

	// Seconds to pick a lock: LockpickSeconds / max(1 + Dexterity, MinDexterityFactor).
	// Dexterity is a fraction (0.2 = 20%). Placeholder for how Dexterity speeds it up.
	CROSSGUARDRULES_API double LockpickSeconds(const FEconomyConfig& Config, double Dexterity);
}
