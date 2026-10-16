#pragma once

#include "CoreMinimal.h"
#include "Game/CrossguardSaveBackend.h"
#include "UObject/Object.h"

#include "CrossguardLocalSaveBackend.generated.h"

// The default save backend: one JSON file per character on the server's disk,
// <Project>/Saved/Crossguard/Characters/<PlayerId>.json (docs/persistence.md). Good for development
// and a single server; a game with several servers needs a shared backend instead.
//
// Writes go to a temporary file first and then replace the save, so a crash mid-write leaves the
// previous save intact. A refused save is renamed to <PlayerId>.corrupt-<UTC time>-<tag>.json and kept.
// Synchronous: callbacks run before the calls return.
UCLASS()
class CROSSGUARD_API UCrossguardLocalSaveBackend : public UObject, public ICrossguardSaveBackend
{
	GENERATED_BODY()

public:
	// Larger files are refused as corrupt (a real save is a few hundred KB at most).
	static constexpr int64 MaxFileBytes = 8 * 1024 * 1024;

	// The folder saves go in: the override if set, else Saved/Crossguard/Characters.
	FString GetDirectory() const;
	// Tests point this at a temporary folder. Empty: back to the default.
	void SetDirectory(const FString& Directory) { DirectoryOverride = Directory; }

	// The file for a player id inside GetDirectory().
	FString GetFilePath(const FString& PlayerId) const;
	// A file name safe on every platform: ASCII letters, digits, '-' and '_' kept, anything else
	// replaced by '_'; when anything was replaced (or the id is empty or very long) a hash of the
	// whole id is appended, so different ids never share a file.
	static FString FileNameFor(const FString& PlayerId);

	// ICrossguardSaveBackend
	virtual void LoadCharacter(const FString& PlayerId, FLoadCallback OnLoaded) override;
	virtual void SaveCharacter(const FString& PlayerId, const FString& Document, FSaveCallback OnSaved) override;
	virtual void QuarantineCharacter(const FString& PlayerId, const FString& Reason) override;
	virtual FString DescribeBackend() const override;

private:
	FString DirectoryOverride;
};
