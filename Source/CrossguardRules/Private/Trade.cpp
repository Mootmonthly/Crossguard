#include "CrossguardRules/Trade.h"

#include <algorithm>

namespace Crossguard
{
	ETradeError CanStartTrade(const FTradeConfig& Config, bool bSamePlayer, double DistanceCm, bool bBothAlive, bool bEitherBusy)
	{
		if (bSamePlayer)
		{
			return ETradeError::SamePlayer;
		}
		if (!bBothAlive)
		{
			return ETradeError::NotAlive;
		}
		if (bEitherBusy)
		{
			return ETradeError::Busy;
		}
		if (DistanceCm > Config.MaxDistanceCm)
		{
			return ETradeError::TooFar;
		}
		return ETradeError::None;
	}

	ETradeError FTradeSession::SetOffer(const FTradeConfig& Config, int Side, const FTradeOffer& Offer, int64_t AvailableGold, bool bInRaid)
	{
		if (Locked[Index(Side)])
		{
			return ETradeError::Locked;
		}
		std::vector<int> Sorted = Offer.ItemIds;
		std::sort(Sorted.begin(), Sorted.end());
		if (static_cast<int>(Sorted.size()) > Config.MaxOfferedStacks || std::adjacent_find(Sorted.begin(), Sorted.end()) != Sorted.end())
		{
			return ETradeError::BadOffer;
		}
		if (Offer.Gold < 0 || Offer.Gold > AvailableGold || (bInRaid && Offer.Gold > 0))
		{
			return ETradeError::NotEnoughGold;
		}
		Offers[Index(Side)] = Offer;
		// Anything changed: both look again.
		Locked = {false, false};
		ResetAgreement();
		return ETradeError::None;
	}

	ETradeError FTradeSession::Lock(int Side)
	{
		Locked[Index(Side)] = true;
		return ETradeError::None;
	}

	ETradeError FTradeSession::Unlock(int Side)
	{
		Locked[Index(Side)] = false;
		ResetAgreement();
		return ETradeError::None;
	}

	ETradeError FTradeSession::Accept(int Side)
	{
		if (!Locked[0] || !Locked[1])
		{
			return ETradeError::NotLocked;
		}
		Accepted[Index(Side)] = true;
		return ETradeError::None;
	}

	void FTradeSession::ResetAgreement()
	{
		Accepted = {false, false};
	}
}
