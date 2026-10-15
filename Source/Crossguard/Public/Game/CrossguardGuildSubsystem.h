#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Guild.h"
#include "Game/CrossguardGuildComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "CrossguardGuildSubsystem.generated.h"

class ACrossguardPlayerState;

// Server: every guild (Crossguard::FGuildRegistry) and the items in guild banks. Players act
// through their UCrossguardGuildComponent; after each change the subsystem refreshes the views of
// every member on this server. Lives with the game instance (survives map travel) and only in
// memory: guilds are lost when the server stops, until a backend saves FGuildRegistry::All() and
// the bank items.
UCLASS()
class CROSSGUARD_API UCrossguardGuildSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// The id guilds know a player by: their online id (a Steam id with Steam), else a stand-in
	// for this session only (PIE without an online subsystem).
	static FString PlayerIdOf(const APlayerState* PlayerState);

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

private:
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
};
