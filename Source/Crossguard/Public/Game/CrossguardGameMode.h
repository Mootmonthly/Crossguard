#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Teams.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "Items/CrossguardLoot.h"

#include "CrossguardGameMode.generated.h"

class ACrossguardCharacter;
class ACrossguardPlayerState;
class UCrossguardSkillCatalog;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardPlayerExtracted, ACrossguardCharacter*, Character);

// A raider whose connection closed mid-raid: their character waits in the world for them to log in
// again (ACrossguardGameMode::HoldForReconnect).
struct FCrossguardHeldPlayer
{
	TWeakObjectPtr<ACrossguardPlayerState> PlayerState;
	TWeakObjectPtr<ACrossguardCharacter> Character;
	// Their FTeamRegistry key: the team slot stays theirs while they are away.
	FString TeamKey;
};

// Match game mode. Assigns teams on login from the "Party" URL option: players joining with the
// same ?Party=<key> share a team (up to 8); players without one each get their own team.
// Matchmaking must put the party key in the travel URL. In the hub, in-game parties
// (UCrossguardPartySubsystem, docs/parties.md) move their members onto one team with SetPlayerParty
// and launch the raid with RaidMapName / RaidServerAddress.
UCLASS()
class CROSSGUARD_API ACrossguardGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACrossguardGameMode();

	// A "?Hub" option on the server's map URL turns bIsRaid off (a dev hub on any map).
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	// Notes whether the login is this process's own player (for the development player id).
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options,
		const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	// Refuses a login whose party's team is already full.
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	// Also: refuses a player id already playing here, or whose raid on this map is over; gives a
	// player held for a reconnect their own player state back (docs/persistence.md, "Reconnecting").
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;
	// A player held for a reconnect keeps their save record, team slot and party.
	virtual void Logout(AController* Exiting) override;
	// Seamless travel skips InitNewPlayer: re-register the player with the party key their player
	// state carried over.
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;
	// Starts the raid for each player when this map is a raid. A reconnecting player takes their
	// waiting character back instead (no new pawn, no new raid).
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	// No respawn into a raid the player already left (extracted or dead for the raid), even if a
	// client forges ServerRestartPlayer.
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;

	// Server. Called by the attribute set when damage brings a character to 0 Health. Awards kill
	// XP to the killer's player: PlayerKill for a player-controlled victim, otherwise the victim's
	// KillXpSource. No XP for killing yourself or an ally.
	virtual void HandleKill(AActor* Victim, AActor* Killer);

	// Server. Called by an extraction zone: ends the raid for the character's player as a success.
	// Dead characters (State.Dead) cannot extract. The character then leaves the world (destroyed
	// next tick) and its player spectates a teammate still in the raid.
	virtual void HandleExtraction(ACrossguardCharacter* Character);

	// Server. The character's revive window ran out: its player's raid ends as a failure. The player
	// spectates a teammate still in the raid, or goes back to the menu if none is left (or solo). A
	// player away (held for a reconnect) is let go: the raid failed, and they cannot log in to it again.
	virtual void HandlePermanentDeath(ACrossguardCharacter* Character);

	// --- Reconnecting (docs/persistence.md, "Reconnecting"). Server. ---

	// The player's connection is closing (ACrossguardPlayerController::PawnLeavingGame). On a raid map,
	// a character still in the raid (alive or downed, not extracted) stays where it is, unpossessed and
	// vulnerable, and its player state (progression, raid XP, team, party) is kept, so the same player
	// id can log in again and take it back. Returns true if held.
	virtual bool HoldForReconnect(APlayerController* Leaving);
	// The held player state of PlayerId (UCrossguardGuildSubsystem::PlayerIdOf), or null.
	ACrossguardPlayerState* FindHeldPlayer(const FString& PlayerId) const;
	// A login with this player id is refused: their raid on this map is over (extracted, or dead for the
	// raid, also while away).
	bool HasFinishedRaidHere(const FString& PlayerId) const { return FinishedPlayerIds.Contains(PlayerId); }

	// Login option asking for a development player id (?DevPlayerId=<name>, becomes "dev-<name>").
	// Development builds only, and only on a server started with -AllowDevPlayerIds (or in automation
	// tests): it is the client's word.
	static const TCHAR* DevPlayerIdOption;

	// After a successful extraction. Hook the trip to the hub here once it exists.
	UPROPERTY(BlueprintAssignable, Category = "Crossguard")
	FCrossguardPlayerExtracted OnPlayerExtracted;

	virtual void StartPlay() override;

	// Seconds into the raid before extract portals can be summoned. Lower it to test.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard", meta = (ClampMin = "0"))
	float PortalUnlockSeconds = 600.0f;

	// Seconds since the raid started (0 outside raid maps).
	UFUNCTION(BlueprintPure, Category = "Crossguard")
	float GetRaidElapsedSeconds() const;

	// Raid maps hold XP until extraction and block between-raid actions (learning Skill Gems,
	// changing attributes). Turn off for a hub / test map where those should work.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard")
	bool bIsRaid = true;

	// Loot tables and the item data they draw from. Set every table here (the same assets the
	// character uses) for chests and monster drops to work.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Loot")
	FCrossguardLootSources Loot;

	// Imported from Data/Mobs/MobTable.json. Row struct: CrossguardMobRow. Monsters with a MobId
	// take their stats from it.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Mobs")
	TObjectPtr<UDataTable> MobTable;

	// The skill catalog (DA_SkillCatalog) mobs' skills are looked up in. Unset: native skills only
	// (data passives such as Sword Mastery do nothing).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Mobs")
	TObjectPtr<UCrossguardSkillCatalog> MobSkillCatalog;

	// Server. Rolls a loot table with a fresh random seed. Empty if the table or data is missing.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Loot")
	TArray<FCrossguardInventoryItem> RollLoot(FName TableId);

	static const TCHAR* PartyOption;
	// Placeholder until a hub map exists: open any map with ?Hub to play it as the hub.
	static const TCHAR* HubOption;

	// Server. Moves the player onto the team of party key Party (empty: a team of their own), as if
	// they had logged in with ?Party=<Party>. No-op if that is their key already.
	void SetPlayerParty(AController* Controller, const FString& Party);

	// --- Raid launch from the hub (docs/parties.md). Placeholders until a matchmaker exists. ---

	// The raid map a party that is the whole server travels to (ServerTravel), e.g.
	// "/Game/Maps/L_Raid". The -RaidMap=<path> command line option overrides it. Empty: none.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Raid Launch")
	FString RaidMapName;

	// A separate raid server ("203.0.113.5:7777") the party's clients travel to while the rest of the
	// hub stays. The -RaidServer=<address> command line option overrides it. Empty: none.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Raid Launch")
	FString RaidServerAddress;

	FString GetRaidMapName() const;
	FString GetRaidServerAddress() const;

private:
	static std::string PlayerKey(const FUniqueNetIdRepl& UniqueId, const AController* Controller);
	static std::string PartyKey(const FString& Options);
	// Registers the player and sets their team. Returns an error message, or empty on success.
	FString JoinTeam(AController* Controller, const std::string& Key, const FString& Party);

	// Re-targets every spectator whose teammate left the raid; sends them to the menu when none is
	// left.
	void UpdateSpectators();
	// A teammate of Player still in the raid; null if none.
	APawn* FindSpectateTarget(const APlayerController* Player) const;

	Crossguard::FTeamRegistry Teams;
	TArray<TWeakObjectPtr<APlayerController>> Spectators;
	// Players who extracted from this raid: they spectate, but are never sent to the menu.
	TArray<TWeakObjectPtr<APlayerController>> ExtractedPlayers;
	float RaidStartSeconds = 0.0f;
	// Controller -> registry key, so Logout can find the player.
	TMap<TWeakObjectPtr<const AController>, FString> PlayerKeys;

	// The stable stand-in id for a login without an online id (ACrossguardPlayerState::DevPlayerId);
	// empty in Shipping or when none applies.
	FString DevPlayerIdFor(const FString& Options) const;
	// A player state here, not held for a reconnect and not Except, whose PlayerIdOf is PlayerId.
	const ACrossguardPlayerState* FindOnlinePlayer(const FString& PlayerId, const APlayerState* Except) const;
	// The id PlayerState is held under; empty if it is not held.
	FString HeldIdOf(const ACrossguardPlayerState* PlayerState) const;
	// The reconnecting controller takes over the held player state (before Super::InitNewPlayer).
	FString RestoreHeldPlayer(APlayerController* Controller, const FString& PlayerId, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal);
	// The held player's character died for the raid: their raid fails and the player state goes.
	void ReleaseHeldPlayer(const FString& PlayerId);
	// Possesses the waiting character again, keeping its Health.
	void ResumeHeldCharacter(APlayerController* Player, ACrossguardCharacter* Character);

	// Raiders away from the raid, by player id.
	TMap<FString, FCrossguardHeldPlayer> HeldPlayers;
	// Reconnected controllers waiting for HandleStartingNewPlayer, and their characters.
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<ACrossguardCharacter>> Reconnecting;
	// Players whose raid on this map is over: they cannot log in to it again.
	TSet<FString> FinishedPlayerIds;
	// During Login of this process's own player (a ULocalPlayer: standalone, listen-server host).
	bool bLoggingInLocalPlayer = false;
};
