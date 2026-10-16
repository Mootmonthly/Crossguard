#include "CrossguardRules/Party.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Crossguard
{
	// Named, not anonymous: unity builds put this file next to Guild.cpp, whose anonymous helpers
	// have similar names.
	namespace PartyRulesDetail
	{
		bool IsKeyChar(char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_';
		}

		FPartyInvite* FindPartyInvite(FParty& Party, const std::string& PlayerId)
		{
			for (FPartyInvite& Invite : Party.Invites)
			{
				if (Invite.PlayerId == PlayerId)
				{
					return &Invite;
				}
			}
			return nullptr;
		}

		void RemovePartyInvite(FParty& Party, const std::string& PlayerId)
		{
			Party.Invites.erase(
				std::remove_if(Party.Invites.begin(), Party.Invites.end(),
					[&PlayerId](const FPartyInvite& Invite) { return Invite.PlayerId == PlayerId; }),
				Party.Invites.end());
		}
	}

	bool IsValidPartyKey(const std::string& Key)
	{
		if (Key.empty() || static_cast<int>(Key.size()) > MaxPartyKeyLength)
		{
			return false;
		}
		return std::all_of(Key.begin(), Key.end(), PartyRulesDetail::IsKeyChar);
	}

	std::string BuildRaidTravelUrl(const std::string& Destination, const std::string& PartyKey, const std::vector<std::string>& ExtraOptions)
	{
		if (Destination.empty() || (!PartyKey.empty() && !IsValidPartyKey(PartyKey)))
		{
			return std::string();
		}
		std::string Url = Destination;
		if (!PartyKey.empty())
		{
			Url += std::string("?") + PartyUrlOption + "=" + PartyKey;
		}
		for (const std::string& Option : ExtraOptions)
		{
			if (!IsValidPartyKey(Option))
			{
				return std::string();
			}
			Url += "?" + Option;
		}
		return Url;
	}

	ERaidTravel PickRaidTravel(const FRaidTravelSituation& Situation)
	{
		if (Situation.bWholeServerInParty && Situation.bHasRaidMap)
		{
			return ERaidTravel::ServerTravel;
		}
		if (!Situation.bHasRaidServer)
		{
			return ERaidTravel::NoDestination;
		}
		return Situation.bListenHostInParty ? ERaidTravel::HostInParty : ERaidTravel::ClientTravel;
	}

	bool FParty::IsMember(const std::string& PlayerId) const
	{
		return std::find(Members.begin(), Members.end(), PlayerId) != Members.end();
	}

	FPartyRegistry::FPartyRegistry(const FPartyConfig& InConfig)
		: Config(InConfig)
	{
	}

	EPartyError FPartyRegistry::Create(const std::string& PartyId, const std::string& FounderId)
	{
		if (FounderId.empty())
		{
			return EPartyError::InvalidPlayer;
		}
		if (!IsValidPartyKey(PartyId))
		{
			return EPartyError::InvalidPartyId;
		}
		if (Parties.count(PartyId) != 0)
		{
			return EPartyError::PartyIdTaken;
		}
		if (PlayerParties.count(FounderId) != 0)
		{
			return EPartyError::AlreadyInParty;
		}
		FParty Party;
		Party.Id = PartyId;
		Party.Members.push_back(FounderId);
		Party.LeaderId = FounderId;
		Parties.emplace(PartyId, std::move(Party));
		PlayerParties.emplace(FounderId, PartyId);
		return EPartyError::None;
	}

	EPartyError FPartyRegistry::CheckInvite(const FParty* Party, const std::string& InviteeId, double Now) const
	{
		if (InviteeId.empty())
		{
			return EPartyError::InvalidPlayer;
		}
		if (PlayerParties.count(InviteeId) != 0)
		{
			return EPartyError::AlreadyInParty;
		}
		if (Party == nullptr)
		{
			// A party about to be founded: one member, no invites.
			return 1 >= Config.MaxMembers ? EPartyError::PartyFull : EPartyError::None;
		}
		if (static_cast<int>(Party->Members.size()) >= Config.MaxMembers)
		{
			return EPartyError::PartyFull;
		}
		for (const FPartyInvite& Pending : Party->Invites)
		{
			if (Pending.PlayerId == InviteeId && Now < Pending.ExpiresAt)
			{
				return EPartyError::AlreadyInvited;
			}
		}
		return EPartyError::None;
	}

	EPartyError FPartyRegistry::Invite(const std::string& ActorId, const std::string& InviteeId, double Now, const std::string& NewPartyId)
	{
		if (ActorId.empty())
		{
			return EPartyError::InvalidPlayer;
		}
		if (ActorId == InviteeId)
		{
			return EPartyError::CannotTargetSelf;
		}

		FParty* Party = nullptr;
		if (ActorParty(ActorId, Party) != EPartyError::None)
		{
			// Founding a party for this invite: every check first, so a refusal founds nothing.
			if (NewPartyId.empty())
			{
				return EPartyError::NotInParty;
			}
			if (!IsValidPartyKey(NewPartyId))
			{
				return EPartyError::InvalidPartyId;
			}
			if (Parties.count(NewPartyId) != 0)
			{
				return EPartyError::PartyIdTaken;
			}
			if (const EPartyError Error = CheckInvite(nullptr, InviteeId, Now); Error != EPartyError::None)
			{
				return Error;
			}
			Create(NewPartyId, ActorId);
			Party = &Parties.at(NewPartyId);
		}
		else
		{
			if (Party->LeaderId != ActorId)
			{
				return EPartyError::NotLeader;
			}
			if (const EPartyError Error = CheckInvite(Party, InviteeId, Now); Error != EPartyError::None)
			{
				return Error;
			}
		}

		const double ExpiresAt = Now + Config.InviteExpirySeconds;
		if (FPartyInvite* Existing = PartyRulesDetail::FindPartyInvite(*Party, InviteeId))
		{
			// Expired (CheckInvite refused a live one): a fresh invite in its place.
			*Existing = {InviteeId, ActorId, ExpiresAt};
			return EPartyError::None;
		}
		Party->Invites.push_back({InviteeId, ActorId, ExpiresAt});
		return EPartyError::None;
	}

	EPartyError FPartyRegistry::Accept(const std::string& PartyId, const std::string& PlayerId, double Now)
	{
		if (PlayerId.empty())
		{
			return EPartyError::InvalidPlayer;
		}
		const auto Found = Parties.find(PartyId);
		if (Found == Parties.end())
		{
			return EPartyError::UnknownParty;
		}
		FParty& Party = Found->second;
		const FPartyInvite* Pending = PartyRulesDetail::FindPartyInvite(Party, PlayerId);
		if (Pending == nullptr)
		{
			return EPartyError::NoInvite;
		}
		if (Now >= Pending->ExpiresAt)
		{
			return EPartyError::InviteExpired;
		}
		if (PlayerParties.count(PlayerId) != 0)
		{
			return EPartyError::AlreadyInParty;
		}
		if (static_cast<int>(Party.Members.size()) >= Config.MaxMembers)
		{
			return EPartyError::PartyFull;
		}
		// One party per player: every other party's invite to them is moot now.
		for (auto& Entry : Parties)
		{
			PartyRulesDetail::RemovePartyInvite(Entry.second, PlayerId);
		}
		Party.Members.push_back(PlayerId);
		PlayerParties.emplace(PlayerId, PartyId);
		return EPartyError::None;
	}

	EPartyError FPartyRegistry::Join(const std::string& PartyId, const std::string& PlayerId)
	{
		if (PlayerId.empty())
		{
			return EPartyError::InvalidPlayer;
		}
		const auto Found = Parties.find(PartyId);
		if (Found == Parties.end())
		{
			return EPartyError::UnknownParty;
		}
		if (PlayerParties.count(PlayerId) != 0)
		{
			return EPartyError::AlreadyInParty;
		}
		FParty& Party = Found->second;
		if (static_cast<int>(Party.Members.size()) >= Config.MaxMembers)
		{
			return EPartyError::PartyFull;
		}
		for (auto& Entry : Parties)
		{
			PartyRulesDetail::RemovePartyInvite(Entry.second, PlayerId);
		}
		Party.Members.push_back(PlayerId);
		PlayerParties.emplace(PlayerId, PartyId);
		return EPartyError::None;
	}

	EPartyError FPartyRegistry::Decline(const std::string& PartyId, const std::string& PlayerId)
	{
		const auto Found = Parties.find(PartyId);
		if (Found == Parties.end())
		{
			return EPartyError::UnknownParty;
		}
		if (PartyRulesDetail::FindPartyInvite(Found->second, PlayerId) == nullptr)
		{
			return EPartyError::NoInvite;
		}
		PartyRulesDetail::RemovePartyInvite(Found->second, PlayerId);
		return EPartyError::None;
	}

	int FPartyRegistry::DeclineAll(const std::string& PlayerId)
	{
		int Dropped = 0;
		for (auto& Entry : Parties)
		{
			const size_t Before = Entry.second.Invites.size();
			PartyRulesDetail::RemovePartyInvite(Entry.second, PlayerId);
			Dropped += static_cast<int>(Before - Entry.second.Invites.size());
		}
		return Dropped;
	}

	int FPartyRegistry::PruneExpiredInvites(double Now)
	{
		int Dropped = 0;
		for (auto& Entry : Parties)
		{
			std::vector<FPartyInvite>& Invites = Entry.second.Invites;
			const size_t Before = Invites.size();
			Invites.erase(
				std::remove_if(Invites.begin(), Invites.end(), [Now](const FPartyInvite& Invite) { return Now >= Invite.ExpiresAt; }),
				Invites.end());
			Dropped += static_cast<int>(Before - Invites.size());
		}
		return Dropped;
	}

	EPartyError FPartyRegistry::Kick(const std::string& ActorId, const std::string& TargetId)
	{
		FParty* Party = nullptr;
		if (const EPartyError Error = CheckLeaderTarget(ActorId, TargetId, Party); Error != EPartyError::None)
		{
			return Error;
		}
		// The leader stays, so the party neither disbands nor changes leader.
		return Leave(TargetId).Error;
	}

	EPartyError FPartyRegistry::Promote(const std::string& ActorId, const std::string& TargetId)
	{
		FParty* Party = nullptr;
		if (const EPartyError Error = CheckLeaderTarget(ActorId, TargetId, Party); Error != EPartyError::None)
		{
			return Error;
		}
		Party->LeaderId = TargetId;
		return EPartyError::None;
	}

	FPartyLeaveResult FPartyRegistry::Leave(const std::string& PlayerId)
	{
		FPartyLeaveResult Result;
		FParty* Party = nullptr;
		Result.Error = ActorParty(PlayerId, Party);
		if (Result.Error != EPartyError::None)
		{
			return Result;
		}
		Result.PartyId = Party->Id;
		PlayerParties.erase(PlayerId);
		Party->Members.erase(std::remove(Party->Members.begin(), Party->Members.end(), PlayerId), Party->Members.end());
		if (Party->Members.empty())
		{
			Result.bDisbanded = true;
			Parties.erase(Result.PartyId);
			return Result;
		}
		if (Party->LeaderId == PlayerId)
		{
			// Members are in join order: the longest-standing one leads now.
			Party->LeaderId = Party->Members.front();
			Result.NewLeaderId = Party->LeaderId;
		}
		return Result;
	}

	EPartyError FPartyRegistry::CanStartRaid(const std::string& ActorId) const
	{
		if (ActorId.empty())
		{
			return EPartyError::InvalidPlayer;
		}
		const FParty* Party = PartyOf(ActorId);
		return Party == nullptr || Party->LeaderId == ActorId ? EPartyError::None : EPartyError::NotLeader;
	}

	const FParty* FPartyRegistry::Find(const std::string& PartyId) const
	{
		const auto Found = Parties.find(PartyId);
		return Found == Parties.end() ? nullptr : &Found->second;
	}

	const FParty* FPartyRegistry::PartyOf(const std::string& PlayerId) const
	{
		const auto Found = PlayerParties.find(PlayerId);
		return Found == PlayerParties.end() ? nullptr : Find(Found->second);
	}

	bool FPartyRegistry::IsLeader(const std::string& PlayerId) const
	{
		const FParty* Party = PartyOf(PlayerId);
		return Party != nullptr && Party->LeaderId == PlayerId;
	}

	std::vector<const FParty*> FPartyRegistry::InvitesFor(const std::string& PlayerId, double Now) const
	{
		std::vector<const FParty*> Found;
		for (const auto& Entry : Parties)
		{
			for (const FPartyInvite& Pending : Entry.second.Invites)
			{
				if (Pending.PlayerId == PlayerId && Now < Pending.ExpiresAt)
				{
					Found.push_back(&Entry.second);
					break;
				}
			}
		}
		return Found;
	}

	EPartyError FPartyRegistry::ActorParty(const std::string& ActorId, FParty*& OutParty)
	{
		const auto Found = PlayerParties.find(ActorId);
		if (Found == PlayerParties.end())
		{
			return EPartyError::NotInParty;
		}
		const auto Party = Parties.find(Found->second);
		if (Party == Parties.end())
		{
			return EPartyError::NotInParty;
		}
		OutParty = &Party->second;
		return EPartyError::None;
	}

	EPartyError FPartyRegistry::CheckLeaderTarget(const std::string& ActorId, const std::string& TargetId, FParty*& OutParty)
	{
		if (const EPartyError Error = ActorParty(ActorId, OutParty); Error != EPartyError::None)
		{
			return Error;
		}
		if (OutParty->LeaderId != ActorId)
		{
			return EPartyError::NotLeader;
		}
		if (TargetId == ActorId)
		{
			return EPartyError::CannotTargetSelf;
		}
		return OutParty->IsMember(TargetId) ? EPartyError::None : EPartyError::TargetNotInParty;
	}
}
