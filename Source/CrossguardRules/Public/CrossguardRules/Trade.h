// Player-to-player trading (docs/trading.md): one player asks, the other accepts, both put items
// (and banked gold, between raids) on the table, both lock their offers, both accept, and the
// server swaps everything at once. Engine-free; the game checks distance and inventory space.

#pragma once

#include "CrossguardRules/RulesApi.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Crossguard
{
	struct FTradeConfig
	{
		// Assumption: how close the two players must stay (cm), to start and throughout.
		double MaxDistanceCm = 500.0;
		// Assumption: how long a trade request waits for an answer.
		double RequestSeconds = 30.0;
		// Assumption: most stacks one side can offer at once.
		int MaxOfferedStacks = 12;
	};

	enum class ETradeError
	{
		None,
		// Trading with yourself.
		SamePlayer,
		// Farther apart than MaxDistanceCm.
		TooFar,
		// Dead or bleeding out.
		NotAlive,
		// Already trading, or already asked.
		Busy,
		// No request from that player, or it ran out.
		NoRequest,
		// Not in a trade.
		NotTrading,
		// An offer cannot change once locked.
		Locked,
		// Accepting needs both offers locked.
		NotLocked,
		// More stacks than MaxOfferedStacks, or the same item twice.
		BadOffer,
		// More gold than banked, or gold during a raid (carried gold is coins: offer them as items).
		NotEnoughGold,
	};

	CROSSGUARDRULES_API ETradeError CanStartTrade(const FTradeConfig& Config, bool bSamePlayer, double DistanceCm, bool bBothAlive,
		bool bEitherBusy);

	// What one side puts on the table: inventory item ids (whole stacks) and banked gold.
	struct FTradeOffer
	{
		std::vector<int> ItemIds;
		int64_t Gold = 0;
	};

	// One trade between side 0 (who asked) and side 1. Changing an offer unlocks both sides, so
	// nobody accepts a deal that changed under them; accepting needs both offers locked; both
	// accepted: Complete, and the game swaps the offers (or cancels if they no longer fit).
	class CROSSGUARDRULES_API FTradeSession
	{
	public:
		// AvailableGold: Side's banked gold; bInRaid: no gold in raids.
		ETradeError SetOffer(const FTradeConfig& Config, int Side, const FTradeOffer& Offer, int64_t AvailableGold, bool bInRaid);
		ETradeError Lock(int Side);
		// Unlocking takes back both acceptances.
		ETradeError Unlock(int Side);
		ETradeError Accept(int Side);

		const FTradeOffer& GetOffer(int Side) const { return Offers[Index(Side)]; }
		bool IsLocked(int Side) const { return Locked[Index(Side)]; }
		bool HasAccepted(int Side) const { return Accepted[Index(Side)]; }
		bool IsComplete() const { return Accepted[0] && Accepted[1]; }

	private:
		static size_t Index(int Side) { return Side == 0 ? 0u : 1u; }
		void ResetAgreement();

		std::array<FTradeOffer, 2> Offers;
		std::array<bool, 2> Locked{false, false};
		std::array<bool, 2> Accepted{false, false};
	};
}
