#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Game/CrossguardSaveBackend.h"
#include "UObject/Object.h"

#include "CrossguardNakamaSaveBackend.generated.h"

class FCrossguardNakamaClient;
class UCrossguardLocalSaveBackend;
struct FCrossguardNakamaResponse;

// Character saves in Nakama (docs/persistence.md, "Nakama backend"), for dedicated servers:
// -SaveBackend=Nakama -NakamaUrl=<url> -NakamaHttpKey=<key> (UCrossguardSaveSubsystem picks it).
//
// - The player id must be a Nakama user id (a verified ?NakamaToken= login): the save is the storage
//   object characters/<CharacterId> owned by that user, read and written only through the server RPCs
//   crossguard_save_load / crossguard_save_write (Backend/nakama/modules/saves.go).
// - Versions: the load remembers the storage version; every write sends it as expected_version ("*"
//   for a new character) and takes the new one back. If another server wrote in between, Nakama refuses
//   (version conflict, HTTP 409): this server stops writing that player (until they load again), and
//   the refused document is kept, in Nakama (characters_conflicts) and on this disk.
// - One write per player in flight, in call order; saves made meanwhile are merged into the next write
//   (the newest document carries everything; every waiting callback gets that write's result).
// - A document equal to the newest one this server wrote or is writing for the player, but for "savedAt", is not
//   written again (its callback joins that write). A travel saves before leaving and again at logout; the second,
//   unchanged write would move the version under the server the player just joined, which then could
//   not save (version conflict).
// - Retries: no answer, 5xx or 429 are retried with backoff (1, 2, 4, 8, 16 s); then, and on any other
//   refusal, the document is kept in Saved/Crossguard/NakamaUnsaved/<id>.json and logged as an error.
//   A save is never dropped silently.
// - Shutdown: FlushWrites sends what is waiting and blocks (bounded) until Nakama answers, then releases
//   every lease.
// - Save leases (Backend/nakama/modules/leases.go): only one server owns a player's save at a time. A
//   load first acquires the player's lease for this server (GetServerId); held by another server (the
//   one the player just left, still writing), it retries every LeaseRetrySeconds for up to
//   LeaseWaitSeconds (a crashed server's lease expires after LeaseTtlSeconds and is taken over). Every
//   write carries the token; leases are renewed every LeaseRenewSeconds; ReleaseCharacter (logout, travel)
//   releases once the player's writes are done. A lease lost (expired and taken, refused renewal or
//   write) stops writes for that player like a version conflict (kept on disk).
// - Guild bank operations (SendWithSave, docs/guild-rules.md "On Nakama") carry the player's save: Nakama
//   writes the guild and the save together or not at all. They queue with the player's writes; saves
//   asked for while one is on its way are held until it is answered (then written after it, or dropped
//   if the save was not written: they hold a change that did not happen).
UCLASS()
class CROSSGUARD_API UCrossguardNakamaSaveBackend : public UObject, public ICrossguardSaveBackend
{
	GENERATED_BODY()

public:
	// Same cap as the local backend and the Nakama module (MaxRecordBytes).
	static constexpr int64 MaxDocumentBytes = 8 * 1024 * 1024;
	// Attempts per write (1 + retries) and per load (the player is waiting).
	static constexpr int32 MaxWriteAttempts = 6;
	static constexpr int32 MaxLoadAttempts = 3;
	// One character per account until character select exists (placeholder).
	static const TCHAR* DefaultCharacterId;
	// Save lease timing (placeholders): TTL asked for, renew interval, how long a load waits for another
	// server's lease (a clean hand-off takes about a second; a crashed server's lease runs out after the
	// TTL), and the pause between tries.
	static constexpr int32 LeaseTtlSeconds = 60;
	static constexpr float LeaseRenewSeconds = 20.0f;
	static constexpr float LeaseWaitSeconds = 75.0f;
	static constexpr float LeaseRetrySeconds = 1.0f;

	// Required before use. Empty Url or HttpKey: every call fails (logged). ServerId: this process's id
	// (UCrossguardNakamaSubsystem::GetServerId), the lease holder.
	void Configure(const FString& Url, const FString& InHttpKey, float TimeoutSeconds, const FString& InServerId);

	// ICrossguardSaveBackend
	virtual void LoadCharacter(const FString& PlayerId, FLoadCallback OnLoaded) override;
	virtual void SaveCharacter(const FString& PlayerId, const FString& Document, FSaveCallback OnSaved) override;
	virtual void QuarantineCharacter(const FString& PlayerId, const FString& Reason) override;
	virtual void ReleaseCharacter(const FString& PlayerId) override;
	virtual FString DescribeBackend() const override;
	virtual void FlushWrites(float TimeoutSeconds) override;

	// --- Guild bank operations ---

	// Response: Nakama's last answer. bSaveWritten: the answer carried the save's new version (the bank
	// change and the save were written; for a rule refusal or a failure, nothing was).
	using FJointCallback = TFunction<void(const FCrossguardNakamaResponse& Response, bool bSaveWritten)>;
	// Sends RpcId (crossguard_guild) with the player's save Document: MakePayload gets the "save" member
	// (BuildSaveMember: the JSON object with the version and lease token current when it is sent) and
	// returns the whole payload; it is built once, so retries (no answer, 5xx) send the same request id.
	// Waits for a write on its way; saves asked for meanwhile wait for it (held). One per player at a
	// time. False (OutError says why, nothing sent) without a lease, a loaded version, after a conflict,
	// or while another one is pending.
	bool SendWithSave(const FString& PlayerId, const FString& Document, const FString& RpcId, TFunction<FString(const FString& SaveMember)> MakePayload,
		FJointCallback OnDone, FString& OutError);
	// A guild bank operation of the player is waiting or on its way.
	bool HasPendingJoint(const FString& PlayerId) const;
	// {"user_id":..,"character_id":..,"record":<Document as is>,"expected_version":..,"lease_token":..}
	static FString BuildSaveMember(const FString& UserId, const FString& CharacterId, const FString& Document, const FString& ExpectedVersion,
		const FString& LeaseToken);
	// The "save_version" of a crossguard_guild answer; empty if none (nothing was written).
	static FString ReadSaveVersion(const FString& Body);

	// The storage version known for a player ("*": no save stored yet; empty: not loaded here).
	FString GetKnownVersion(const FString& PlayerId) const;
	// This server holds the player's save lease.
	bool HoldsLease(const FString& PlayerId) const;
	// Where documents Nakama did not take are kept.
	FString GetFallbackDirectory() const;

	// --- Pure helpers (tested in CrossguardNakamaTests.cpp) ---

	// The crossguard_save_write payload; Document (a JSON object) is embedded as is, not re-encoded.
	static FString BuildWritePayload(const FString& UserId, const FString& CharacterId, const FString& Document, const FString& ExpectedVersion,
		const FString& LeaseToken);
	// The crossguard_lease_acquire / _renew / _release payload (LeaseToken empty for acquire).
	static FString BuildLeasePayload(const FString& UserId, const FString& CharacterId, const FString& InServerId, const FString& LeaseToken, int32 TtlSeconds);
	// A refusal that means the lease is gone (FAILED_PRECONDITION, "save lease not held").
	static bool IsLeaseLost(const FCrossguardNakamaResponse& Response);
	// Reads a crossguard_save_load answer: {"found":bool,"version":"...","record":<object or null>}, record
	// last (the module's field order). OutRecord is the record's exact text (numbers untouched).
	static bool ParseLoadResponse(const FString& Body, bool& bOutFound, FString& OutVersion, FString& OutRecord);
	// Seconds before retry number Attempt (1-based): 1, 2, 4, 8, 16, capped at 30.
	static float RetryDelaySeconds(int32 Attempt);
	// The document without its "savedAt" member's value (for "nothing changed" checks).
	static FString WithoutSavedAt(const FString& Document);

private:
	struct FPlayerSaves
	{
		// Storage version: "*" = nothing stored yet, empty = never loaded here.
		FString Version;
		// WithoutSavedAt of the last document stored at Version by this server; empty after a load.
		FString LastWritten;
		// A write was refused for a stale version: no more writes until the player loads again.
		bool bConflict = false;
		// The write on its way to Nakama, and who waits for it.
		bool bInFlight = false;
		FString InFlightDocument;
		TArray<FSaveCallback> InFlightCallbacks;
		int32 Attempt = 0;
		// The next write (newest document) and who waits for it.
		bool bHasQueued = false;
		FString QueuedDocument;
		TArray<FSaveCallback> QueuedCallbacks;
		// A retry is scheduled.
		FTSTicker::FDelegateHandle RetryHandle;
		// Loads asked for while writes were pending: run once they are done (a load must see them).
		TArray<FLoadCallback> DeferredLoads;
		// The save lease: token from crossguard_lease_acquire (empty: none held).
		FString LeaseToken;
		// Released once idle (ReleaseCharacter), and the release on its way.
		bool bReleaseWhenIdle = false;
		bool bReleaseInFlight = false;
		// A guild bank operation carrying the save (SendWithSave): waiting to go (also between retries), or
		// on its way. Payload built when first sent.
		bool bJointWaiting = false;
		bool bJointInFlight = false;
		FString JointRpc;
		FString JointDocument;
		FString JointPayload;
		TFunction<FString(const FString&)> JointMakePayload;
		FJointCallback JointCallback;
		int32 JointAttempt = 0;
		// Saves asked for while it was pending (the newest document, and who waits for it).
		bool bHasHeld = false;
		FString HeldDocument;
		TArray<FSaveCallback> HeldCallbacks;

		bool IsJointPending() const { return bJointWaiting || bJointInFlight; }
		bool IsBusy() const { return bInFlight || bHasQueued || RetryHandle.IsValid() || bReleaseInFlight || IsJointPending() || bHasHeld; }
	};

	// Acquires the lease (retrying while another server holds it), then loads.
	void StartAcquire(const FString& PlayerId, double WaitUntil, FLoadCallback OnLoaded);
	void HandleAcquire(const FString& PlayerId, double WaitUntil, FLoadCallback OnLoaded, const FCrossguardNakamaResponse& Response);
	void StartLoad(const FString& PlayerId, int32 Attempt, FLoadCallback OnLoaded);
	void HandleLoad(const FString& PlayerId, int32 Attempt, FLoadCallback OnLoaded, const FCrossguardNakamaResponse& Response);
	// Sends the queued write if nothing is in flight; runs deferred loads when idle.
	void Pump(const FString& PlayerId);
	void Send(const FString& PlayerId, FPlayerSaves& Entry);
	void HandleWrite(const FString& PlayerId, const FCrossguardNakamaResponse& Response);
	// The in-flight document did not reach Nakama: keep it on disk, tell its callbacks.
	void GiveUpInFlight(const FString& PlayerId, FPlayerSaves& Entry, const FString& Reason);
	// Writes Document to the fallback folder. Returns the file (empty if even that failed).
	FString KeepLocally(const FString& PlayerId, const FString& Document, const FString& Reason);
	bool CanCall(FString& OutError) const;
	// Guild bank operations: send (Payload built the first time), answer, and the end of one.
	void SendJoint(const FString& PlayerId, FPlayerSaves& Entry);
	void HandleJoint(const FString& PlayerId, const FCrossguardNakamaResponse& Response);
	void FinishJoint(const FString& PlayerId, FPlayerSaves& Entry, const FCrossguardNakamaResponse& Response, bool bSaveWritten);
	// The player lost the lease: no more writes until they load again (logged).
	void LoseLease(const FString& PlayerId, FPlayerSaves& Entry, const FString& Why);
	void SendRelease(const FString& PlayerId, FPlayerSaves& Entry);
	bool RenewTick(float DeltaSeconds);

	TSharedPtr<FCrossguardNakamaClient> Client;
	FString HttpKey;
	FString CharacterId;
	FString ServerId;
	TMap<FString, FPlayerSaves> Players;
	FTSTicker::FDelegateHandle RenewHandle;
	// Shutting down: no backoff waits, failures go to disk at once.
	bool bFlushing = false;

	UPROPERTY()
	TObjectPtr<UCrossguardLocalSaveBackend> Fallback;
};
