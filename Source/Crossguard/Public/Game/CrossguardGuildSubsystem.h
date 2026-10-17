#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Guild.h"
#include "Game/CrossguardGuildComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "CrossguardGuildSubsystem.generated.h"

class ACrossguardPlayerState;
class FJsonObject;
struct FCrossguardNakamaResponse;
struct FUniqueNetIdRepl;

// Server: every guild (Crossguard::FGuildRegistry) and the items in guild banks. Players act
// through their UCrossguardGuildComponent; after each change the subsystem refreshes the views of
// every member on this server. Lives with the game instance (survives map travel). Docs:
// docs/guild-rules.md.
//
// Two modes, like parties:
// - In memory (default: development, PIE, tests): the registry here is the guilds; gone when the
//   process stops.
// - Backend (UCrossguardNakamaSubsystem::IsBackendEnabled, docs/guild-rules.md "On Nakama"): Nakama holds
//   the guilds (Backend/nakama/modules/guilds.go, the same rules), the same on every hub shard. This
//   server checks the game's own rules (between raids, the inventory, the recipient here, room), sends
//   the request (crossguard_guild) and returns None ("sent"); the answer updates the registry here (a
//   mirror of the guilds of the players on this server) and a refusal comes later through
//   OnGuildRefused. Bank moves change the player at once and carry their save to Nakama
//   (UCrossguardNakamaSaveBackend::SendWithSave): the bank and the save are written together, or the
//   player is put back. The mirror refreshes when a player arrives and every GuildRefreshSeconds.
//   Players need a Nakama login; others are refused (NoPlayer).
UCLASS()
class CROSSGUARD_API UCrossguardGuildSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Backend mode: how often the guilds of everyone on this server are read again (placeholder).
	static constexpr float GuildRefreshSeconds = 10.0f;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Guilds live in Nakama (the backend is on).
	bool UsesBackend() const { return bBackend; }

	// The id guilds (and saves, parties) know a player by: the server-verified Nakama user id
	// (ACrossguardPlayerState::NakamaUserId), else their online id (a Steam id with Steam), else in development
	// builds the server-set stand-in ACrossguardPlayerState::DevPlayerId ("pie-<n>", "dev-<name>"),
	// else a stand-in for this session only ("session-<n>").
	static FString PlayerIdOf(const APlayerState* PlayerState);
	// A unique id worth keying saves and guilds by: valid and from a real online subsystem. The Null
	// subsystem (PIE, no Steam) makes up a new id every run, so it does not count.
	static bool IsOnlineId(const FUniqueNetIdRepl& UniqueId);

	ECrossguardGuildResult Create(ACrossguardPlayerState* Actor, const FString& Name);
	ECrossguardGuildResult Invite(ACrossguardPlayerState* Actor, ACrossguardPlayerState* Invitee);
	ECrossguardGuildResult Accept(ACrossguardPlayerState* Actor, const FString& GuildId);
	ECrossguardGuildResult Decline(ACrossguardPlayerState* Actor, const FString& GuildId);
	ECrossguardGuildResult Kick(ACrossguardPlayerState* Actor, const FString& TargetId);
	ECrossguardGuildResult SetRank(ACrossguardPlayerState* Actor, const FString& TargetId, ECrossguardGuildRank NewRank);
	ECrossguardGuildResult Leave(ACrossguardPlayerState* Actor);
	// Positive: deposit banked gold; negative: withdraw.
	ECrossguardGuildResult MoveGold(ACrossguardPlayerState* Actor, int32 Amount);
	ECrossguardGuildResult DepositItem(ACrossguardPlayerState* Actor, int32 InventoryItemId);
	ECrossguardGuildResult WithdrawItem(ACrossguardPlayerState* Actor, int32 SlotId);
	ECrossguardGuildResult GiveGold(ACrossguardPlayerState* Actor, const FString& RecipientId, int32 Amount);
	ECrossguardGuildResult GiveItem(ACrossguardPlayerState* Actor, const FString& RecipientId, int32 SlotId);

	// Rebuilds one player's guild view (on joining the server, after changes).
	void RefreshPlayer(ACrossguardPlayerState* PlayerState);

	// Their online id is known: their own view, and their guildmates see them online. With the
	// backend, their guild is read from Nakama first.
	void PlayerJoined(ACrossguardPlayerState* PlayerState);

	// Backend mode: applies a crossguard_guild / crossguard_guild_get answer to the mirror here and
	// brings every affected player on this server up to date. Returns the answer's "result" (an
	// EGuildError name or BankChanged; empty for a get; "<unreadable>" for garbage). Public for tests.
	FString ApplyBackendState(const FString& Body);
	// "None", "NotPermitted", "BankChanged", ... -> the result (unknown names: BackendUnavailable).
	static ECrossguardGuildResult ResultFromName(const FString& Name);
	// A guild of an answer's "guilds" map as the rules' data (bank grid and items placed) and each bank
	// stack's payload by slot. False if it is not a readable guild.
	static bool ParseGuildView(const FJsonObject& View, Crossguard::FGuild& OutGuild, TMap<int32, FString>& OutPayloads);
	// A crossguard_guild payload: {"op":Op,"actor_id":ActorId,"server_id":ServerId, then each field as
	// "name":<raw JSON value>}.
	static FString BuildOpPayload(const FString& Op, const FString& ActorId, const FString& ServerId, const TArray<TPair<FString, FString>>& Fields);
	// The "item" member of a deposit: definition id, rarity, count, size and the stack as a payload.
	static FString BuildItemJson(const FCrossguardInventoryItem& Item);

	// Read-only view of every guild (tests; with the backend, the mirror).
	const Crossguard::FGuildRegistry& GetRegistry() const { return Registry; }

private:
	// What a bank operation changes on the player whose save it carries (backend mode).
	struct FBankChange
	{
		// Added to their banked gold (negative: taken).
		int32 GoldDelta = 0;
		// Inventory stacks taken (a deposit).
		TArray<int32> RemoveIds;
		// Stacks given (a withdrawal, a gift, the last member's share).
		TArray<FCrossguardInventoryItem> Add;
	};
	// What was done, to put back if Nakama does not write it.
	struct FBankUndo
	{
		int32 GoldDelta = 0;
		TArray<FCrossguardInventoryItem> Removed;
		TArray<FCrossguardInventoryItem> Added;
		TArray<int32> AddedIds;
	};

	// Backend mode.
	ECrossguardGuildResult SendBackendOp(const TCHAR* Op, ACrossguardPlayerState* Actor, TArray<TPair<FString, FString>> Fields);
	// A bank operation: the change on SavePlayer (the actor, or a gift's recipient) now, their save with
	// it to Nakama; undone if Nakama does not write it.
	ECrossguardGuildResult SendBankOp(const TCHAR* Op, ACrossguardPlayerState* Actor, ACrossguardPlayerState* SavePlayer,
		TArray<TPair<FString, FString>> Fields, const FBankChange& Change);
	static bool ApplyBankChange(ACrossguardPlayerState* Player, const FBankChange& Change, FBankUndo& OutUndo);
	static void UndoBankChange(ACrossguardPlayerState* Player, const FBankUndo& Undo);
	void FetchFromBackend(const TArray<FString>& PlayerIds);
	bool RefreshTick(float DeltaSeconds);
	// A refusal that comes after the request returned (an asynchronous answer).
	void RefuseLater(const FString& ActorId, ECrossguardGuildResult Result);
	void ForgetMirroredGuild(const FString& GuildId);
	UWorld* GetServerWorld() const;

	static ECrossguardGuildResult ToResult(Crossguard::EGuildError Error);
	static double Now();
	bool IsBetweenRaids(const ACrossguardPlayerState* Actor) const;
	// Every online player of the guild (and Extra, e.g. someone who just left) gets a fresh view.
	void RefreshGuild(const std::string& GuildId, ACrossguardPlayerState* Extra = nullptr);
	ACrossguardPlayerState* FindOnline(const UWorld* World, const FString& PlayerId) const;
	// The game's copy of a bank stack (affixes and all), keyed by guild and slot.
	static FString BankKey(const std::string& GuildId, int SlotId);

	Crossguard::FGuildRegistry Registry;
	TMap<FString, FCrossguardInventoryItem> BankItemData;
	// Last seen names, for rosters.
	TMap<FString, FString> DisplayNames;
	bool bBackend = false;
	FTSTicker::FDelegateHandle RefreshHandle;
	// Backend mode: each player's open invites (guild id, guild name, inviter's id), from guilds this
	// server may not mirror.
	TMap<FString, TArray<FCrossguardGuildInviteView>> BackendInvites;
	// Backend mode: players with a bank operation on its way (one at a time; others answer Busy).
	TSet<FString> BankBusy;
};
