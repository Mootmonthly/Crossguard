#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/Interface.h"

#include "CrossguardSaveBackend.generated.h"

// Where character saves live (docs/persistence.md). A backend only stores and fetches one opaque
// document per player id: the format, versioning and validation belong to CrossguardRules
// (CrossguardRules/Save.h) and UCrossguardSaveSubsystem, so swapping the backend (local files today,
// a database or platform cloud later) changes nothing else.
//
// Backends: UCrossguardLocalSaveBackend (default), UCrossguardNakamaSaveBackend (-SaveBackend=Nakama).
//
// Only the server talks to a backend. Callbacks must run on the game thread; they may run before the
// call returns (the local backend is synchronous). Saves for one player id must be stored in the
// order they are made.

enum class ECrossguardSaveLoadStatus : uint8
{
	// Document holds the stored save.
	Found,
	// No save yet: a new character.
	NotFound,
	// Something is stored but cannot be a save (unreadable, far too large). The subsystem quarantines
	// it and starts the player fresh.
	Corrupt,
	// The backend could not answer (offline, I/O error). The subsystem keeps the player from saving
	// this session, so a good save is never overwritten with a fresh character.
	Failed,
};

struct FCrossguardSaveLoadResult
{
	ECrossguardSaveLoadStatus Status = ECrossguardSaveLoadStatus::Failed;
	FString Document;
	// Corrupt / Failed: what went wrong, for the log.
	FString Error;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCrossguardSaveBackend : public UInterface
{
	GENERATED_BODY()
};

class CROSSGUARD_API ICrossguardSaveBackend
{
	GENERATED_BODY()

public:
	using FLoadCallback = TFunction<void(const FCrossguardSaveLoadResult& Result)>;
	using FSaveCallback = TFunction<void(bool bSaved, const FString& Error)>;

	virtual void LoadCharacter(const FString& PlayerId, FLoadCallback OnLoaded) = 0;
	// Replaces the stored document. OnSaved may be empty.
	virtual void SaveCharacter(const FString& PlayerId, const FString& Document, FSaveCallback OnSaved) = 0;
	// The stored document was refused on load: keep it aside for support (never delete it); the
	// next SaveCharacter writes a fresh one.
	virtual void QuarantineCharacter(const FString& PlayerId, const FString& Reason) = 0;
	// The player left this server (logout, travel): once their last save is stored, another server may
	// take over their save (Nakama: the save lease is released). Backends without leases: nothing to do.
	virtual void ReleaseCharacter(const FString& PlayerId) {}
	// For logs: "local files in <dir>".
	virtual FString DescribeBackend() const = 0;
	// Shutdown: sends every waiting write and blocks until answered or TimeoutSeconds pass. Writes that
	// did not make it must be kept somewhere (never dropped silently). Synchronous backends: nothing to do.
	virtual void FlushWrites(float TimeoutSeconds) {}
};
