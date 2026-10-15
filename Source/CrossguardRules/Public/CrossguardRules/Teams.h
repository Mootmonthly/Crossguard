// Teams in a match (docs/design-decisions.md, "Teams").
//
// Players who enter with the same party key form one team, up to MaxTeamSize. A player without a
// party key gets a team of their own. Teams decide who counts as an ally (party skills such as
// Multiscale); they do NOT stop damage: friendly fire is always on.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/RulesApi.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace Crossguard
{
	enum class ETeamJoinError
	{
		None,
		// The party's team already has MaxTeamSize players.
		TeamFull,
		AlreadyInMatch,
		InvalidPlayer,
	};

	struct FTeamJoinResult
	{
		ETeamJoinError Error = ETeamJoinError::None;
		// Valid when Error is None.
		int TeamId = -1;

		bool IsValid() const { return Error == ETeamJoinError::None; }
	};

	class CROSSGUARDRULES_API FTeamRegistry
	{
	public:
		explicit FTeamRegistry(const FMatchConfig& InConfig = FMatchConfig());

		// Would Join succeed? Does not change anything.
		ETeamJoinError CanJoin(const std::string& PlayerId, const std::string& PartyKey) const;

		// Empty PartyKey: a new team of one. A party keeps its team for the whole match, so a
		// player who leaves and rejoins with the same key returns to the same team.
		FTeamJoinResult Join(const std::string& PlayerId, const std::string& PartyKey);

		// Returns false if the player was not in the match.
		bool Leave(const std::string& PlayerId);

		std::optional<int> TeamOf(const std::string& PlayerId) const;
		int TeamSize(int TeamId) const;
		bool AreAllies(const std::string& A, const std::string& B) const;

	private:
		FMatchConfig Config;
		int NextTeamId = 0;
		std::unordered_map<std::string, int> PlayerTeams;
		std::unordered_map<std::string, int> PartyTeams;
		std::unordered_map<int, int> TeamSizes;
	};
}
