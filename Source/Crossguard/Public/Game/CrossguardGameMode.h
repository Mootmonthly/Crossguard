#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Teams.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "Items/CrossguardLoot.h"
#include "TimerManager.h"

#include "CrossguardGameMode.generated.h"

class ACrossguardCharacter;
class ACrossguardPlayerState;
class APlayerController;
class UCrossguardSkillCatalog;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardPlayerExtracted, ACrossguardCharacter*, Character);
// The match on this match server is over (ACrossguardGameMode::IsMatchOver): the hook for the host
// (GameLift's ProcessEnding, later).
DECLARE_MULTICAST_DELEGATE(FCrossguardMatchOver);

// A raider whose connection closed mid-raid: their character waits in the world for them to log in
// again (ACrossguardGameMode::HoldForReconnect).
struct FCrossguardHeldPlayer
{
	TWeakObjectPtr<ACrossguardPlayerState> PlayerState;
	TWeakObjectPtr<ACrossguardCharacter> Character;
	// Their FTeamRegistry key: the team slot stays theirs while they are away.
	FString TeamKey;
};

// Where a player leaving this match server goes (ACrossguardGameMode::GetHubDestination).
enum class ECrossguardHubDestination : uint8
{
	// A dedicated hub server (the backend's pick, else HubServerAddress / -HubServer=): the client travels.
	HubServer,
	// DEV-ONLY (listen server): the whole server travels back to the hub map (ReturnToHub).
	DevServerTravel,
	// Nowhere (a dedicated or Shipping server that knows no hub server): the main menu, with a message.
	Menu,
};

struct FCrossguardHubDestination
{
	ECrossguardHubDestination Kind = ECrossguardHubDestination::Menu;
	// HubServer: the hub server's address; DevServerTravel: the hub map.
	FString Address;
	// For logs: "backend", "-HubServer", "dev listen server", "none".
	FString Source;
};

// How one member of a team that finished goes back to the hub (ACrossguardGameMode::ReturnTeamToHub).
enum class ECrossguardTeamHubTravel : uint8
{
	// To a hub server (GetHubDestination) with ?Party=<key>.
	HubServer,
	// No hub server known: the main menu (NoHubReason).
	Menu,
};

// One member's part of a team's trip back.
struct FCrossguardTeamHubTravel
{
	FString PlayerId;
	TWeakObjectPtr<APlayerController> Player;
	ECrossguardTeamHubTravel Mode = ECrossguardTeamHubTravel::Menu;
	FString Url;
};

// A team's countdown to its early trip back.
struct FCrossguardTeamHubCountdown
{
	// World time of the travel.
	double At = 0.0;
	// The last whole second shown on the team's HUD.
	int32 ShownSeconds = -1;
	bool bWaitsForHeld = false;
};

// Match game mode. Assigns teams on login from the "Party" URL option: players joining with the
// same ?Party=<key> share a team (up to 8); players without one each get their own team.
// Development travel URLs carry the party key; real match servers will learn the team from the
// backend's match ticket (?MatchTicket=, not checked yet). In the hub, in-game parties
// (UCrossguardPartySubsystem, docs/parties.md) move their members onto one team with SetPlayerParty
// and launch the raid through matchmaking (UCrossguardMatchmakingSubsystem).
//
// Owner's architecture: raids on dedicated match servers many teams join, hubs on dedicated hub
// servers, a backend for accounts, saves, parties and matchmaking. The listen-server flow is the
// single-PC development mode only (Crossguard::IsDevListenMode, tagged "DEV-ONLY (listen server)").
UCLASS()
class CROSSGUARD_API ACrossguardGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACrossguardGameMode();

	// A hub map (IsHubMap: a "?Hub" URL option or a placed ACrossguardHubMarker) turns bIsRaid off.
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	// Notes whether the login is this process's own player (for the development player id).
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options,
		const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	// Refuses a login whose party's team is already full.
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	// A login carrying ?NakamaToken= waits while Nakama verifies the token (crossguard_verify_session);
	// a token that does not verify is refused, and the rest of PreLogin runs only after it does
	// (docs/anti-cheat.md, "Player identity"). No token: PreLogin as usual (InitNewPlayer refuses the
	// login where the server requires a Nakama login).
	virtual void PreLoginAsync(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		const FOnPreLoginCompleteDelegate& OnComplete) override;
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
	// spectates a teammate still in the raid; with none left (or solo) they keep watching their body
	// and go back to the hub with their team (ruled; the menu only when there is no hub map). A player
	// away (held for a reconnect) is let go: the raid failed, and they cannot log in to it again.
	virtual void HandlePermanentDeath(ACrossguardCharacter* Character);

	// --- Reconnecting (docs/persistence.md, "Reconnecting"). Server. ---

	// The player's connection is closing (ACrossguardPlayerController::PawnLeavingGame). On a raid map,
	// a character still in the raid (alive or downed, not extracted) stays where it is, unpossessed and
	// vulnerable, and its player state (progression, raid XP, team, party) is kept, so the same player
	// id can log in again and take it back. Returns true if held. Never for a player leaving with their
	// team (IsLeavingForHub): a deliberate travel.
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
	// Development builds: Url plus "?DevPlayerId=<name>" for a "dev-<name>" DevPlayerId, so the next
	// server (match or hub) keeps the player id and their save. Url unchanged in Shipping, without a
	// development id, or when Url is empty.
	static FString WithDevPlayerId(const FString& Url, const FString& DevPlayerId);

	// After a successful extraction (the trip back to the hub: UpdateHubReturn).
	UPROPERTY(BlueprintAssignable, Category = "Crossguard")
	FCrossguardPlayerExtracted OnPlayerExtracted;

	// --- Back to the hub after the raid (docs/hub-and-menu.md, "Back to the hub"). Server. ---
	//
	// A team (one party key; solo: a team of one) whose members have all extracted or died for the raid
	// leaves this match server for its hub destination (GetHubDestination) while the other teams play on
	// (ReturnTeamToHub). DEV-ONLY (listen server): when the destination is the whole-server return, the
	// finished teams wait and, once nobody is left in the raid, the whole server travels back to the hub
	// map (ReturnToHub). Disconnected raiders held for a reconnect still count as in the raid, but only
	// for HeldPlayerReturnGraceSeconds.

	// DEV-ONLY (listen server): the hub map the whole server travels back to: "<HubMapName>?listen?Hub"
	// on a listen server, "<HubMapName>?Hub" otherwise. The -HubMap=<path> command line option overrides
	// it (as on the menu's game mode). Empty in development mode: no trip back (players stay).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Hub Return")
	FString HubMapName = TEXT("/Game/Maps/L_Hub");

	// Seconds from a team's (development: the last) player leaving the raid to the travel (HUD:
	// "Returning to the hub in N s"). Placeholder.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Hub Return", meta = (ClampMin = "0"))
	float HubReturnCountdownSeconds = 10.0f;

	// When only disconnected raiders (held for a reconnect) are left in the raid: seconds the server
	// waits for them before travelling anyway. A held character still in the raid then fails its
	// extraction (released as if it died while away). Placeholder.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Hub Return", meta = (ClampMin = "0"))
	float HeldPlayerReturnGraceSeconds = 120.0f;

	FString GetHubMapName() const;

	// DEV-ONLY (listen server): "<HubMap>?listen?Hub" (bListen) or "<HubMap>?Hub"; empty if HubMap is empty.
	static FString BuildHubReturnUrl(const FString& HubMap, bool bListen);

	// Where Player goes when their team is done, in order: the hub server the backend picks
	// (matchmaking; a stub until the backend exists), HubServerAddress / -HubServer=, in development
	// mode only (Crossguard::IsDevListenMode, the match server is the dev listen server) the whole
	// server's return to HubMapName, else the main menu. Player may be null (the server's default).
	FCrossguardHubDestination GetHubDestination(const APlayerController* Player) const;

	// DEV-ONLY (listen server): the whole server's countdown to the trip back runs.
	bool IsHubReturnPending() const { return bHubReturnPending; }
	// Seconds until the travel (0 when not pending).
	float GetHubReturnSecondsLeft() const;
	// The countdown waits for disconnected raiders only (HeldPlayerReturnGraceSeconds).
	bool IsHubReturnWaitingForHeldPlayers() const { return bHubReturnPending && bHubReturnWaitsForHeld; }
	// ReturnToHub ran: the URL the server travelled to (or, plan only, would have).
	bool HasHubReturnStarted() const { return bHubReturnStarted; }
	const FString& GetHubReturnUrl() const { return HubReturnUrl; }

	// DEV-ONLY (listen server). Server. The whole server's countdown's end: held raiders fail their
	// raid, everyone's save is written, absent players leave their parties, then the server travels to
	// the hub (unless plan only). Refused outside development mode.
	void ReturnToHub();

	// Tests: ReturnToHub does everything but the ServerTravel. During automation tests it is plan only
	// anyway unless bHubReturnTravelInTests is set (Net.SeamlessTravelHubToRaid travels for real).
	bool bHubReturnPlanOnly = false;
	bool bHubReturnTravelInTests = false;

	// --- A team going back (docs/hub-and-menu.md, "A team goes back"). Server. ---
	//
	// Ruled (owner): a finished team goes back without waiting for the teams still raiding (after its
	// own HubReturnCountdownSeconds, shown to that team only). DEV-ONLY (listen server): a team holding
	// the server's own player (listen host, standalone) never leaves early, and with the whole-server
	// return as the destination finished teams wait for it.

	// A dedicated hub server ("203.0.113.5:7777") finished teams travel to when the backend names none.
	// The -HubServer=<address> command line option overrides it.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Hub Return")
	FString HubServerAddress;

	FString GetHubServerAddress() const;

	// The team's countdown to its own trip back runs.
	bool IsTeamHubReturnPending(int32 TeamId) const { return TeamHubReturns.Contains(TeamId); }
	// Seconds until the team leaves (0 when not pending).
	float GetTeamHubReturnSecondsLeft(int32 TeamId) const;
	// The plan ReturnTeamToHub made for the team (followed, or plan only); null before.
	const TArray<FCrossguardTeamHubTravel>* GetTeamHubReturnPlan(int32 TeamId) const { return TeamHubPlans.Find(TeamId); }
	// The player is leaving with their team (ReturnTeamToHub): their connection closing is not a
	// disconnect (no hold for a reconnect), and they no longer count for this raid.
	bool IsLeavingForHub(const FString& PlayerId) const { return LeavingForHubIds.Contains(PlayerId); }
	// The player was sent to the main menu (ReturnToMenu).
	bool WasSentToMenu(const FString& PlayerId) const { return SentToMenuIds.Contains(PlayerId); }

	// Server. The team's countdown ended: its raiders away fail the raid, every member is saved and
	// marked as leaving, then each member's client travels to their hub destination, or goes to the
	// menu without one (unless plan only, bHubReturnPlanOnly). Does nothing while the destination is
	// the development whole-server return.
	void ReturnTeamToHub(int32 TeamId);

	// "<Address>?Party=<key>" (a hub server; no ?Party= for an empty key); empty if Address is empty or
	// holds URL separators, or the key is not URL-safe.
	static FString BuildTeamHubJoinUrl(const FString& Address, const FString& Party);

	virtual void StartPlay() override;

	// --- Match lifecycle (docs/dedicated-server-plan.md, C6). Server. ---
	//
	// Assumed (coordinator's decision, placeholder): one match per match-server process. The match is
	// over once every player has left the raid map's play (all teams gone to the hub or the menu) or
	// MaxRaidDurationSeconds ran out; then OnMatchOver fires and a dedicated server process exits
	// cleanly (after MatchOverExitDelaySeconds) so the fleet starts a fresh one. Never in development
	// listen mode (the whole server travels back instead), the editor, automation tests, or with
	// -KeepMatchServer on the command line (the local cluster).

	// Seconds a raid may last before it closes: raiders still in it fail it (Assumed: as a death for the
	// raid), held raiders are released, and every team goes back. Not in development listen mode.
	// Placeholder.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Match", meta = (ClampMin = "0"))
	float MaxRaidDurationSeconds = 3600.0f;

	// Seconds between the match being over and the process exiting (the last clients' travel).
	// Placeholder.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Match", meta = (ClampMin = "0"))
	float MatchOverExitDelaySeconds = 5.0f;

	bool IsMatchOver() const { return bMatchOver; }
	FCrossguardMatchOver OnMatchOver;

	// Server. The raid's time limit: closes the raid (see MaxRaidDurationSeconds).
	void CloseRaidForTime();

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
	// Open any map with ?Hub to play it as the hub (the main menu's Play adds it too).
	static const TCHAR* HubOption;

	// Server. This map plays as the hub (bIsRaid off): the URL Options carry ?Hub, or the world holds
	// an ACrossguardHubMarker (L_Hub does: docs/hub-and-menu.md).
	bool IsHubMap(const FString& Options) const;

	// --- Main menu (docs/hub-and-menu.md) ---

	// Where a player whose raid is over goes when there is no hub to go back to (no Hub Map Name), or a
	// team's trip back has nowhere to go: the main menu map (ACrossguardMenuGameMode). Empty, or a map
	// that does not exist: the engine's own "return to main menu" (the project's Game Default Map).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Menu")
	FString MenuMapName = TEXT("/Game/Maps/L_MainMenu");

	// Server. Sends Player's client to the main menu (MenuMapName); the menu shows the message for
	// ReasonKey (ACrossguardMenuGameMode::ReasonMessage, e.g. RaidOverReason).
	void ReturnToMenu(APlayerController* Player, const FString& ReasonKey);

	// Server. Moves the player onto the team of party key Party (empty: a team of their own), as if
	// they had logged in with ?Party=<Party>. No-op if that is their key already.
	void SetPlayerParty(AController* Controller, const FString& Party);

	// --- Raid launch from the hub (docs/parties.md). The real launch asks matchmaking
	// (UCrossguardMatchmakingSubsystem) for a match server. ---

	// DEV-ONLY (listen server): the raid map a party that is the whole server travels to (ServerTravel),
	// e.g. "/Game/Maps/L_Raid". The -RaidMap=<path> command line option overrides it. Empty: none.
	// Placeholder default: the dev sandbox (monsters, chests, gather nodes) until a raid map exists.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Raid Launch")
	FString RaidMapName = TEXT("/Game/Maps/Dev/L_DevSandbox");

	// Development matchmaking's fake assignment (FCrossguardDevMatchmaking; never in Shipping): a match
	// server ("203.0.113.5:7777") the party's clients travel to with ?Party=<key> while the rest of the
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

	// Re-targets every spectator whose teammate left the raid. With none left they wait for their
	// team's trip back (to the menu only without a hub map).
	void UpdateSpectators();
	// A teammate of Player still in the raid; null if none.
	APawn* FindSpectateTarget(const APlayerController* Player) const;

	Crossguard::FTeamRegistry Teams;
	TArray<TWeakObjectPtr<APlayerController>> Spectators;
	// Players who extracted from this raid: they spectate, but are never sent to the menu.
	TArray<TWeakObjectPtr<APlayerController>> ExtractedPlayers;
	// Players dead for the raid -> their body: watched once no teammate is left in the raid.
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<APawn>> DeadBodies;
	float RaidStartSeconds = 0.0f;
	// Controller -> registry key, so Logout can find the player.
	TMap<TWeakObjectPtr<const AController>, FString> PlayerKeys;

	// The stable stand-in id for a login without an online id (ACrossguardPlayerState::DevPlayerId);
	// empty in Shipping or when none applies.
	FString DevPlayerIdFor(const FString& Options) const;
	// The Nakama user id of this login: verified at PreLoginAsync for a remote player; the server's own
	// logged-in player (listen host, standalone) for itself. Empty if none. OutError: the login must be
	// refused (a Nakama login is required and missing).
	FString NakamaUserIdFor(const FString& Options, FString& OutError) const;
	// A player state here, not held for a reconnect and not Except, whose PlayerIdOf is PlayerId.
	const ACrossguardPlayerState* FindOnlinePlayer(const FString& PlayerId, const APlayerState* Except) const;
	// The id PlayerState is held under; empty if it is not held.
	FString HeldIdOf(const ACrossguardPlayerState* PlayerState) const;
	// The reconnecting controller takes over the held player state (before Super::InitNewPlayer).
	FString RestoreHeldPlayer(APlayerController* Controller, const FString& PlayerId, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal);
	// The held player's character died for the raid (or bRaidOver: the server returns to the hub
	// while they are away): their raid fails and the player state goes.
	void ReleaseHeldPlayer(const FString& PlayerId, bool bRaidOver = false);
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

	// Marks the match over when nobody is left in its play (see IsMatchOver), then requests the exit.
	void CheckMatchOver();
	// A dedicated match server that may exit when its match is over.
	bool ShouldExitWhenMatchOver() const;
	void ExitForMatchOver();
	FTimerHandle MatchTimeLimitTimer;
	FTimerHandle MatchOverExitTimer;
	bool bMatchOver = false;

	// Players finished with the raid go somewhere: false only in development mode with no hub map and
	// no hub server (they stay, as before; the dead with nobody to watch go to the menu).
	bool HasTripBack() const;
	// After anyone's raid state changes (extraction, death for the raid, a held player's release, a
	// logout, a reconnect, a new raider): starts, shortens or cancels the countdown to the hub.
	void UpdateHubReturn();
	// Every HubReturnTickSeconds while pending: the HUD line, and ReturnToHub when time is up.
	void HubReturnTick();
	// Players sent to the menu (ReturnToMenu): leaving, so they keep no raid going and do not count as
	// travellers.
	TSet<FString> SentToMenuIds;
	FTimerHandle HubReturnTimer;

	// Teams counting down (team id), the plans made, the players leaving with their team (player id).
	TMap<int32, FCrossguardTeamHubCountdown> TeamHubReturns;
	TMap<int32, TArray<FCrossguardTeamHubTravel>> TeamHubPlans;
	TSet<FString> LeavingForHubIds;
	// DEV-ONLY (listen server): finished teams waiting for the whole server's return: told once.
	TSet<int32> TeamsWaitingForServer;
	FTimerHandle TeamHubReturnTimer;
	// Every HubReturnTickSeconds while a team counts down: its HUD line, and ReturnTeamToHub on time.
	void TeamHubReturnTick();
	// Calls off a team's countdown (bTell: its members see it).
	void CancelTeamHubReturn(int32 TeamId, bool bTell);
	// The HUD line for every member of the team here.
	void TellTeam(int32 TeamId, const FString& Message) const;
	// This server process's own player (listen-server host, standalone): it has a ULocalPlayer.
	static bool IsServerOwnPlayer(const APlayerController* Player);
	// World time of the travel.
	double HubReturnAt = 0.0;
	// The last whole second shown on the HUD.
	int32 HubReturnShownSeconds = -1;
	bool bHubReturnPending = false;
	bool bHubReturnWaitsForHeld = false;
	bool bHubReturnStarted = false;
	FString HubReturnUrl;
