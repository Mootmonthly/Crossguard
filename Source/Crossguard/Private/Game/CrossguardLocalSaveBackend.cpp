#include "Game/CrossguardLocalSaveBackend.h"

#include "Crossguard.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

FString UCrossguardLocalSaveBackend::GetDirectory() const
{
	return DirectoryOverride.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crossguard"), TEXT("Characters")) : DirectoryOverride;
}

FString UCrossguardLocalSaveBackend::FileNameFor(const FString& PlayerId)
{
	constexpr int32 MaxReadableLength = 64;
	FString Safe;
	bool bChanged = PlayerId.IsEmpty() || PlayerId.Len() > MaxReadableLength;
	const int32 Length = FMath::Min(PlayerId.Len(), MaxReadableLength);
	for (int32 Index = 0; Index < Length; ++Index)
	{
		const TCHAR Char = PlayerId[Index];
		const bool bAllowed = (Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('A') && Char <= TEXT('Z'))
			|| (Char >= TEXT('0') && Char <= TEXT('9')) || Char == TEXT('-') || Char == TEXT('_');
		Safe.AppendChar(bAllowed ? Char : TEXT('_'));
		bChanged = bChanged || !bAllowed;
	}
	if (bChanged)
	{
		Safe += FString::Printf(TEXT("-%08x"), FCrc::StrCrc32(*PlayerId));
	}
	return Safe + TEXT(".json");
}

FString UCrossguardLocalSaveBackend::GetFilePath(const FString& PlayerId) const
{
	return FPaths::Combine(GetDirectory(), FileNameFor(PlayerId));
}

FString UCrossguardLocalSaveBackend::DescribeBackend() const
{
	return FString::Printf(TEXT("local files in %s"), *FPaths::ConvertRelativePathToFull(GetDirectory()));
}

void UCrossguardLocalSaveBackend::LoadCharacter(const FString& PlayerId, FLoadCallback OnLoaded)
{
	FCrossguardSaveLoadResult Result;
	const FString Path = GetFilePath(PlayerId);
	const int64 Size = IFileManager::Get().FileSize(*Path);
	if (Size < 0)
	{
		Result.Status = ECrossguardSaveLoadStatus::NotFound;
	}
	else if (Size > MaxFileBytes)
	{
		Result.Status = ECrossguardSaveLoadStatus::Corrupt;
		Result.Error = FString::Printf(TEXT("%s is %lld bytes"), *Path, Size);
	}
	else if (FFileHelper::LoadFileToString(Result.Document, *Path))
	{
		Result.Status = ECrossguardSaveLoadStatus::Found;
	}
	else
	{
		// The file exists but could not be read (locked, permissions): maybe only for now.
		Result.Status = ECrossguardSaveLoadStatus::Failed;
		Result.Error = FString::Printf(TEXT("could not read %s"), *Path);
	}
	if (OnLoaded)
	{
		OnLoaded(Result);
	}
}

void UCrossguardLocalSaveBackend::SaveCharacter(const FString& PlayerId, const FString& Document, FSaveCallback OnSaved)
{
	const FString Path = GetFilePath(PlayerId);
	const FString TempPath = Path + TEXT(".tmp");
	IFileManager& Files = IFileManager::Get();
	FString Error;
	if (!Files.MakeDirectory(*GetDirectory(), true))
	{
		Error = FString::Printf(TEXT("could not create %s"), *GetDirectory());
	}
	else if (!FFileHelper::SaveStringToFile(Document, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Error = FString::Printf(TEXT("could not write %s"), *TempPath);
	}
	else if (!Files.Move(*Path, *TempPath, true, true))
	{
		Error = FString::Printf(TEXT("could not replace %s"), *Path);
		Files.Delete(*TempPath, false, true, true);
	}
	if (OnSaved)
	{
		OnSaved(Error.IsEmpty(), Error);
	}
}

void UCrossguardLocalSaveBackend::QuarantineCharacter(const FString& PlayerId, const FString& Reason)
{
	const FString Path = GetFilePath(PlayerId);
	if (IFileManager::Get().FileSize(*Path) < 0)
	{
		return;
	}
	// Time plus a random tag: two refusals within a second never collide.
	const FString Aside = FPaths::ChangeExtension(Path, FString::Printf(TEXT("corrupt-%s-%s.json"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
	if (IFileManager::Get().Move(*Aside, *Path, false, true))
	{
		UE_LOG(LogCrossguard, Warning, TEXT("Save for %s set aside as %s (%s)"), *PlayerId, *Aside, *Reason);
	}
	else
	{
		UE_LOG(LogCrossguard, Error, TEXT("Could not set aside the refused save %s (%s); the next save replaces it"), *Path, *Reason);
	}
}
