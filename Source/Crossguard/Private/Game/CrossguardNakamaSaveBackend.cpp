#include "Game/CrossguardNakamaSaveBackend.h"

#include "Crossguard.h"
#include "Dom/JsonObject.h"
#include "Game/CrossguardLocalSaveBackend.h"
#include "Game/CrossguardNakamaClient.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CrossguardNakamaSaveLocal
{
	const TCHAR* LoadRpc = TEXT("crossguard_save_load");
	const TCHAR* WriteRpc = TEXT("crossguard_save_write");
	const TCHAR* QuarantineRpc = TEXT("crossguard_save_quarantine");

	void Report(TArray<ICrossguardSaveBackend::FSaveCallback>& Callbacks, bool bSaved, const FString& Error)
	{
		TArray<ICrossguardSaveBackend::FSaveCallback> Waiting = MoveTemp(Callbacks);
		Callbacks.Reset();
		for (ICrossguardSaveBackend::FSaveCallback& Callback : Waiting)
		{
			if (Callback)
			{
				Callback(bSaved, Error);
			}
		}
	}
}

const TCHAR* UCrossguardNakamaSaveBackend::DefaultCharacterId = TEXT("main");

void UCrossguardNakamaSaveBackend::Configure(const FString& Url, const FString& InHttpKey, float TimeoutSeconds)
{
	Client = Url.IsEmpty() ? nullptr : MakeShared<FCrossguardNakamaClient>(Url, TimeoutSeconds);
	HttpKey = InHttpKey;
	CharacterId = DefaultCharacterId;
	Fallback = NewObject<UCrossguardLocalSaveBackend>(this);
	Fallback->SetDirectory(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crossguard"), TEXT("NakamaUnsaved")));
}

FString UCrossguardNakamaSaveBackend::DescribeBackend() const
{
	return FString::Printf(TEXT("Nakama at %s (character \"%s\"; unsaved documents kept in %s)"), Client.IsValid() ? *Client->GetBaseUrl() : TEXT("<no url>"),
		*CharacterId, *GetFallbackDirectory());
}

FString UCrossguardNakamaSaveBackend::GetFallbackDirectory() const
{
	return Fallback != nullptr ? FPaths::ConvertRelativePathToFull(Fallback->GetDirectory()) : FString();
}

FString UCrossguardNakamaSaveBackend::GetKnownVersion(const FString& PlayerId) const
{
	const FPlayerSaves* Entry = Players.Find(PlayerId);
	return Entry != nullptr ? Entry->Version : FString();
}

bool UCrossguardNakamaSaveBackend::CanCall(FString& OutError) const
{
	if (!Client.IsValid() || HttpKey.IsEmpty())
	{
		OutError = TEXT("Nakama is not configured (-NakamaUrl= and -NakamaHttpKey= / CROSSGUARD_NAKAMA_HTTP_KEY)");
		return false;
	}
	return true;
}

// --- Helpers -------------------------------------------------------------------------------------

FString UCrossguardNakamaSaveBackend::BuildWritePayload(const FString& UserId, const FString& InCharacterId, const FString& Document,
	const FString& ExpectedVersion)
{
	// "record" last and verbatim: the document is already JSON, and re-encoding it could change numbers.
	return FString::Printf(TEXT("{\"user_id\":%s,\"character_id\":%s,\"expected_version\":%s,\"record\":%s}"), *FCrossguardNakamaClient::Quote(UserId),
		*FCrossguardNakamaClient::Quote(InCharacterId), *FCrossguardNakamaClient::Quote(ExpectedVersion), *Document);
}

bool UCrossguardNakamaSaveBackend::ParseLoadResponse(const FString& Body, bool& bOutFound, FString& OutVersion, FString& OutRecord)
{
	// The module writes {"found":..,"version":"..","record":..} in that order, and "found" and "version"
	// (a bool and a hash) cannot contain the marker, so its first match is the record field.
	static const FString RecordMarker = TEXT(",\"record\":");
	const int32 RecordAt = Body.Find(RecordMarker, ESearchCase::CaseSensitive);
	const int32 End = Body.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (RecordAt == INDEX_NONE || End == INDEX_NONE || End < RecordAt)
	{
		return false;
	}
	TSharedPtr<FJsonObject> Head;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body.Left(RecordAt) + TEXT("}")), Head) || !Head.IsValid()
		|| !Head->TryGetBoolField(TEXT("found"), bOutFound) || !Head->TryGetStringField(TEXT("version"), OutVersion))
	{
		return false;
	}
	const int32 RecordStart = RecordAt + RecordMarker.Len();
	OutRecord = Body.Mid(RecordStart, End - RecordStart).TrimStartAndEnd();
	if (!bOutFound)
	{
		OutRecord.Reset();
		return OutVersion == TEXT("*");
	}
	return OutRecord.StartsWith(TEXT("{")) && !OutVersion.IsEmpty() && OutVersion != TEXT("*");
}

float UCrossguardNakamaSaveBackend::RetryDelaySeconds(int32 Attempt)
{
	return FMath::Min(30.0f, static_cast<float>(1 << FMath::Clamp(Attempt - 1, 0, 5)));
}

// --- Load ----------------------------------------------------------------------------------------

void UCrossguardNakamaSaveBackend::LoadCharacter(const FString& PlayerId, FLoadCallback OnLoaded)
{
	FCrossguardSaveLoadResult Result;
	if (!CanCall(Result.Error))
	{
		OnLoaded(Result);
		return;
	}
	if (!FCrossguardNakamaClient::IsUserId(PlayerId))
	{
		// A development / session / Steam id: this player did not log in through Nakama.
		Result.Error = FString::Printf(TEXT("\"%s\" is not a Nakama user id (log in with ?%s=)"), *PlayerId, TEXT("NakamaToken"));
		OnLoaded(Result);
		return;
	}
	FPlayerSaves& Entry = Players.FindOrAdd(PlayerId);
	if (Entry.IsBusy())
	{
		// Writes for this player are still going out (they left and came back quickly): load after them.
		Entry.DeferredLoads.Add(MoveTemp(OnLoaded));
		return;
	}
	StartLoad(PlayerId, 1, MoveTemp(OnLoaded));
}

void UCrossguardNakamaSaveBackend::StartLoad(const FString& PlayerId, int32 Attempt, FLoadCallback OnLoaded)
{
	const FString Payload = FString::Printf(TEXT("{\"user_id\":%s,\"character_id\":%s}"), *FCrossguardNakamaClient::Quote(PlayerId),
		*FCrossguardNakamaClient::Quote(CharacterId));
	TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
	Client->CallServerRpc(HttpKey, CrossguardNakamaSaveLocal::LoadRpc, Payload,
		[WeakThis, PlayerId, Attempt, OnLoaded = MoveTemp(OnLoaded)](const FCrossguardNakamaResponse& Response)
		{
			if (UCrossguardNakamaSaveBackend* Self = WeakThis.Get())
			{
				Self->HandleLoad(PlayerId, Attempt, OnLoaded, Response);
			}
			else
			{
				FCrossguardSaveLoadResult Result;
				Result.Error = TEXT("save backend gone");
				OnLoaded(Result);
			}
		});
}

void UCrossguardNakamaSaveBackend::HandleLoad(const FString& PlayerId, int32 Attempt, FLoadCallback OnLoaded, const FCrossguardNakamaResponse& Response)
{
	FCrossguardSaveLoadResult Result;
	if (!Response.IsOk())
	{
		if (Response.IsRetryable() && Attempt < MaxLoadAttempts && !bFlushing)
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Nakama: load of %s failed (%s); retrying"), *PlayerId, *Response.Describe());
			TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, PlayerId, Attempt, OnLoaded](float)
			{
				if (UCrossguardNakamaSaveBackend* Self = WeakThis.Get())
				{
					Self->StartLoad(PlayerId, Attempt + 1, OnLoaded);
				}
				return false;
			}), RetryDelaySeconds(Attempt));
			return;
		}
		Result.Status = ECrossguardSaveLoadStatus::Failed;
		Result.Error = Response.Describe();
		OnLoaded(Result);
		return;
	}

	bool bFound = false;
	FString Version;
	FString Record;
	if (!ParseLoadResponse(Response.Body, bFound, Version, Record))
	{
		Result.Status = ECrossguardSaveLoadStatus::Failed;
		Result.Error = FString::Printf(TEXT("unexpected answer from %s: %s"), CrossguardNakamaSaveLocal::LoadRpc, *Response.Body.Left(200));
		OnLoaded(Result);
		return;
	}
	FPlayerSaves& Entry = Players.FindOrAdd(PlayerId);
	// A fresh load: this server now knows the latest version and may write again.
	Entry.Version = Version;
	Entry.bConflict = false;
	Entry.Attempt = 0;
	if (!bFound)
	{
		Result.Status = ECrossguardSaveLoadStatus::NotFound;
	}
	else if (Record.Len() > MaxDocumentBytes)
	{
		Result.Status = ECrossguardSaveLoadStatus::Corrupt;
		Result.Error = FString::Printf(TEXT("stored save is %d characters"), Record.Len());
	}
	else
	{
		Result.Status = ECrossguardSaveLoadStatus::Found;
		Result.Document = MoveTemp(Record);
	}
	OnLoaded(Result);
}

// --- Write ---------------------------------------------------------------------------------------

void UCrossguardNakamaSaveBackend::SaveCharacter(const FString& PlayerId, const FString& Document, FSaveCallback OnSaved)
{
	FString Error;
	FPlayerSaves* Entry = Players.Find(PlayerId);
	if (!CanCall(Error))
	{
		// Error says what is missing.
	}
	else if (!FCrossguardNakamaClient::IsUserId(PlayerId))
	{
		Error = TEXT("not a Nakama user id");
	}
	else if (Entry == nullptr || Entry->Version.IsEmpty())
	{
		// Without the version from a load, a write could replace a save this server never saw.
		Error = TEXT("never loaded on this server (no storage version)");
	}
	else if (Entry->bConflict)
	{
		Error = TEXT("another server changed this save (version conflict); not written until the player loads again");
	}
	if (!Error.IsEmpty())
	{
		const FString File = KeepLocally(PlayerId, Document, Error);
		if (OnSaved)
		{
			OnSaved(false, FString::Printf(TEXT("%s; kept in %s"), *Error, *File));
		}
		return;
	}
	// The newest document replaces a queued one: it holds everything the older one did.
	Entry->QueuedDocument = Document;
	Entry->bHasQueued = true;
	Entry->QueuedCallbacks.Add(MoveTemp(OnSaved));
	Pump(PlayerId);
}

void UCrossguardNakamaSaveBackend::Pump(const FString& PlayerId)
{
	FPlayerSaves* Entry = Players.Find(PlayerId);
	if (Entry == nullptr || Entry->bInFlight || Entry->RetryHandle.IsValid())
	{
		return;
	}
	if (Entry->bHasQueued)
	{
		Entry->InFlightDocument = MoveTemp(Entry->QueuedDocument);
		Entry->InFlightCallbacks.Append(MoveTemp(Entry->QueuedCallbacks));
		Entry->QueuedDocument.Reset();
		Entry->QueuedCallbacks.Reset();
		Entry->bHasQueued = false;
		Entry->Attempt = 1;
		Send(PlayerId, *Entry);
		return;
	}
	// Idle: loads that waited for the writes go now.
	if (Entry->DeferredLoads.Num() > 0)
	{
		TArray<FLoadCallback> Loads = MoveTemp(Entry->DeferredLoads);
		Entry->DeferredLoads.Reset();
		for (FLoadCallback& Load : Loads)
		{
			StartLoad(PlayerId, 1, MoveTemp(Load));
		}
	}
}

void UCrossguardNakamaSaveBackend::Send(const FString& PlayerId, FPlayerSaves& Entry)
{
	Entry.bInFlight = true;
	const FString Payload = BuildWritePayload(PlayerId, CharacterId, Entry.InFlightDocument, Entry.Version);
	TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
	Client->CallServerRpc(HttpKey, CrossguardNakamaSaveLocal::WriteRpc, Payload, [WeakThis, PlayerId](const FCrossguardNakamaResponse& Response)
	{
		if (UCrossguardNakamaSaveBackend* Self = WeakThis.Get())
		{
			Self->HandleWrite(PlayerId, Response);
		}
		else
		{
			// The backend is gone (shutdown past FlushWrites): nobody is left to keep the document.
			UE_LOG(LogCrossguard, Error, TEXT("Nakama: write of %s finished after shutdown (%s)"), *PlayerId, *Response.Describe());
		}
	});
}

void UCrossguardNakamaSaveBackend::HandleWrite(const FString& PlayerId, const FCrossguardNakamaResponse& Response)
{
	FPlayerSaves* Entry = Players.Find(PlayerId);
	if (Entry == nullptr || !Entry->bInFlight)
	{
		return;
	}
	Entry->bInFlight = false;

	if (Response.IsOk())
	{
		TSharedPtr<FJsonObject> Object;
		FString NewVersion;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) && Object.IsValid())
		{
			Object->TryGetStringField(TEXT("version"), NewVersion);
		}
		if (NewVersion.IsEmpty())
		{
			// Stored, but the next write cannot prove it saw this one: treat as a conflict (reload fixes it).
			Entry->bConflict = true;
			UE_LOG(LogCrossguard, Error, TEXT("Nakama: write of %s answered without a version; writes stop until the player loads again"), *PlayerId);
		}
		Entry->Version = NewVersion;
		Entry->InFlightDocument.Reset();
		Entry->Attempt = 0;
		CrossguardNakamaSaveLocal::Report(Entry->InFlightCallbacks, true, FString());
		Pump(PlayerId);
		return;
	}

	if (Response.HttpStatus == 409 || Response.ErrorCode == FCrossguardNakamaClient::CodeAborted)
	{
		// Another server wrote since our load. Nakama kept the refused record (characters_conflicts);
		// keep it here too, and stop writing this player: what we hold is out of date.
		Entry->bConflict = true;
		UE_LOG(LogCrossguard, Error, TEXT("Nakama: save of %s refused: %s. No more writes for this player until they load again."), *PlayerId,
			*Response.Describe());
		GiveUpInFlight(PlayerId, *Entry, TEXT("version conflict"));
		if (Entry->bHasQueued)
		{
			const FString File = KeepLocally(PlayerId, Entry->QueuedDocument, TEXT("version conflict"));
			Entry->QueuedDocument.Reset();
			Entry->bHasQueued = false;
			CrossguardNakamaSaveLocal::Report(Entry->QueuedCallbacks, false, FString::Printf(TEXT("version conflict; kept in %s"), *File));
		}
		Pump(PlayerId);
		return;
	}

	if (Response.IsRetryable() && Entry->Attempt < MaxWriteAttempts && !bFlushing)
	{
		// A newer document waiting goes instead (it holds this one's changes); its callbacks join.
		if (Entry->bHasQueued)
		{
			Entry->InFlightDocument = MoveTemp(Entry->QueuedDocument);
			Entry->InFlightCallbacks.Append(MoveTemp(Entry->QueuedCallbacks));
			Entry->QueuedDocument.Reset();
			Entry->QueuedCallbacks.Reset();
			Entry->bHasQueued = false;
		}
		const float Delay = RetryDelaySeconds(Entry->Attempt);
		UE_LOG(LogCrossguard, Warning, TEXT("Nakama: save of %s failed (%s); retry %d in %.0f s"), *PlayerId, *Response.Describe(), Entry->Attempt, Delay);
		TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
		Entry->RetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, PlayerId](float)
		{
			UCrossguardNakamaSaveBackend* Self = WeakThis.Get();
			FPlayerSaves* Retrying = Self != nullptr ? Self->Players.Find(PlayerId) : nullptr;
			if (Retrying != nullptr)
			{
				Retrying->RetryHandle.Reset();
				++Retrying->Attempt;
				Self->Send(PlayerId, *Retrying);
			}
			return false;
		}), Delay);
		return;
	}

	GiveUpInFlight(PlayerId, *Entry, Response.Describe());
	Pump(PlayerId);
}

void UCrossguardNakamaSaveBackend::GiveUpInFlight(const FString& PlayerId, FPlayerSaves& Entry, const FString& Reason)
{
	const FString File = KeepLocally(PlayerId, Entry.InFlightDocument, Reason);
	Entry.InFlightDocument.Reset();
	Entry.Attempt = 0;
	CrossguardNakamaSaveLocal::Report(Entry.InFlightCallbacks, false, FString::Printf(TEXT("%s; kept in %s"), *Reason, *File));
}

FString UCrossguardNakamaSaveBackend::KeepLocally(const FString& PlayerId, const FString& Document, const FString& Reason)
{
	if (Fallback == nullptr || Document.IsEmpty())
	{
		UE_LOG(LogCrossguard, Error, TEXT("Nakama: save of %s NOT stored anywhere (%s)"), *PlayerId, *Reason);
		return FString();
	}
	FString File = FPaths::ConvertRelativePathToFull(Fallback->GetFilePath(PlayerId));
	Fallback->SaveCharacter(PlayerId, Document, [&File](bool bSaved, const FString& Error)
	{
		if (!bSaved)
		{
			File = FString::Printf(TEXT("<nowhere: %s>"), *Error);
		}
	});
	UE_LOG(LogCrossguard, Error, TEXT("Nakama: save of %s not stored in Nakama (%s); kept in %s"), *PlayerId, *Reason, *File);
	return File;
}

// --- Quarantine, shutdown ------------------------------------------------------------------------

void UCrossguardNakamaSaveBackend::QuarantineCharacter(const FString& PlayerId, const FString& Reason)
{
	FString Error;
	if (!CanCall(Error) || !FCrossguardNakamaClient::IsUserId(PlayerId))
	{
		UE_LOG(LogCrossguard, Error, TEXT("Nakama: cannot set aside the refused save of %s (%s)"), *PlayerId, Error.IsEmpty() ? TEXT("not a Nakama user id") : *Error);
		return;
	}
	const FString Payload = FString::Printf(TEXT("{\"user_id\":%s,\"character_id\":%s,\"reason\":%s}"), *FCrossguardNakamaClient::Quote(PlayerId),
		*FCrossguardNakamaClient::Quote(CharacterId), *FCrossguardNakamaClient::Quote(Reason));
	Client->CallServerRpc(HttpKey, CrossguardNakamaSaveLocal::QuarantineRpc, Payload, [PlayerId, Reason](const FCrossguardNakamaResponse& Response)
	{
		if (Response.IsOk())
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Save for %s set aside in Nakama (characters_quarantine): %s (%s)"), *PlayerId, *Response.Body, *Reason);
		}
		else
		{
			// The stored document stays where it is until the next write replaces it.
			UE_LOG(LogCrossguard, Error, TEXT("Nakama: could not set aside the refused save of %s: %s"), *PlayerId, *Response.Describe());
		}
	});
}

void UCrossguardNakamaSaveBackend::FlushWrites(float TimeoutSeconds)
{
	TGuardValue<bool> Flushing(bFlushing, true);
	// Scheduled retries go now.
	for (TPair<FString, FPlayerSaves>& Pair : Players)
	{
		if (Pair.Value.RetryHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Pair.Value.RetryHandle);
			Pair.Value.RetryHandle.Reset();
			if (!Pair.Value.bInFlight && !Pair.Value.InFlightDocument.IsEmpty())
			{
				Send(Pair.Key, Pair.Value);
			}
		}
	}
	TArray<FString> Ids;
	Players.GetKeys(Ids);
	for (const FString& Id : Ids)
	{
		Pump(Id);
	}

	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
	const auto AnyInFlight = [this]()
	{
		for (const TPair<FString, FPlayerSaves>& Pair : Players)
		{
			if (Pair.Value.bInFlight)
			{
				return true;
			}
		}
		return false;
	};
	// Completions may send the next queued document: tick the HTTP module until all is answered.
	while (AnyInFlight() && FPlatformTime::Seconds() < Deadline)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.05f);
		FPlatformProcess::Sleep(0.05f);
	}
	for (TPair<FString, FPlayerSaves>& Pair : Players)
	{
		FPlayerSaves& Entry = Pair.Value;
		if (Entry.bInFlight)
		{
			// No answer in time: it may still land, but keep it here as well.
			Entry.bInFlight = false;
			GiveUpInFlight(Pair.Key, Entry, TEXT("no answer before shutdown"));
		}
		if (Entry.bHasQueued)
		{
			const FString File = KeepLocally(Pair.Key, Entry.QueuedDocument, TEXT("not sent before shutdown"));
			Entry.QueuedDocument.Reset();
			Entry.bHasQueued = false;
			CrossguardNakamaSaveLocal::Report(Entry.QueuedCallbacks, false, FString::Printf(TEXT("not sent before shutdown; kept in %s"), *File));
		}
	}
}
