// Deterministic random numbers for rolls (affixes, loot). Internal to CrossguardRules.

#pragma once

#include <cstddef>
#include <cstdint>

namespace Crossguard::Detail
{
	// SplitMix64: small, fast, identical output on every platform.
	struct FSplitMix64
	{
		uint64_t State;

		uint64_t Next()
		{
			uint64_t Z = (State += 0x9E3779B97F4A7C15ull);
			Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
			Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
			return Z ^ (Z >> 31);
		}

		// Uniform in [0, 1).
		double NextUnit()
		{
			return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0);
		}

		// Uniform in [0, Bound). Bound > 0.
		size_t NextIndex(size_t Bound)
		{
			return static_cast<size_t>(NextUnit() * static_cast<double>(Bound));
		}
	};
}
