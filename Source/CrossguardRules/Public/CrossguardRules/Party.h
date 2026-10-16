// Parties: in-game groups that play as one team and enter a raid together (docs/parties.md).
//
// Ruled: up to 8 players (the match's team cap). Everything else here is a placeholder or an
// assumption (listed in the doc, numbers in FPartyConfig). Player and party ids are opaque strings
// chosen by the caller; the party id is also the ?Party=<key> travel option, so it must be
// URL-safe (IsValidPartyKey). Time is passed in as Now; the library reads no clocks.
//
// Every registry operation returns EPartyError::None on success and changes nothing on error.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/RulesApi.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Crossguard
{
	enum class EPartyError
	{
		None,
		// Empty player id.
		InvalidPlayer,
		// Empty or not URL-safe (see IsValidPartyKey).
		InvalidPartyId,
		PartyIdTaken,
		UnknownParty,
		// The acting player is in no party.
		NotInParty,
		// The player (invitee, joiner) is already in a party: one party per player.
		AlreadyInParty,
		// The target player is not a member of the actor's party.
		TargetNotInParty,
		// Only the leader may do this.
		NotLeader,
		// Invite, kick or promote aimed at the actor.
		CannotTargetSelf,
		PartyFull,
		// An unexpired invite from this party to that player is already pending.
		AlreadyInvited,
		NoInvite,
		InviteExpired,
	};

	// The travel option the game mode reads at login (ACrossguardGameMode::PartyOption).
	constexpr const char* PartyUrlOption = "Party";
	// Longest party key accepted (a GUID in hex is 32).
	constexpr int MaxPartyKeyLength = 64;

	// 1..MaxPartyKeyLength ASCII letters, digits, '-' or '_': safe inside a travel URL.
	CROSSGUARDRULES_API bool IsValidPartyKey(const std::string& Key);

	// Destination (a map path such as "/Game/Maps/Raid", or a server address such as
	// "203.0.113.5:7777") plus "?Party=<PartyKey>". An empty key means a solo player: Destination
	// alone. Extra options (e.g. "listen") are appended as "?<option>". Empty result if Destination
	// is empty, or the key or an option is not URL-safe (same characters as a key).
	CROSSGUARDRULES_API std::string BuildRaidTravelUrl(
		const std::string& Destination, const std::string& PartyKey, const std::vector<std::string>& ExtraOptions = {});

	// How a hub sends a party to a raid (docs/parties.md, "Raid launch").
	enum class ERaidTravel
	{
		// Everyone on this server goes: the server itself loads the raid map (ServerTravel).
		ServerTravel,
		// Only the party goes: each member's client travels to a separate raid server.
		ClientTravel,
		// Neither is possible: no raid map for a whole-server party, and no raid server address.
		NoDestination,
		// A listen server's host is in the party but the hub cannot travel as a whole: the host
		// leaving would close the hub for everyone else.
		HostInParty,
	};

	struct FRaidTravelSituation
	{
		// Every player on this server is in the launching party (or the launcher is alone).
		bool bWholeServerInParty = false;
		bool bHasRaidMap = false;
		bool bHasRaidServer = false;
		// A listen server whose own (host) player is in the party.
		bool bListenHostInParty = false;
	};

	// ServerTravel when the whole server is the party and a raid map is set; else ClientTravel to the
	// raid server when one is set (refused if the listen host is in the party); else NoDestination.
	CROSSGUARDRULES_API ERaidTravel PickRaidTravel(const FRaidTravelSituation& Situation);

	struct FPartyInvite
	{
		std::string PlayerId;
		std::string InvitedBy;
		// Expired once Now >= ExpiresAt.
		double ExpiresAt = 0.0;
	};

	struct FParty
	{
		std::string Id;
		// In join order; the founder first.
		std::vector<std::string> Members;
		// Always one of Members.
		std::string LeaderId;
		std::vector<FPartyInvite> Invites;

		bool IsMember(const std::string& PlayerId) const;
	};

	struct FPartyLeaveResult
	{
		EPartyError Error = EPartyError::None;
		// The party the player left (also when it disbanded).
		std::string PartyId;
		// The last member left: the party is gone.
		bool bDisbanded = false;
		// The leader left and others remain: the new leader (the longest-standing member).
		std::string NewLeaderId;

		bool IsValid() const { return Error == EPartyError::None; }
	};

	// Every party, with a player -> party index.
	class CROSSGUARDRULES_API FPartyRegistry
	{
	public:
		explicit FPartyRegistry(const FPartyConfig& InConfig = FPartyConfig());

		const FPartyConfig& GetConfig() const { return Config; }

		// A party of one, its founder the leader.
		EPartyError Create(const std::string& PartyId, const std::string& FounderId);

		// The leader invites a player in no party. An actor in no party founds one first, with
		// NewPartyId (empty: NotInParty), but only if the invite would succeed. Replaces an expired
		// invite to the same player. Pending invites do not reserve a place.
		EPartyError Invite(const std::string& ActorId, const std::string& InviteeId, double Now, const std::string& NewPartyId = std::string());
		// Joins as a member. The player's invites from other parties are dropped.
		EPartyError Accept(const std::string& PartyId, const std::string& PlayerId, double Now);
		// Removes the invite, expired or not.
		EPartyError Decline(const std::string& PartyId, const std::string& PlayerId);
		// Drops expired invites from every party. Returns how many were dropped.
		int PruneExpiredInvites(double Now);

		// Leader only. The target leaves as if by Leave (the party keeps at least the leader).
		EPartyError Kick(const std::string& ActorId, const std::string& TargetId);
		// Leader only: the target becomes leader.
		EPartyError Promote(const std::string& ActorId, const std::string& TargetId);
		// Anyone. A leader leaving hands the lead to the longest-standing member; the last member
		// leaving disbands the party (its invites go with it).
		FPartyLeaveResult Leave(const std::string& PlayerId);

		// None if the actor may send the party to a raid: its leader, or a player in no party (who
		// goes alone).
		EPartyError CanStartRaid(const std::string& ActorId) const;

		const FParty* Find(const std::string& PartyId) const;
		const FParty* PartyOf(const std::string& PlayerId) const;
		bool IsLeader(const std::string& PlayerId) const;
		// Parties with an unexpired invite for the player, in no particular order.
		std::vector<const FParty*> InvitesFor(const std::string& PlayerId, double Now) const;
		const std::unordered_map<std::string, FParty>& All() const { return Parties; }

	private:
		// The actor's party; NotInParty if none.
		EPartyError ActorParty(const std::string& ActorId, FParty*& OutParty);
		// The actor's party, the actor its leader, the target another member.
		EPartyError CheckLeaderTarget(const std::string& ActorId, const std::string& TargetId, FParty*& OutParty);
		// Checks Invite's conditions against an existing party (or a would-be party of the actor).
		EPartyError CheckInvite(const FParty* Party, const std::string& InviteeId, double Now) const;

		FPartyConfig Config;
		std::unordered_map<std::string, FParty> Parties;
		// Player id -> party id.
		std::unordered_map<std::string, std::string> PlayerParties;
	};
}
