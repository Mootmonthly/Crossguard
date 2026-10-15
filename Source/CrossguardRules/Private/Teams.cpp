#include "CrossguardRules/Teams.h"

namespace Crossguard
{
	FTeamRegistry::FTeamRegistry(const FMatchConfig& InConfig)
		: Config(InConfig)
	{
	}

	ETeamJoinError FTeamRegistry::CanJoin(const std::string& PlayerId, const std::string& PartyKey) const
	{
		if (PlayerId.empty())
		{
			return ETeamJoinError::InvalidPlayer;
		}
		if (PlayerTeams.count(PlayerId) != 0)
		{
			return ETeamJoinError::AlreadyInMatch;
		}
		if (!PartyKey.empty())
		{
			const auto Party = PartyTeams.find(PartyKey);
			if (Party != PartyTeams.end() && TeamSize(Party->second) >= Config.MaxTeamSize)
			{
				return ETeamJoinError::TeamFull;
			}
		}
		return ETeamJoinError::None;
	}

	FTeamJoinResult FTeamRegistry::Join(const std::string& PlayerId, const std::string& PartyKey)
	{
		FTeamJoinResult Result;
		Result.Error = CanJoin(PlayerId, PartyKey);
		if (Result.Error != ETeamJoinError::None)
		{
			return Result;
		}

		if (PartyKey.empty())
		{
			Result.TeamId = NextTeamId++;
		}
		else
		{
			const auto Party = PartyTeams.find(PartyKey);
			if (Party != PartyTeams.end())
			{
				Result.TeamId = Party->second;
			}
			else
			{
				Result.TeamId = NextTeamId++;
				PartyTeams.emplace(PartyKey, Result.TeamId);
			}
		}

		PlayerTeams.emplace(PlayerId, Result.TeamId);
		++TeamSizes[Result.TeamId];
		return Result;
	}

	bool FTeamRegistry::Leave(const std::string& PlayerId)
	{
		const auto Player = PlayerTeams.find(PlayerId);
		if (Player == PlayerTeams.end())
		{
			return false;
		}
		--TeamSizes[Player->second];
		PlayerTeams.erase(Player);
		return true;
	}

	std::optional<int> FTeamRegistry::TeamOf(const std::string& PlayerId) const
	{
		const auto Player = PlayerTeams.find(PlayerId);
		if (Player == PlayerTeams.end())
		{
			return std::nullopt;
		}
		return Player->second;
	}

	int FTeamRegistry::TeamSize(int TeamId) const
	{
		const auto Team = TeamSizes.find(TeamId);
		return Team == TeamSizes.end() ? 0 : Team->second;
	}

	bool FTeamRegistry::AreAllies(const std::string& A, const std::string& B) const
	{
		const std::optional<int> TeamA = TeamOf(A);
		const std::optional<int> TeamB = TeamOf(B);
		return TeamA && TeamB && *TeamA == *TeamB;
	}
}
