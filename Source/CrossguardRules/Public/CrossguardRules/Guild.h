// Guilds: membership, ranks, invites and the guild bank (docs/guild-rules.md).
//
// Ruling: members deposit; officers withdraw, manage and redistribute. Everything else here is
// an assumption (listed in the doc, numbers in FGuildConfig). Player and guild ids are opaque
// strings chosen by the caller (a Steam id, a backend id). Time is passed in as Now (seconds on
// any clock the caller keeps); the library reads no clocks.
//
// Every operation returns EGuildError::None on success and changes nothing on error.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/Inventory.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/RulesApi.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Crossguard
{
	enum class EGuildRank
	{
		Member,
		Officer,
		// Exactly one per guild.
		Leader,
	};

	enum class EGuildError
	{
		None,
		// Empty player id.
		InvalidPlayer,
		// Empty guild id.
		InvalidGuildId,
		GuildIdTaken,
		// Wrong length or characters after trimming (see ValidateGuildName).
		InvalidName,
		// Another guild has the same name, ignoring case.
		NameTaken,
		UnknownGuild,
		// The acting player is in no guild.
		NotInGuild,
		// The player (founder, invitee) is already in a guild: one guild per player.
		AlreadyInGuild,
		// The target player is not a member of the actor's guild.
		TargetNotInGuild,
		// The actor's rank does not allow it.
		NotPermitted,
		// Kick, promote, demote or transfer aimed at the actor.
		CannotTargetSelf,
		// Promote an Officer, demote a Member.
		AlreadyRank,
		GuildFull,
		// An unexpired invite from this guild is already pending.
		AlreadyInvited,
		NoInvite,
		InviteExpired,
		// The Leader leaves only as the last member; otherwise TransferLeadership first.
		LeaderMustTransfer,
		// Gold amount <= 0.
		InvalidAmount,
		NotEnoughGold,
		// The bank balance would pass INT64_MAX.
		GoldOverflow,
		// Empty item id, count <= 0 or a non-positive size.
		InvalidItem,
		// No bank item with that slot id.
		UnknownItem,
		// The item does not fit at the requested spot, or anywhere when no spot is given.
		NoRoom,
		// Restore: inconsistent saved data.
		InvalidData,
	};

	enum class EGuildPermission
	{
		Invite,
		// Promote, demote, transfer leadership.
		ManageRanks,
		Deposit,
		// Withdraw gold or items to oneself.
		Withdraw,
		// Give bank gold or items to a named member.
		Redistribute,
		// Rearrange the bank grid.
		ManageBank,
	};

	// Kicking is not here: it depends on the target's rank too (see CanKick).
	CROSSGUARDRULES_API bool GuildRankCan(EGuildRank Rank, EGuildPermission Permission);

	// Leader kicks anyone but themself; Officer kicks Members only. Self-kicks are handled by the
	// caller (CannotTargetSelf); this only compares ranks.
	CROSSGUARDRULES_API bool CanKick(EGuildRank Actor, EGuildRank Target);

	// Trims leading and trailing spaces. The result is what gets stored.
	CROSSGUARDRULES_API std::string TrimGuildName(const std::string& Name);

	// None or InvalidName: MinNameLength..MaxNameLength characters after trimming, ASCII letters,
	// digits, spaces and '-' only.
	CROSSGUARDRULES_API EGuildError ValidateGuildName(const FGuildConfig& Config, const std::string& Name);

	struct FGuildMember
	{
		std::string PlayerId;
		EGuildRank Rank = EGuildRank::Member;
	};

	struct FGuildInvite
	{
		std::string PlayerId;
		std::string InvitedBy;
		// Expired once Now >= ExpiresAt.
		double ExpiresAt = 0.0;
	};

	struct FGuildBankItem
	{
		// Id in the bank grid, assigned by the bank on deposit (> 0). Ignored on deposit.
		int SlotId = 0;
		// Item definition id ("healing_potion"). Opaque to the library.
		std::string ItemId;
		ERarity Rarity = ERarity::Common;
		int Count = 1;
		// Unrotated size.
		FGridSize Size;
		// Item state the game round-trips (affixes, durability). Never read by the library.
		std::string Payload;
	};

	struct FGuildBank
	{
		// Never negative.
		std::int64_t Gold = 0;
		// Footprints by SlotId; Items holds what each slot is.
		FInventoryGrid Grid;
		std::vector<FGuildBankItem> Items;
		int NextSlotId = 1;

		const FGuildBankItem* Find(int SlotId) const;
	};

	enum class EGuildBankAction
	{
		DepositGold,
		WithdrawGold,
		RedistributeGold,
		DepositItem,
		WithdrawItem,
		RedistributeItem,
	};

	struct FGuildBankLogEntry
	{
		// Who acted.
		std::string PlayerId;
		EGuildBankAction Action = EGuildBankAction::DepositGold;
		// Item actions: the item definition id, its rarity and stack count. Empty / 0 for gold.
		std::string ItemId;
		ERarity Rarity = ERarity::Common;
		int Count = 0;
		// Gold actions: the amount. 0 for items.
		std::int64_t Gold = 0;
		// Redistribute: who received it. Empty otherwise.
		std::string RecipientId;
		double Time = 0.0;
	};

	// Plain data, so a save layer can write it field by field and hand it back to Restore.
	struct FGuild
	{
		std::string Id;
		// Trimmed, as typed (case kept).
		std::string Name;
		// In join order; the founder first.
		std::vector<FGuildMember> Members;
		std::vector<FGuildInvite> Invites;
		FGuildBank Bank;
		// Oldest first, at most BankLogSize entries.
		std::vector<FGuildBankLogEntry> BankLog;

		const FGuildMember* FindMember(const std::string& PlayerId) const;
		const std::string& LeaderId() const;
	};

	struct FGuildLeaveResult
	{
		EGuildError Error = EGuildError::None;
		// The last member left: the guild is gone and its name free again.
		bool bDisbanded = false;
		// Disbanded only: the bank contents, for the game to hand to the last member.
		std::int64_t Gold = 0;
		std::vector<FGuildBankItem> Items;

		bool IsValid() const { return Error == EGuildError::None; }
	};

	struct FGuildItemResult
	{
		EGuildError Error = EGuildError::None;
		// Deposit: the item as stored (its SlotId set). Withdraw / redistribute: the item to hand
		// over; where it lands is the game's job.
		FGuildBankItem Item;

		bool IsValid() const { return Error == EGuildError::None; }
	};

	// Every guild, a case-insensitive name index and a player -> guild index. Operations act on
	// the actor's own guild, found through the index.
	class CROSSGUARDRULES_API FGuildRegistry
	{
	public:
		explicit FGuildRegistry(const FGuildConfig& InConfig = FGuildConfig());

		const FGuildConfig& GetConfig() const { return Config; }

		// The founder becomes Leader. The bank starts empty, BankWidth × BankHeight.
		EGuildError Create(const std::string& GuildId, const std::string& Name, const std::string& FounderId);

		// Loads a saved guild. Checks ids, the name, exactly one Leader, no member already in a
		// guild, unique members and a bank grid that matches its items. MaxMembers is not
		// enforced, so lowering it never breaks a saved guild.
		EGuildError Restore(const FGuild& Guild);

		// Leader or Officer invites a player in no guild. Replaces an expired invite to the same
		// player. Pending invites do not reserve a place.
		EGuildError Invite(const std::string& ActorId, const std::string& InviteeId, double Now);
		EGuildError Accept(const std::string& GuildId, const std::string& PlayerId, double Now);
		// Removes the invite, expired or not.
		EGuildError Decline(const std::string& GuildId, const std::string& PlayerId);
		// Drops expired invites from every guild. Returns how many were dropped.
		int PruneExpiredInvites(double Now);

		EGuildError Kick(const std::string& ActorId, const std::string& TargetId);
		// Member -> Officer and back. Leader only.
		EGuildError Promote(const std::string& ActorId, const std::string& TargetId);
		EGuildError Demote(const std::string& ActorId, const std::string& TargetId);
		// Leader only; the old Leader becomes Officer.
		EGuildError TransferLeadership(const std::string& ActorId, const std::string& TargetId);
		// The Leader must transfer first unless they are the last member; then the guild disbands.
		FGuildLeaveResult Leave(const std::string& PlayerId);

		// Bank. The library does not know the player's own gold or inventory: on None the caller
		// takes the deposit from the player, or gives them what was withdrawn. Redistributing to
		// oneself is a withdrawal (Withdraw permission, logged as one).
		EGuildError DepositGold(const std::string& ActorId, std::int64_t Amount, double Now);
		EGuildError WithdrawGold(const std::string& ActorId, std::int64_t Amount, double Now);
		EGuildError RedistributeGold(
			const std::string& ActorId, const std::string& RecipientId, std::int64_t Amount, double Now);
		// At null: first free spot (rotation allowed). Stacks are never merged in the bank.
		FGuildItemResult DepositItem(
			const std::string& ActorId, const FGuildBankItem& Item, const FGridPlacement* At, double Now);
		// Whole stacks only.
		FGuildItemResult WithdrawItem(const std::string& ActorId, int SlotId, double Now);
		FGuildItemResult RedistributeItem(
			const std::string& ActorId, const std::string& RecipientId, int SlotId, double Now);
		// Moves or rotates a bank item. Not logged.
		EGuildError MoveBankItem(const std::string& ActorId, int SlotId, FGridPlacement At);

		// A mirror of guilds kept elsewhere (the Nakama backend, docs/guild-rules.md "On Nakama"): puts
		// Guild in, replacing this registry's copy of it and anything stale in its way (another guild
		// under its name, its members' places in other guilds; a guild left empty goes). Then the
		// Restore checks; on error the registry is as it was before the call.
		EGuildError Mirror(const FGuild& Guild);
		// Forgets a guild (disbanded elsewhere). False if unknown.
		bool RemoveGuild(const std::string& GuildId);
		// The player is in no guild (elsewhere): drops them from the one they are in here (a guild left
		// empty goes; a Leader dropped leaves none, until the next Mirror of that guild).
		void DropPlayer(const std::string& PlayerId);

		const FGuild* Find(const std::string& GuildId) const;
		const FGuild* FindByName(const std::string& Name) const;
		const FGuild* GuildOf(const std::string& PlayerId) const;
		std::optional<EGuildRank> RankOf(const std::string& PlayerId) const;
		// For saving.
		const std::unordered_map<std::string, FGuild>& All() const { return Guilds; }

	private:
		// Finds the actor's guild and member entry; NotInGuild if none.
		EGuildError ActorGuild(const std::string& ActorId, FGuild*& OutGuild, FGuildMember*& OutMember);
		EGuildError CheckMemberTarget(
			const std::string& ActorId, const std::string& TargetId, FGuild*& OutGuild,
			FGuildMember*& OutActor, FGuildMember*& OutTarget);
		EGuildError CheckBankTake(
			const std::string& ActorId, const std::string* RecipientId, EGuildPermission Permission,
			FGuild*& OutGuild);
		FGuildItemResult TakeItem(
			const std::string& ActorId, const std::string* RecipientId, int SlotId, double Now);
		void Log(FGuild& Guild, FGuildBankLogEntry Entry);
		void RemoveMember(FGuild& Guild, const std::string& PlayerId);
		void AddMember(FGuild& Guild, const std::string& PlayerId, EGuildRank Rank);

		FGuildConfig Config;
		std::unordered_map<std::string, FGuild> Guilds;
		// Lowercased name -> guild id.
		std::unordered_map<std::string, std::string> NameIndex;
		// Player id -> guild id.
		std::unordered_map<std::string, std::string> PlayerGuilds;
	};
}
