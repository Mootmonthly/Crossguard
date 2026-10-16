#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Save.h"
#include "Game/CrossguardSaveBackend.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"

#include "CrossguardSaveSubsystem.generated.h"

class ACrossguardPlayerState;
class APawn;
class UCrossguardSaveSubsystem;

// Server: one per online player. Forwards their state changes (progression, stash, inventory,
// equipment, loadout) to the subsystem's debounced save.
UCLASS()
class CROSSGUARD_API UCrossguardSaveWatcher : public UObject
{
	GENERATED_BODY()

public:
	void Watch(UCrossguardSaveSubsystem* InSubsystem, ACrossguardPlayerState* InPlayerState);

	UFUNCTION()
	void HandleChange();

private:
	TWeakObjectPtr<UCrossguardSaveSubsystem> Subsystem;
	TWeakObjectPtr<ACrossguardPlayerState> PlayerState;
	TWeakObjectPtr<APawn> WatchedPawn;
};

// Where a player's save stands. For tests and logs.
UENUM(BlueprintType)
enum class ECrossguardSaveState : uint8
{
	// Saving is off, or this player is not tracked (client, never joined).
	None,
	// Waiting for the backend.
	Loading,
	// Loaded (or a new character); applied to the player once their pawn is ready. Saves are allowed
	// from then on.
	Ready,
	// The backend failed: this player is not saved this session, so their stored save is kept.
	Failed,
};

// Server: loads each character when the player joins and saves it when it matters
// (docs/persistence.md). Owns the save backend (ICrossguardSaveBackend; local JSON files by default).
//
// - Load: when the player state gets its unique id (ACrossguardPlayerState::OnSetUniqueId). The
//   record is applied once the pawn is possessed (ACrossguardGameMode::HandleStartingNewPlayer),
//   through the components' server functions, after validation (CrossguardRules ValidateCharacterSave).
// - Save: when a raid starts and ends (extraction or failure), on logout, and a short while after any
//   change between raids (debounced, SaveDebounceSeconds). Never during a raid: a raid save records
//   no carried gear, because that gear is lost unless the player extracts. A raider whose connection
//   drops is held for a reconnect (ACrossguardGameMode::HoldForReconnect): no logout save and the
//   entry is kept; the raid's end (extraction or failure) saves as usual.
// - The player id is UCrossguardGuildSubsystem::PlayerIdOf: the online id (Steam), else in
//   development builds a server-set stand-in ("pie-<n>", "dev-<name>"), else
//   "session-<PlayerId>", which is NOT stable across server runs (docs/steam-setup.md).
//
// Off while automation tests run, so tests never read or write real saves; a test turns it on with
// EnableForTesting and a temporary folder.
UCLASS(Config = Game)
class CROSSGUARD_API UCrossguardSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Null without a game instance.
	static UCrossguardSaveSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Settings (DefaultGame.ini, [/Script/Crossguard.CrossguardSaveSubsystem]) ---------------

	UPROPERTY(Config)
	bool bSavingEnabled = true;

	// Players without an online id get "session-<n>" ids that another player may get after a server
	// restart. On for development; turn off once Steam ids exist.
	UPROPERTY(Config)
	bool bSaveSessionIds = true;

	// Seconds between a change between raids and its save; more changes in that time share one save.
	UPROPERTY(Config)
	float SaveDebounceSeconds = 2.0f;

	// Where saves go: "Local" (files, the default for development, PIE and tests) or "Nakama"
	// (dedicated servers; needs -NakamaUrl= and -NakamaHttpKey=, docs/backend-setup.md). The command
	// line -SaveBackend=<name> overrides it.
	UPROPERTY(Config)
	FString SaveBackend = TEXT("Local");

	// Shutdown waits this long at most for the backend to take the last saves (Nakama).
	UPROPERTY(Config)
	float ShutdownFlushSeconds = 10.0f;

	// --- Hooks (server; each does nothing for clients or while saving is off) -------------------

	// The player's unique id is set: starts loading their save. A second player state claiming the
	// id of a player who is online (a duplicate login, refused by the game mode) is ignored here, in
	// PlayerReady and in PlayerLeaving: the online player keeps their entry.
	void PlayerJoined(ACrossguardPlayerState* PlayerState);
	// Their pawn is possessed: applies the loaded save (or, still loading, as soon as it arrives).
	void PlayerReady(ACrossguardPlayerState* PlayerState);
	// Saves now (raid start and end).
	void SaveNow(ACrossguardPlayerState* PlayerState);
	// Logout: a last save, then forgets the player.
	void PlayerLeaving(ACrossguardPlayerState* PlayerState);
	// Something changed: saves after SaveDebounceSeconds. Ignored during a raid.
	void MarkDirty(ACrossguardPlayerState* PlayerState);
	// Writes every debounced save now.
	void FlushPendingSaves();

	// --- State --------------------------------------------------------------------------------

	bool IsSavingEnabled() const;
	ECrossguardSaveState GetSaveState(const ACrossguardPlayerState* PlayerState) const;
	// While a save is being applied (its own changes are not new saves).
	bool IsApplying() const { return bApplying; }

	// The record SaveNow would write for this player now. False if the player is not tracked.
	bool BuildRecord(ACrossguardPlayerState* PlayerState, Crossguard::FCharacterSave& OutRecord) const;

	// --- Backend ------------------------------------------------------------------------------

	ICrossguardSaveBackend* GetBackend() const;
	// Must implement ICrossguardSaveBackend (ignored otherwise). Set it before players join.
	void SetBackend(UObject* NewBackend);

	// --- Tests ----------------------------------------------------------------------------------

	// Turns saving on during automation tests, for session ids too, with a fresh local backend
	// writing to Directory.
	void EnableForTesting(const FString& Directory);
	// Back to off; drops every pending save and cached record.
	void DisableForTesting();
	// Loads the player's save from the backend again (ignoring the cached record) and applies it.
	// False if the load was refused or failed; the player is unchanged then.
	bool ReloadPlayer(ACrossguardPlayerState* PlayerState);

private:
	struct FPlayerEntry
	{
		ECrossguardSaveState State = ECrossguardSaveState::None;
		TWeakObjectPtr<ACrossguardPlayerState> PlayerState;
		// The player state the save was applied to; saving waits for this.
		TWeakObjectPtr<ACrossguardPlayerState> AppliedTo;
		// Waiting for PlayerReady (loaded before the pawn existed).
		bool bHasPending = false;
		Crossguard::FCharacterSave Pending;
		// The latest record: what was loaded or last built. Holds what the live game does not
		// (quarantine), and the carried gear while the player has no pawn.
		bool bHasLast = false;
		Crossguard::FCharacterSave Last;
		// A debounced save is waiting.
		bool bDirty = false;
		// PlayerReady happened: the pawn is there.
		bool bReady = false;
		// The last load found a usable save, or none (a new character); false if it was refused or failed.
		bool bLastLoadAccepted = false;
	};

	FString PlayerIdFor(const ACrossguardPlayerState* PlayerState) const;
	bool CanSave(const ACrossguardPlayerState* PlayerState, const FPlayerEntry*& OutEntry) const;
	void StartLoad(const FString& PlayerId, bool bForceReload);
	// Backend answered. Returns false when the document was refused or the backend failed.
	bool HandleLoaded(const FString& PlayerId, const FCrossguardSaveLoadResult& Result);
	void Apply(FPlayerEntry& Entry, ACrossguardPlayerState& PlayerState, Crossguard::FCharacterSave Record);
	void Write(const FString& PlayerId, FPlayerEntry& Entry);
	// Writes every debounced save (no timer access: also used while shutting down).
	void WriteDirty();
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	UPROPERTY()
	TObjectPtr<UObject> BackendObject;

	UPROPERTY()
	TMap<FString, TObjectPtr<UCrossguardSaveWatcher>> Watchers;

	TMap<FString, FPlayerEntry> Players;
	FTimerHandle FlushTimer;
	FDelegateHandle WorldCleanupHandle;
	bool bApplying = false;
	bool bEnabledForTesting = false;
};
