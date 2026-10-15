#include "CrossguardRules/Portal.h"

namespace Crossguard
{
	EPortalSummonError CanSummonPortal(
		const FPortalConfig& Config, bool bInRaid, double RaidElapsedSeconds, bool bInsidePointOfInterest, bool bAlreadySummoned)
	{
		if (!bInRaid)
		{
			return EPortalSummonError::NotInRaid;
		}
		if (RaidElapsedSeconds < Config.UnlockSeconds)
		{
			return EPortalSummonError::TooEarly;
		}
		if (!bInsidePointOfInterest)
		{
			return EPortalSummonError::OutsidePointOfInterest;
		}
		if (bAlreadySummoned)
		{
			return EPortalSummonError::AlreadySummoned;
		}
		return EPortalSummonError::None;
	}

	EPortalState FExtractPortal::Advance(double DeltaSeconds, bool bKeeperPresent)
	{
		if (DeltaSeconds < 0.0)
		{
			DeltaSeconds = 0.0;
		}
		if (State == EPortalState::Channeling)
		{
			if (!bKeeperPresent)
			{
				State = EPortalState::Collapsed;
				return State;
			}
			Elapsed += DeltaSeconds;
			if (Elapsed < Config.ChannelSeconds)
			{
				return State;
			}
			// Carry the overshoot into the open time.
			Elapsed -= Config.ChannelSeconds;
			State = EPortalState::Open;
			DeltaSeconds = 0.0;
		}
		if (State == EPortalState::Open)
		{
			Elapsed += DeltaSeconds;
			if (Elapsed >= Config.OpenSeconds)
			{
				State = EPortalState::Closed;
			}
		}
		return State;
	}

	double FExtractPortal::GetSecondsLeft() const
	{
		switch (State)
		{
		case EPortalState::Channeling:
			return Config.ChannelSeconds - Elapsed;
		case EPortalState::Open:
			return Config.OpenSeconds - Elapsed;
		case EPortalState::Closed:
		case EPortalState::Collapsed:
			return 0.0;
		}
		return 0.0;
	}
}
