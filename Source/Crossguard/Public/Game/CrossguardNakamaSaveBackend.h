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
// - Shutdown: FlushWrites sends what is waiting and blocks (bounded) until Nakama answers.
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

	// Required before use. Empty Url or HttpKey: every call fails (logged).
	void Configure(const FString& Url, const FString& InHttpKey, float TimeoutSeconds);

	// ICrossguardSaveBackend
	virtual void LoadCharacter(const FString& PlayerId, FLoadCallback OnLoaded) override;
	virtual void SaveCharacter(const FString& PlayerId, const FString& Document, FSaveCallback OnSaved) override;
	virtual void QuarantineCharacter(const FString& PlayerId, const FString& Reason) override;
	virtual FString DescribeBackend() const override;
	virtual void FlushWrites(float TimeoutSeconds) override;

	// The storage version known for a player ("*": no save stored yet; empty: not loaded here).
	FString GetKnownVersion(const FString& PlayerId) const;
	// Where documents Nakama did not take are kept.
	FString GetFallbackDirectory() const;

	// --- Pure helpers (tested in CrossguardNakamaTests.cpp) ---

	// The crossguard_save_write payload; Document (a JSON object) is embedded as is, not re-encoded.
	static FString BuildWritePayload(const FString& UserId, const FString& CharacterId, const FString& Document, const FString& ExpectedVersion);
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

		bool IsBusy() const { return bInFlight || bHasQueued || RetryHandle.IsValid(); }
	};

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

	TSharedPtr<FCrossguardNakamaClient> Client;
	FString HttpKey;
	FString CharacterId;
	TMap<FString, FPlayerSaves> Players;
	// Shutting down: no backoff waits, failures go to disk at once.
	bool bFlushing = false;

	UPROPERTY()
	TObjectPtr<UCrossguardLocalSaveBackend> Fallback;
};
