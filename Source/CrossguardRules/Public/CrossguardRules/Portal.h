// Extract portals: summoned anywhere inside a point of interest once the raid has run long enough,
// channel while the summoner's team stays near, then open for anyone to step through.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/RulesApi.h"

namespace Crossguard
{
	enum class EPortalSummonError
	{
		None,
		NotInRaid,
		// Before UnlockSeconds into the raid.
		TooEarly,
		// Only inside a point of interest.
		OutsidePointOfInterest,
		// One portal per player per raid.
		AlreadySummoned,
	};

	CROSSGUARDRULES_API EPortalSummonError CanSummonPortal(
		const FPortalConfig& Config, bool bInRaid, double RaidElapsedSeconds, bool bInsidePointOfInterest, bool bAlreadySummoned);

	enum class EPortalState
	{
		Channeling,
		Open,
		// Opened and ran out.
		Closed,
		// The channel lost every keeper.
		Collapsed,
	};

	// One portal's timeline. Advance it with the elapsed time and whether any keeper (a living
	// member of the summoner's team within KeepAliveRadius) is present.
	class CROSSGUARDRULES_API FExtractPortal
	{
	public:
		explicit FExtractPortal(const FPortalConfig& InConfig) : Config(InConfig) {}

		// Returns the state after the step. A channel that ends inside this step opens, and the
		// leftover time counts toward the open time.
		EPortalState Advance(double DeltaSeconds, bool bKeeperPresent);

		EPortalState GetState() const { return State; }
		// Seconds left in the current state (0 once closed or collapsed).
		double GetSecondsLeft() const;
		bool CanEnter() const { return State == EPortalState::Open; }

	private:
		FPortalConfig Config;
		EPortalState State = EPortalState::Channeling;
		double Elapsed = 0.0;
	};
}
