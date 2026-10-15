#include "CrossguardRules/Guild.h"

#include <cstddef>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Crossguard
{
	namespace
	{
		bool IsNameChar(char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == ' '
				|| C == '-';
		}

		// Key for the case-insensitive name index. Names are ASCII once validated.
		std::string NameKey(const std::string& Name)
		{
			std::string Key = TrimGuildName(Name);
			for (char& C : Key)
			{
				if (C >= 'A' && C <= 'Z')
				{
					C = static_cast<char>(C - 'A' + 'a');
				}
			}
			return Key;
		}

		bool IsValidItem(const FGuildBankItem& Item)
		{
			return !Item.ItemId.empty() && Item.Count > 0 && Item.Size.Width > 0 && Item.Size.Height > 0;
		}

		FGuildMember* FindMutableMember(FGuild& Guild, const std::string& PlayerId)
		{
			for (FGuildMember& Member : Guild.Members)
			{
				if (Member.PlayerId == PlayerId)
				{
					return &Member;
				}
			}
			return nullptr;
		}

		FGuildInvite* FindInvite(FGuild& Guild, const std::string& PlayerId)
		{
			for (FGuildInvite& Invite : Guild.Invites)
			{
				if (Invite.PlayerId == PlayerId)
				{
					return &Invite;
				}
			}
			return nullptr;
		}

		void RemoveInvite(FGuild& Guild, const std::string& PlayerId)
		{
			for (size_t I = 0; I < Guild.Invites.size(); ++I)
			{
				if (Guild.Invites[I].PlayerId == PlayerId)
				{
					Guild.Invites.erase(Guild.Invites.begin() + static_cast<std::ptrdiff_t>(I));
					return;
				}
			}
		}
	}

	bool GuildRankCan(EGuildRank Rank, EGuildPermission Permission)
	{
		switch (Permission)
		{
		case EGuildPermission::Deposit:
			return true;
		case EGuildPermission::ManageRanks:
			return Rank == EGuildRank::Leader;
		case EGuildPermission::Invite:
		case EGuildPermission::Withdraw:
		case EGuildPermission::Redistribute:
		case EGuildPermission::ManageBank:
			return Rank == EGuildRank::Officer || Rank == EGuildRank::Leader;
		}
		return false;
	}

	bool CanKick(EGuildRank Actor, EGuildRank Target)
	{
		if (Actor == EGuildRank::Leader)
		{
			return Target != EGuildRank::Leader;
		}
		return Actor == EGuildRank::Officer && Target == EGuildRank::Member;
	}

	std::string TrimGuildName(const std::string& Name)
	{
		const size_t First = Name.find_first_not_of(' ');
		if (First == std::string::npos)
		{
			return std::string();
		}
		const size_t Last = Name.find_last_not_of(' ');
		return Name.substr(First, Last - First + 1);
	}

	EGuildError ValidateGuildName(const FGuildConfig& Config, const std::string& Name)
	{
		const std::string Trimmed = TrimGuildName(Name);
		const int Length = static_cast<int>(Trimmed.size());
		if (Length < Config.MinNameLength || Length > Config.MaxNameLength || Trimmed.empty())
		{
			return EGuildError::InvalidName;
		}
		for (const char C : Trimmed)
		{
			if (!IsNameChar(C))
			{
				return EGuildError::InvalidName;
			}
		}
		return EGuildError::None;
	}

	const FGuildBankItem* FGuildBank::Find(int SlotId) const
	{
		for (const FGuildBankItem& Item : Items)
		{
			if (Item.SlotId == SlotId)
			{
				return &Item;
			}
		}
		return nullptr;
	}

	const FGuildMember* FGuild::FindMember(const std::string& PlayerId) const
	{
		for (const FGuildMember& Member : Members)
		{
			if (Member.PlayerId == PlayerId)
			{
				return &Member;
			}
		}
		return nullptr;
	}

	const std::string& FGuild::LeaderId() const
	{
		static const std::string None;
		for (const FGuildMember& Member : Members)
		{
			if (Member.Rank == EGuildRank::Leader)
			{
				return Member.PlayerId;
			}
		}
		return None;
	}

	FGuildRegistry::FGuildRegistry(const FGuildConfig& InConfig)
		: Config(InConfig)
	{
	}

	EGuildError FGuildRegistry::Create(const std::string& GuildId, const std::string& Name, const std::string& FounderId)
	{
		if (FounderId.empty())
		{
			return EGuildError::InvalidPlayer;
		}
		if (GuildId.empty())
		{
			return EGuildError::InvalidGuildId;
		}
		if (Guilds.count(GuildId) != 0)
		{
			return EGuildError::GuildIdTaken;
		}
		if (ValidateGuildName(Config, Name) != EGuildError::None)
		{
			return EGuildError::InvalidName;
		}
		if (NameIndex.count(NameKey(Name)) != 0)
		{
			return EGuildError::NameTaken;
		}
		if (PlayerGuilds.count(FounderId) != 0)
		{
			return EGuildError::AlreadyInGuild;
		}

		FGuild Guild;
		Guild.Id = GuildId;
		Guild.Name = TrimGuildName(Name);
		Guild.Bank.Grid = FInventoryGrid(FGridSize{Config.BankWidth, Config.BankHeight});
		FGuild& Added = Guilds.emplace(GuildId, std::move(Guild)).first->second;
		NameIndex.emplace(NameKey(Name), GuildId);
		AddMember(Added, FounderId, EGuildRank::Leader);
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::Restore(const FGuild& Guild)
	{
		if (Guild.Id.empty())
		{
			return EGuildError::InvalidGuildId;
		}
		if (Guilds.count(Guild.Id) != 0)
		{
			return EGuildError::GuildIdTaken;
		}
		if (ValidateGuildName(Config, Guild.Name) != EGuildError::None)
		{
			return EGuildError::InvalidName;
		}
		if (Guild.Name != TrimGuildName(Guild.Name))
		{
			return EGuildError::InvalidData;
		}
		if (NameIndex.count(NameKey(Guild.Name)) != 0)
		{
			return EGuildError::NameTaken;
		}

		int Leaders = 0;
		std::unordered_set<std::string> Seen;
		for (const FGuildMember& Member : Guild.Members)
		{
			if (Member.PlayerId.empty() || !Seen.insert(Member.PlayerId).second)
			{
				return EGuildError::InvalidData;
			}
			Leaders += Member.Rank == EGuildRank::Leader ? 1 : 0;
		}
		if (Leaders != 1)
		{
			return EGuildError::InvalidData;
		}
		for (const FGuildMember& Member : Guild.Members)
		{
			if (PlayerGuilds.count(Member.PlayerId) != 0)
			{
				return EGuildError::AlreadyInGuild;
			}
		}

		const FGuildBank& Bank = Guild.Bank;
		const FGridSize GridSize = Bank.Grid.Size();
		if (Bank.Gold < 0 || GridSize.Width <= 0 || GridSize.Height <= 0
			|| Bank.Items.size() != Bank.Grid.Items().size())
		{
			return EGuildError::InvalidData;
		}
		std::unordered_set<int> Slots;
		for (const FGuildBankItem& Item : Bank.Items)
		{
			const FGridItem* Placed = Bank.Grid.Find(Item.SlotId);
			if (!IsValidItem(Item) || Item.SlotId <= 0 || Item.SlotId >= Bank.NextSlotId
				|| !Slots.insert(Item.SlotId).second || Placed == nullptr
				|| Placed->Size.Width != Item.Size.Width || Placed->Size.Height != Item.Size.Height)
			{
				return EGuildError::InvalidData;
			}
		}

		Guilds.emplace(Guild.Id, Guild);
		NameIndex.emplace(NameKey(Guild.Name), Guild.Id);
		for (const FGuildMember& Member : Guild.Members)
		{
			PlayerGuilds.emplace(Member.PlayerId, Guild.Id);
		}
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::Invite(const std::string& ActorId, const std::string& InviteeId, double Now)
	{
		FGuild* Guild = nullptr;
		FGuildMember* Actor = nullptr;
		if (const EGuildError Error = ActorGuild(ActorId, Guild, Actor); Error != EGuildError::None)
		{
			return Error;
		}
		if (!GuildRankCan(Actor->Rank, EGuildPermission::Invite))
		{
			return EGuildError::NotPermitted;
		}
		if (InviteeId.empty())
		{
			return EGuildError::InvalidPlayer;
		}
		if (PlayerGuilds.count(InviteeId) != 0)
		{
			return EGuildError::AlreadyInGuild;
		}
		if (static_cast<int>(Guild->Members.size()) >= Config.MaxMembers)
		{
			return EGuildError::GuildFull;
		}

		const double ExpiresAt = Now + Config.InviteExpirySeconds;
		if (FGuildInvite* Existing = FindInvite(*Guild, InviteeId))
		{
			if (Now < Existing->ExpiresAt)
			{
				return EGuildError::AlreadyInvited;
			}
			*Existing = {InviteeId, ActorId, ExpiresAt};
			return EGuildError::None;
		}
		Guild->Invites.push_back({InviteeId, ActorId, ExpiresAt});
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::Accept(const std::string& GuildId, const std::string& PlayerId, double Now)
	{
		if (PlayerId.empty())
		{
			return EGuildError::InvalidPlayer;
		}
		const auto Found = Guilds.find(GuildId);
		if (Found == Guilds.end())
		{
			return EGuildError::UnknownGuild;
		}
		FGuild& Guild = Found->second;
		const FGuildInvite* Invite = FindInvite(Guild, PlayerId);
		if (Invite == nullptr)
		{
			return EGuildError::NoInvite;
		}
		if (Now >= Invite->ExpiresAt)
		{
			return EGuildError::InviteExpired;
		}
		if (PlayerGuilds.count(PlayerId) != 0)
		{
			return EGuildError::AlreadyInGuild;
		}
		if (static_cast<int>(Guild.Members.size()) >= Config.MaxMembers)
		{
			return EGuildError::GuildFull;
		}
		RemoveInvite(Guild, PlayerId);
		AddMember(Guild, PlayerId, EGuildRank::Member);
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::Decline(const std::string& GuildId, const std::string& PlayerId)
	{
		const auto Found = Guilds.find(GuildId);
		if (Found == Guilds.end())
		{
			return EGuildError::UnknownGuild;
		}
		if (FindInvite(Found->second, PlayerId) == nullptr)
		{
			return EGuildError::NoInvite;
		}
		RemoveInvite(Found->second, PlayerId);
		return EGuildError::None;
	}

	int FGuildRegistry::PruneExpiredInvites(double Now)
	{
		int Dropped = 0;
		for (auto& Entry : Guilds)
		{
			std::vector<FGuildInvite>& Invites = Entry.second.Invites;
			for (size_t I = Invites.size(); I-- > 0;)
			{
				if (Now >= Invites[I].ExpiresAt)
				{
					Invites.erase(Invites.begin() + static_cast<std::ptrdiff_t>(I));
					++Dropped;
				}
			}
		}
		return Dropped;
	}

	EGuildError FGuildRegistry::Kick(const std::string& ActorId, const std::string& TargetId)
	{
		FGuild* Guild = nullptr;
		FGuildMember* Actor = nullptr;
		FGuildMember* Target = nullptr;
		if (const EGuildError Error = CheckMemberTarget(ActorId, TargetId, Guild, Actor, Target); Error != EGuildError::None)
		{
			return Error;
		}
		if (!CanKick(Actor->Rank, Target->Rank))
		{
			return EGuildError::NotPermitted;
		}
		RemoveMember(*Guild, TargetId);
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::Promote(const std::string& ActorId, const std::string& TargetId)
	{
		FGuild* Guild = nullptr;
		FGuildMember* Actor = nullptr;
		FGuildMember* Target = nullptr;
		if (const EGuildError Error = CheckMemberTarget(ActorId, TargetId, Guild, Actor, Target); Error != EGuildError::None)
		{
			return Error;
		}
		if (!GuildRankCan(Actor->Rank, EGuildPermission::ManageRanks))
		{
			return EGuildError::NotPermitted;
		}
		if (Target->Rank != EGuildRank::Member)
		{
			return EGuildError::AlreadyRank;
		}
		Target->Rank = EGuildRank::Officer;
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::Demote(const std::string& ActorId, const std::string& TargetId)
	{
		FGuild* Guild = nullptr;
		FGuildMember* Actor = nullptr;
		FGuildMember* Target = nullptr;
		if (const EGuildError Error = CheckMemberTarget(ActorId, TargetId, Guild, Actor, Target); Error != EGuildError::None)
		{
			return Error;
		}
		if (!GuildRankCan(Actor->Rank, EGuildPermission::ManageRanks))
		{
			return EGuildError::NotPermitted;
		}
		if (Target->Rank != EGuildRank::Officer)
		{
			return EGuildError::AlreadyRank;
		}
		Target->Rank = EGuildRank::Member;
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::TransferLeadership(const std::string& ActorId, const std::string& TargetId)
	{
		FGuild* Guild = nullptr;
		FGuildMember* Actor = nullptr;
		FGuildMember* Target = nullptr;
		if (const EGuildError Error = CheckMemberTarget(ActorId, TargetId, Guild, Actor, Target); Error != EGuildError::None)
		{
			return Error;
		}
		if (Actor->Rank != EGuildRank::Leader)
		{
			return EGuildError::NotPermitted;
		}
		Target->Rank = EGuildRank::Leader;
		Actor->Rank = EGuildRank::Officer;
		return EGuildError::None;
	}

	FGuildLeaveResult FGuildRegistry::Leave(const std::string& PlayerId)
	{
		FGuildLeaveResult Result;
		FGuild* Guild = nullptr;
		FGuildMember* Member = nullptr;
		Result.Error = ActorGuild(PlayerId, Guild, Member);
		if (Result.Error != EGuildError::None)
		{
			return Result;
		}
		if (Guild->Members.size() > 1)
		{
			if (Member->Rank == EGuildRank::Leader)
			{
				Result.Error = EGuildError::LeaderMustTransfer;
				return Result;
			}
			RemoveMember(*Guild, PlayerId);
			return Result;
		}

		// Last member: disband and hand the bank to them.
		Result.bDisbanded = true;
		Result.Gold = Guild->Bank.Gold;
		Result.Items = std::move(Guild->Bank.Items);
		const std::string GuildId = Guild->Id;
		PlayerGuilds.erase(PlayerId);
		NameIndex.erase(NameKey(Guild->Name));
		Guilds.erase(GuildId);
		return Result;
	}

	EGuildError FGuildRegistry::DepositGold(const std::string& ActorId, std::int64_t Amount, double Now)
	{
		FGuild* Guild = nullptr;
		FGuildMember* Actor = nullptr;
		if (const EGuildError Error = ActorGuild(ActorId, Guild, Actor); Error != EGuildError::None)
		{
			return Error;
		}
		if (!GuildRankCan(Actor->Rank, EGuildPermission::Deposit))
		{
			return EGuildError::NotPermitted;
		}
		if (Amount <= 0)
		{
			return EGuildError::InvalidAmount;
		}
		if (Amount > std::numeric_limits<std::int64_t>::max() - Guild->Bank.Gold)
		{
			return EGuildError::GoldOverflow;
		}
		Guild->Bank.Gold += Amount;
		FGuildBankLogEntry Entry;
		Entry.PlayerId = ActorId;
		Entry.Action = EGuildBankAction::DepositGold;
		Entry.Gold = Amount;
		Entry.Time = Now;
		Log(*Guild, std::move(Entry));
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::WithdrawGold(const std::string& ActorId, std::int64_t Amount, double Now)
	{
		return RedistributeGold(ActorId, ActorId, Amount, Now);
	}

	EGuildError FGuildRegistry::RedistributeGold(
		const std::string& ActorId, const std::string& RecipientId, std::int64_t Amount, double Now)
	{
		// Withdrawing is redistributing to oneself, under the Withdraw permission and logged as such.
		const bool bSelf = RecipientId == ActorId;
		FGuild* Guild = nullptr;
		if (const EGuildError Error = CheckBankTake(
				ActorId, bSelf ? nullptr : &RecipientId,
				bSelf ? EGuildPermission::Withdraw : EGuildPermission::Redistribute, Guild);
			Error != EGuildError::None)
		{
			return Error;
		}
		if (Amount <= 0)
		{
			return EGuildError::InvalidAmount;
		}
		if (Guild->Bank.Gold < Amount)
		{
			return EGuildError::NotEnoughGold;
		}
		Guild->Bank.Gold -= Amount;
		FGuildBankLogEntry Entry;
		Entry.PlayerId = ActorId;
		Entry.Action = bSelf ? EGuildBankAction::WithdrawGold : EGuildBankAction::RedistributeGold;
		Entry.Gold = Amount;
		Entry.RecipientId = bSelf ? std::string() : RecipientId;
		Entry.Time = Now;
		Log(*Guild, std::move(Entry));
		return EGuildError::None;
	}

	FGuildItemResult FGuildRegistry::DepositItem(
		const std::string& ActorId, const FGuildBankItem& Item, const FGridPlacement* At, double Now)
	{
		FGuildItemResult Result;
		FGuild* Guild = nullptr;
		FGuildMember* Actor = nullptr;
		Result.Error = ActorGuild(ActorId, Guild, Actor);
		if (Result.Error != EGuildError::None)
		{
			return Result;
		}
		if (!GuildRankCan(Actor->Rank, EGuildPermission::Deposit))
		{
			Result.Error = EGuildError::NotPermitted;
			return Result;
		}
		if (!IsValidItem(Item))
		{
			Result.Error = EGuildError::InvalidItem;
			return Result;
		}

		FGuildBank& Bank = Guild->Bank;
		FGridPlacement Placement;
		if (At != nullptr ? !Bank.Grid.CanPlace(Item.Size, *At) : !Bank.Grid.FindFreeSpot(Item.Size, true, Placement))
		{
			Result.Error = EGuildError::NoRoom;
			return Result;
		}
		if (At != nullptr)
		{
			Placement = *At;
		}

		Result.Item = Item;
		Result.Item.SlotId = Bank.NextSlotId++;
		Bank.Grid.Place(Result.Item.SlotId, Item.Size, Placement);
		Bank.Items.push_back(Result.Item);

		FGuildBankLogEntry Entry;
		Entry.PlayerId = ActorId;
		Entry.Action = EGuildBankAction::DepositItem;
		Entry.ItemId = Item.ItemId;
		Entry.Rarity = Item.Rarity;
		Entry.Count = Item.Count;
		Entry.Time = Now;
		Log(*Guild, std::move(Entry));
		return Result;
	}

	FGuildItemResult FGuildRegistry::WithdrawItem(const std::string& ActorId, int SlotId, double Now)
	{
		return TakeItem(ActorId, nullptr, SlotId, Now);
	}

	FGuildItemResult FGuildRegistry::RedistributeItem(
		const std::string& ActorId, const std::string& RecipientId, int SlotId, double Now)
	{
		return TakeItem(ActorId, RecipientId == ActorId ? nullptr : &RecipientId, SlotId, Now);
	}

	EGuildError FGuildRegistry::MoveBankItem(const std::string& ActorId, int SlotId, FGridPlacement At)
	{
		FGuild* Guild = nullptr;
		FGuildMember* Actor = nullptr;
		if (const EGuildError Error = ActorGuild(ActorId, Guild, Actor); Error != EGuildError::None)
		{
			return Error;
		}
		if (!GuildRankCan(Actor->Rank, EGuildPermission::ManageBank))
		{
			return EGuildError::NotPermitted;
		}
		if (Guild->Bank.Find(SlotId) == nullptr)
		{
			return EGuildError::UnknownItem;
		}
		return Guild->Bank.Grid.Move(SlotId, At) ? EGuildError::None : EGuildError::NoRoom;
	}

	const FGuild* FGuildRegistry::Find(const std::string& GuildId) const
	{
		const auto Found = Guilds.find(GuildId);
		return Found == Guilds.end() ? nullptr : &Found->second;
	}

	const FGuild* FGuildRegistry::FindByName(const std::string& Name) const
	{
		const auto Found = NameIndex.find(NameKey(Name));
		return Found == NameIndex.end() ? nullptr : Find(Found->second);
	}

	const FGuild* FGuildRegistry::GuildOf(const std::string& PlayerId) const
	{
		const auto Found = PlayerGuilds.find(PlayerId);
		return Found == PlayerGuilds.end() ? nullptr : Find(Found->second);
	}

	std::optional<EGuildRank> FGuildRegistry::RankOf(const std::string& PlayerId) const
	{
		const FGuild* Guild = GuildOf(PlayerId);
		const FGuildMember* Member = Guild != nullptr ? Guild->FindMember(PlayerId) : nullptr;
		if (Member == nullptr)
		{
			return std::nullopt;
		}
		return Member->Rank;
	}

	EGuildError FGuildRegistry::ActorGuild(const std::string& ActorId, FGuild*& OutGuild, FGuildMember*& OutMember)
	{
		const auto Found = PlayerGuilds.find(ActorId);
		if (Found == PlayerGuilds.end())
		{
			return EGuildError::NotInGuild;
		}
		const auto Guild = Guilds.find(Found->second);
		FGuildMember* Member = Guild != Guilds.end() ? FindMutableMember(Guild->second, ActorId) : nullptr;
		if (Member == nullptr)
		{
			return EGuildError::NotInGuild;
		}
		OutGuild = &Guild->second;
		OutMember = Member;
		return EGuildError::None;
	}

	EGuildError FGuildRegistry::CheckMemberTarget(
		const std::string& ActorId, const std::string& TargetId, FGuild*& OutGuild,
		FGuildMember*& OutActor, FGuildMember*& OutTarget)
	{
		if (const EGuildError Error = ActorGuild(ActorId, OutGuild, OutActor); Error != EGuildError::None)
		{
			return Error;
		}
		if (TargetId == ActorId)
		{
			return EGuildError::CannotTargetSelf;
		}
		OutTarget = FindMutableMember(*OutGuild, TargetId);
		return OutTarget == nullptr ? EGuildError::TargetNotInGuild : EGuildError::None;
	}

	EGuildError FGuildRegistry::CheckBankTake(
		const std::string& ActorId, const std::string* RecipientId, EGuildPermission Permission, FGuild*& OutGuild)
	{
		FGuildMember* Actor = nullptr;
		if (const EGuildError Error = ActorGuild(ActorId, OutGuild, Actor); Error != EGuildError::None)
		{
			return Error;
		}
		if (!GuildRankCan(Actor->Rank, Permission))
		{
			return EGuildError::NotPermitted;
		}
		if (RecipientId != nullptr && OutGuild->FindMember(*RecipientId) == nullptr)
		{
			return EGuildError::TargetNotInGuild;
		}
		return EGuildError::None;
	}

	FGuildItemResult FGuildRegistry::TakeItem(
		const std::string& ActorId, const std::string* RecipientId, int SlotId, double Now)
	{
		FGuildItemResult Result;
		FGuild* Guild = nullptr;
		Result.Error = CheckBankTake(
			ActorId, RecipientId, RecipientId != nullptr ? EGuildPermission::Redistribute : EGuildPermission::Withdraw,
			Guild);
		if (Result.Error != EGuildError::None)
		{
			return Result;
		}

		FGuildBank& Bank = Guild->Bank;
		for (size_t I = 0; I < Bank.Items.size(); ++I)
		{
			if (Bank.Items[I].SlotId != SlotId)
			{
				continue;
			}
			Result.Item = std::move(Bank.Items[I]);
			Bank.Items.erase(Bank.Items.begin() + static_cast<std::ptrdiff_t>(I));
			Bank.Grid.Remove(SlotId);

			FGuildBankLogEntry Entry;
			Entry.PlayerId = ActorId;
			Entry.Action = RecipientId != nullptr ? EGuildBankAction::RedistributeItem : EGuildBankAction::WithdrawItem;
			Entry.ItemId = Result.Item.ItemId;
			Entry.Rarity = Result.Item.Rarity;
			Entry.Count = Result.Item.Count;
			Entry.RecipientId = RecipientId != nullptr ? *RecipientId : std::string();
			Entry.Time = Now;
			Log(*Guild, std::move(Entry));
			return Result;
		}
		Result.Error = EGuildError::UnknownItem;
		return Result;
	}

	void FGuildRegistry::Log(FGuild& Guild, FGuildBankLogEntry Entry)
	{
		std::vector<FGuildBankLogEntry>& BankLog = Guild.BankLog;
		BankLog.push_back(std::move(Entry));
		const size_t Keep = Config.BankLogSize > 0 ? static_cast<size_t>(Config.BankLogSize) : 0;
		if (BankLog.size() > Keep)
		{
			BankLog.erase(BankLog.begin(), BankLog.end() - static_cast<std::ptrdiff_t>(Keep));
		}
	}

	void FGuildRegistry::RemoveMember(FGuild& Guild, const std::string& PlayerId)
	{
		for (size_t I = 0; I < Guild.Members.size(); ++I)
		{
			if (Guild.Members[I].PlayerId == PlayerId)
			{
				Guild.Members.erase(Guild.Members.begin() + static_cast<std::ptrdiff_t>(I));
				break;
			}
		}
		PlayerGuilds.erase(PlayerId);
	}

	void FGuildRegistry::AddMember(FGuild& Guild, const std::string& PlayerId, EGuildRank Rank)
	{
		Guild.Members.push_back({PlayerId, Rank});
		PlayerGuilds.emplace(PlayerId, Guild.Id);
	}
}
