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
	const TCHAR* LeaseAcquireRpc = TEXT("crossguard_lease_acquire");
	const TCHAR* LeaseRenewRpc = TEXT("crossguard_lease_renew");
	const TCHAR* LeaseReleaseRpc = TEXT("crossguard_lease_release");

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

void UCrossguardNakamaSaveBackend::Configure(const FString& Url, const FString& InHttpKey, float TimeoutSeconds, const FString& InServerId)
{
	Client.Reset();
	if (!Url.IsEmpty())
	{
		Client = MakeShared<FCrossguardNakamaClient>(Url, TimeoutSeconds);
	}
	HttpKey = InHttpKey;
	ServerId = InServerId;
	CharacterId = DefaultCharacterId;
	if (!RenewHandle.IsValid())
	{
		RenewHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCrossguardNakamaSaveBackend::RenewTick), LeaseRenewSeconds);
	}
	Fallback = NewObject<UCrossguardLocalSaveBackend>(this);
	Fallback->SetDirectory(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crossguard"), TEXT("NakamaUnsaved")));
}

FString UCrossguardNakamaSaveBackend::DescribeBackend() const
{
	return FString::Printf(TEXT("Nakama at %s (character \"%s\", save leases as %s; unsaved documents kept in %s)"),
		Client.IsValid() ? *Client->GetBaseUrl() : TEXT("<no url>"), *CharacterId, *ServerId, *GetFallbackDirectory());
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

bool UCrossguardNakamaSaveBackend::HoldsLease(const FString& PlayerId) const
{
	const FPlayerSaves* Entry = Players.Find(PlayerId);
	return Entry != nullptr && !Entry->LeaseToken.IsEmpty();
}

bool UCrossguardNakamaSaveBackend::CanCall(FString& OutError) const
{
	if (!Client.IsValid() || HttpKey.IsEmpty() || ServerId.IsEmpty())
	{
		OutError = TEXT("Nakama is not configured (-NakamaUrl= and -NakamaHttpKey= / CROSSGUARD_NAKAMA_HTTP_KEY)");
		return false;
	}
	return true;
}

// --- Helpers -------------------------------------------------------------------------------------

FString UCrossguardNakamaSaveBackend::BuildWritePayload(const FString& UserId, const FString& InCharacterId, const FString& Document,
	const FString& ExpectedVersion, const FString& LeaseToken)
{
	// "record" last and verbatim: the document is already JSON, and re-encoding it could change numbers.
	return FString::Printf(TEXT("{\"user_id\":%s,\"character_id\":%s,\"expected_version\":%s,\"lease_token\":%s,\"record\":%s}"),
		*FCrossguardNakamaClient::Quote(UserId), *FCrossguardNakamaClient::Quote(InCharacterId), *FCrossguardNakamaClient::Quote(ExpectedVersion),
		*FCrossguardNakamaClient::Quote(LeaseToken), *Document);
}

FString UCrossguardNakamaSaveBackend::BuildLeasePayload(const FString& UserId, const FString& InCharacterId, const FString& InServerId,
	const FString& LeaseToken, int32 TtlSeconds)
{
	return FString::Printf(TEXT("{\"user_id\":%s,\"character_id\":%s,\"server_id\":%s,\"lease_token\":%s,\"ttl_sec\":%d}"),
		*FCrossguardNakamaClient::Quote(UserId), *FCrossguardNakamaClient::Quote(InCharacterId), *FCrossguardNakamaClient::Quote(InServerId),
		*FCrossguardNakamaClient::Quote(LeaseToken), TtlSeconds);
}

bool UCrossguardNakamaSaveBackend::IsLeaseLost(const FCrossguardNakamaResponse& Response)
{
	return Response.bConnected && Response.ErrorCode == FCrossguardNakamaClient::CodeFailedPrecondition && Response.ErrorMessage.Contains(TEXT("lease"));
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

FString UCrossguardNakamaSaveBackend::WithoutSavedAt(const FString& Document)
{
	static const FString Key = TEXT("\"savedAt\"");
	const int32 KeyAt = Document.Find(Key, ESearchCase::CaseSensitive);
	if (KeyAt == INDEX_NONE)
	{
		return Document;
	}
	// Skip  : -123  (spaces allowed around the colon).
	int32 End = KeyAt + Key.Len();
	while (End < Document.Len() && (FChar::IsWhitespace(Document[End]) || Document[End] == TEXT(':')))
	{
		++End;
	}
	while (End < Document.Len() && (FChar::IsDigit(Document[End]) || Document[End] == TEXT('-')))
	{
		++End;
	}
	return Document.Left(KeyAt + Key.Len()) + Document.Mid(End);
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
	// Back on this server: keep the lease.
	Entry.bReleaseWhenIdle = false;
	if (Entry.IsBusy())
	{
		// Writes (or the lease release) for this player are still going out (they left and came back
		// quickly): load after them.
		Entry.DeferredLoads.Add(MoveTemp(OnLoaded));
		return;
	}
	StartAcquire(PlayerId, FPlatformTime::Seconds() + LeaseWaitSeconds, MoveTemp(OnLoaded));
}

void UCrossguardNakamaSaveBackend::StartAcquire(const FString& PlayerId, double WaitUntil, FLoadCallback OnLoaded)
{
	TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
	Client->CallServerRpc(HttpKey, CrossguardNakamaSaveLocal::LeaseAcquireRpc, BuildLeasePayload(PlayerId, CharacterId, ServerId, FString(), LeaseTtlSeconds),
		[WeakThis, PlayerId, WaitUntil, OnLoaded = MoveTemp(OnLoaded)](const FCrossguardNakamaResponse& Response)
		{
			if (UCrossguardNakamaSaveBackend* Self = WeakThis.Get())
			{
				Self->HandleAcquire(PlayerId, WaitUntil, OnLoaded, Response);
			}
			else
			{
				FCrossguardSaveLoadResult Result;
				Result.Error = TEXT("save backend gone");
				OnLoaded(Result);
			}
		});
}

void UCrossguardNakamaSaveBackend::HandleAcquire(const FString& PlayerId, double WaitUntil, FLoadCallback OnLoaded, const FCrossguardNakamaResponse& Response)
{
	FString Token;
	TSharedPtr<FJsonObject> Object;
	if (Response.IsOk() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) && Object.IsValid())
	{
		Object->TryGetStringField(TEXT("lease_token"), Token);
	}
	if (!Token.IsEmpty())
	{
		FPlayerSaves& Entry = Players.FindOrAdd(PlayerId);
		Entry.LeaseToken = Token;
		StartLoad(PlayerId, 1, MoveTemp(OnLoaded));
		return;
	}
	// Held by the server the player just left (still writing), or no answer: wait and try again.
	const bool bHeld = Response.bConnected && Response.ErrorCode == FCrossguardNakamaClient::CodeAborted;
	if ((bHeld || Response.IsRetryable()) && FPlatformTime::Seconds() < WaitUntil && !bFlushing)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Nakama: save lease of %s not free yet (%s); trying again"), *PlayerId, *Response.Describe());
		TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, PlayerId, WaitUntil, OnLoaded](float)
		{
			if (UCrossguardNakamaSaveBackend* Self = WeakThis.Get())
			{
				Self->StartAcquire(PlayerId, WaitUntil, OnLoaded);
			}
			return false;
		}), LeaseRetrySeconds);
		return;
	}
	// The player plays unsaved this session (the subsystem keeps their stored save).
	FCrossguardSaveLoadResult Result;
	Result.Status = ECrossguardSaveLoadStatus::Failed;
	Result.Error = FString::Printf(TEXT("no save lease: %s"), *Response.Describe());
	UE_LOG(LogCrossguard, Error, TEXT("Nakama: could not get the save lease of %s: %s"), *PlayerId, *Response.Describe());
	OnLoaded(Result);
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
	Entry.LastWritten.Reset();
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
		Error = TEXT("another server changed this save or holds its lease; not written until the player loads again");
	}
	else if (Entry->LeaseToken.IsEmpty())
	{
		Error = TEXT("this server holds no save lease for the player");
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
	// A guild bank operation is on its way: this document was built after its change was made here, so
	// it waits for the answer (written after it, or dropped if the change did not happen).
	if (Entry->IsJointPending())
	{
		Entry->HeldDocument = Document;
		Entry->bHasHeld = true;
		Entry->HeldCallbacks.Add(MoveTemp(OnSaved));
		return;
	}
	// Nothing changed since the newest write (sent, waiting or done): writing again would only move
	// the version. The caller waits for that write instead.
	const FString Unstamped = WithoutSavedAt(Document);
	if (Entry->bHasQueued && WithoutSavedAt(Entry->QueuedDocument) == Unstamped)
	{
		Entry->QueuedCallbacks.Add(MoveTemp(OnSaved));
		return;
	}
	if (!Entry->bHasQueued && !Entry->InFlightDocument.IsEmpty() && WithoutSavedAt(Entry->InFlightDocument) == Unstamped)
	{
		Entry->InFlightCallbacks.Add(MoveTemp(OnSaved));
		return;
	}
	if (!Entry->IsBusy() && !Entry->LastWritten.IsEmpty() && Entry->LastWritten == Unstamped)
	{
		if (OnSaved)
		{
			OnSaved(true, FString());
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
	if (Entry == nullptr || Entry->bInFlight || Entry->bJointInFlight || Entry->RetryHandle.IsValid())
	{
		return;
	}
	// Saves queued before a guild bank operation go first (they do not hold its change), then it.
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
	if (Entry->bJointWaiting)
	{
		SendJoint(PlayerId, *Entry);
		return;
	}
	if (Entry->bReleaseInFlight)
	{
		return;
	}
	// Idle: loads that waited for the writes go now (acquiring the lease again).
	if (Entry->DeferredLoads.Num() > 0)
	{
		TArray<FLoadCallback> Loads = MoveTemp(Entry->DeferredLoads);
		Entry->DeferredLoads.Reset();
		Entry->bReleaseWhenIdle = false;
		const double WaitUntil = FPlatformTime::Seconds() + LeaseWaitSeconds;
		for (FLoadCallback& Load : Loads)
		{
			StartAcquire(PlayerId, WaitUntil, MoveTemp(Load));
		}
		return;
	}
	// Idle and gone (logout, travel): the next server may take the save over.
	if (Entry->bReleaseWhenIdle && !Entry->LeaseToken.IsEmpty())
	{
		SendRelease(PlayerId, *Entry);
	}
}

void UCrossguardNakamaSaveBackend::Send(const FString& PlayerId, FPlayerSaves& Entry)
{
	Entry.bInFlight = true;
	const FString Payload = BuildWritePayload(PlayerId, CharacterId, Entry.InFlightDocument, Entry.Version, Entry.LeaseToken);
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
		Entry->LastWritten = NewVersion.IsEmpty() ? FString() : WithoutSavedAt(Entry->InFlightDocument);
		Entry->InFlightDocument.Reset();
		Entry->Attempt = 0;
		CrossguardNakamaSaveLocal::Report(Entry->InFlightCallbacks, true, FString());
		Pump(PlayerId);
		return;
	}

	const bool bLeaseLost = IsLeaseLost(Response);
	if (bLeaseLost || Response.HttpStatus == 409 || Response.ErrorCode == FCrossguardNakamaClient::CodeAborted)
	{
		// Another server wrote since our load (Nakama kept the refused record in characters_conflicts), or
		// holds the player's save lease now. Keep the document here too, and stop writing this player: what
		// we hold is out of date.
		const TCHAR* Reason = bLeaseLost ? TEXT("save lease lost") : TEXT("version conflict");
		if (bLeaseLost)
		{
			LoseLease(PlayerId, *Entry, Response.Describe());
		}
		Entry->bConflict = true;
		UE_LOG(LogCrossguard, Error, TEXT("Nakama: save of %s refused: %s. No more writes for this player until they load again."), *PlayerId,
			*Response.Describe());
		GiveUpInFlight(PlayerId, *Entry, Reason);
		if (Entry->bHasQueued)
		{
			const FString File = KeepLocally(PlayerId, Entry->QueuedDocument, Reason);
			Entry->QueuedDocument.Reset();
			Entry->bHasQueued = false;
			CrossguardNakamaSaveLocal::Report(Entry->QueuedCallbacks, false, FString::Printf(TEXT("%s; kept in %s"), Reason, *File));
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

// --- Guild bank operations ------------------------------------------------------------------------

FString UCrossguardNakamaSaveBackend::BuildSaveMember(const FString& UserId, const FString& InCharacterId, const FString& Document,
	const FString& ExpectedVersion, const FString& LeaseToken)
{
	return FString::Printf(TEXT("{\"user_id\":%s,\"character_id\":%s,\"record\":%s,\"expected_version\":%s,\"lease_token\":%s}"),
		*FCrossguardNakamaClient::Quote(UserId), *FCrossguardNakamaClient::Quote(InCharacterId), *Document, *FCrossguardNakamaClient::Quote(ExpectedVersion),
		*FCrossguardNakamaClient::Quote(LeaseToken));
}

FString UCrossguardNakamaSaveBackend::ReadSaveVersion(const FString& Body)
{
	TSharedPtr<FJsonObject> Object;
	FString Version;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) && Object.IsValid())
	{
		Object->TryGetStringField(TEXT("save_version"), Version);
	}
	return Version;
}

bool UCrossguardNakamaSaveBackend::HasPendingJoint(const FString& PlayerId) const
{
	const FPlayerSaves* Entry = Players.Find(PlayerId);
	return Entry != nullptr && Entry->IsJointPending();
}

bool UCrossguardNakamaSaveBackend::SendWithSave(const FString& PlayerId, const FString& Document, const FString& RpcId,
	TFunction<FString(const FString& SaveMember)> MakePayload, FJointCallback OnDone, FString& OutError)
{
	FPlayerSaves* Entry = Players.Find(PlayerId);
	if (!CanCall(OutError))
	{
		return false;
	}
	if (!FCrossguardNakamaClient::IsUserId(PlayerId))
	{
		OutError = TEXT("not a Nakama user id");
	}
	else if (Entry == nullptr || Entry->Version.IsEmpty())
	{
		OutError = TEXT("never loaded on this server (no storage version)");
	}
	else if (Entry->bConflict || Entry->LeaseToken.IsEmpty())
	{
		OutError = TEXT("this server does not own the player's save (conflict or no lease)");
	}
	else if (Entry->IsJointPending() || Entry->bHasHeld)
	{
		OutError = TEXT("another guild bank operation of this player is on its way");
	}
	if (!OutError.IsEmpty())
	{
		return false;
	}
	Entry->bJointWaiting = true;
	Entry->JointRpc = RpcId;
	Entry->JointDocument = Document;
	Entry->JointPayload.Reset();
	Entry->JointMakePayload = MoveTemp(MakePayload);
	Entry->JointCallback = MoveTemp(OnDone);
	Entry->JointAttempt = 0;
	Pump(PlayerId);
	return true;
}

void UCrossguardNakamaSaveBackend::SendJoint(const FString& PlayerId, FPlayerSaves& Entry)
{
	FCrossguardNakamaResponse NotSent;
	if (Entry.bConflict || Entry.LeaseToken.IsEmpty())
	{
		// A write queued before it lost the race or the lease: this one cannot be written either.
		NotSent.ErrorMessage = TEXT("not sent: this server no longer owns the player's save");
		FinishJoint(PlayerId, Entry, NotSent, false);
		return;
	}
	if (Entry.JointPayload.IsEmpty())
	{
		// The version the writes before it left (they are done).
		Entry.JointPayload = Entry.JointMakePayload(BuildSaveMember(PlayerId, CharacterId, Entry.JointDocument, Entry.Version, Entry.LeaseToken));
	}
	Entry.bJointWaiting = false;
	Entry.bJointInFlight = true;
	++Entry.JointAttempt;
	TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
	Client->CallServerRpc(HttpKey, Entry.JointRpc, Entry.JointPayload, [WeakThis, PlayerId](const FCrossguardNakamaResponse& Response)
	{
		if (UCrossguardNakamaSaveBackend* Self = WeakThis.Get())
		{
			Self->HandleJoint(PlayerId, Response);
		}
		else
		{
			UE_LOG(LogCrossguard, Error, TEXT("Nakama: guild bank operation of %s finished after shutdown (%s)"), *PlayerId, *Response.Describe());
		}
	});
}

void UCrossguardNakamaSaveBackend::HandleJoint(const FString& PlayerId, const FCrossguardNakamaResponse& Response)
{
	FPlayerSaves* Entry = Players.Find(PlayerId);
	if (Entry == nullptr || !Entry->bJointInFlight)
	{
		return;
	}
	Entry->bJointInFlight = false;
	if (Response.IsOk())
	{
		const FString NewVersion = ReadSaveVersion(Response.Body);
		if (!NewVersion.IsEmpty())
		{
			Entry->Version = NewVersion;
			Entry->LastWritten = WithoutSavedAt(Entry->JointDocument);
		}
		FinishJoint(PlayerId, *Entry, Response, !NewVersion.IsEmpty());
		return;
	}
	if (IsLeaseLost(Response))
	{
		LoseLease(PlayerId, *Entry, Response.Describe());
		FinishJoint(PlayerId, *Entry, Response, false);
		return;
	}
	if (Response.HttpStatus == 409 || Response.ErrorCode == FCrossguardNakamaClient::CodeAborted)
	{
		// Nothing was written. A stale save means another server wrote it: stop writing, as for a save.
		if (Response.ErrorMessage.Contains(TEXT("version conflict")))
		{
			Entry->bConflict = true;
			UE_LOG(LogCrossguard, Error, TEXT("Nakama: guild bank operation of %s refused, the save changed elsewhere (%s). No more writes for this player until they load again."),
				*PlayerId, *Response.Describe());
		}
		FinishJoint(PlayerId, *Entry, Response, false);
		return;
	}
	if (Response.IsRetryable() && Entry->JointAttempt < MaxWriteAttempts && !bFlushing)
	{
		// The same payload (request id): applied already, Nakama answers from its record of it.
		Entry->bJointWaiting = true;
		const float Delay = RetryDelaySeconds(Entry->JointAttempt);
		UE_LOG(LogCrossguard, Warning, TEXT("Nakama: guild bank operation of %s failed (%s); retry %d in %.0f s"), *PlayerId, *Response.Describe(),
			Entry->JointAttempt, Delay);
		TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
		Entry->RetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, PlayerId](float)
		{
			UCrossguardNakamaSaveBackend* Self = WeakThis.Get();
			if (FPlayerSaves* Retrying = Self != nullptr ? Self->Players.Find(PlayerId) : nullptr)
			{
				Retrying->RetryHandle.Reset();
				Self->Pump(PlayerId);
			}
			return false;
		}), Delay);
		return;
	}
	// No answer: it may or may not have landed. The version stays the one from before it, so if it did,
	// the next write of this player is refused (version conflict) instead of undoing it.
	UE_LOG(LogCrossguard, Error, TEXT("Nakama: guild bank operation of %s got no answer (%s); its outcome is unknown, later writes are version-checked"),
		*PlayerId, *Response.Describe());
	FinishJoint(PlayerId, *Entry, Response, false);
}

void UCrossguardNakamaSaveBackend::FinishJoint(const FString& PlayerId, FPlayerSaves& Entry, const FCrossguardNakamaResponse& Response, bool bSaveWritten)
{
	FJointCallback Callback = MoveTemp(Entry.JointCallback);
	Entry.bJointWaiting = false;
	Entry.bJointInFlight = false;
	Entry.JointRpc.Reset();
	Entry.JointDocument.Reset();
	Entry.JointPayload.Reset();
	Entry.JointMakePayload = nullptr;
	Entry.JointCallback = nullptr;
	Entry.JointAttempt = 0;
	if (Entry.bHasHeld)
	{
		const FString Held = MoveTemp(Entry.HeldDocument);
		TArray<FSaveCallback> HeldCallbacks = MoveTemp(Entry.HeldCallbacks);
		Entry.HeldDocument.Reset();
		Entry.HeldCallbacks.Reset();
		Entry.bHasHeld = false;
		if (bSaveWritten)
		{
			// Built after the change, which is in the save now: it goes next, as usual.
			SaveCharacter(PlayerId, Held, [Waiting = HeldCallbacks](bool bSaved, const FString& Error) mutable
			{
				CrossguardNakamaSaveLocal::Report(Waiting, bSaved, Error);
			});
		}
		else
		{
			// Built with a change Nakama did not make (or of unknown outcome): never written. The guild
			// subsystem puts the player back and saves again.
			UE_LOG(LogCrossguard, Warning, TEXT("Nakama: a save of %s made during a guild bank operation was dropped (the operation was not written)"), *PlayerId);
			CrossguardNakamaSaveLocal::Report(HeldCallbacks, false, TEXT("dropped: built with a guild bank change that was not written"));
		}
	}
	if (Callback)
	{
		Callback(Response, bSaveWritten);
	}
	Pump(PlayerId);
}

// --- Leases ----------------------------------------------------------------------------------------

void UCrossguardNakamaSaveBackend::LoseLease(const FString& PlayerId, FPlayerSaves& Entry, const FString& Why)
{
	Entry.LeaseToken.Reset();
	Entry.bConflict = true;
	UE_LOG(LogCrossguard, Error, TEXT("Nakama: save lease of %s lost (%s): another server owns the save now; no more writes here until they load again"),
		*PlayerId, *Why);
}

void UCrossguardNakamaSaveBackend::ReleaseCharacter(const FString& PlayerId)
{
	FPlayerSaves* Entry = Players.Find(PlayerId);
	if (Entry == nullptr || Entry->LeaseToken.IsEmpty())
	{
		return;
	}
	// After the writes still going out (the logout save among them).
	Entry->bReleaseWhenIdle = true;
	Pump(PlayerId);
}

void UCrossguardNakamaSaveBackend::SendRelease(const FString& PlayerId, FPlayerSaves& Entry)
{
	const FString Token = Entry.LeaseToken;
	Entry.LeaseToken.Reset();
	Entry.bReleaseWhenIdle = false;
	Entry.bReleaseInFlight = true;
	TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
	Client->CallServerRpc(HttpKey, CrossguardNakamaSaveLocal::LeaseReleaseRpc, BuildLeasePayload(PlayerId, CharacterId, ServerId, Token, 0),
		[WeakThis, PlayerId](const FCrossguardNakamaResponse& Response)
		{
			if (!Response.IsOk())
			{
				// It runs out after its TTL anyway; the next server waits until then.
				UE_LOG(LogCrossguard, Warning, TEXT("Nakama: save lease of %s not released (%s); it expires on its own"), *PlayerId, *Response.Describe());
			}
			UCrossguardNakamaSaveBackend* Self = WeakThis.Get();
			FPlayerSaves* Released = Self != nullptr ? Self->Players.Find(PlayerId) : nullptr;
			if (Released != nullptr)
			{
				Released->bReleaseInFlight = false;
				// A load that came meanwhile acquires again.
				Self->Pump(PlayerId);
			}
		});
}

bool UCrossguardNakamaSaveBackend::RenewTick(float DeltaSeconds)
{
	if (bFlushing || !Client.IsValid())
	{
		return true;
	}
	for (TPair<FString, FPlayerSaves>& Pair : Players)
	{
		if (Pair.Value.LeaseToken.IsEmpty() || Pair.Value.bReleaseInFlight)
		{
			continue;
		}
		const FString PlayerId = Pair.Key;
		const FString Token = Pair.Value.LeaseToken;
		TWeakObjectPtr<UCrossguardNakamaSaveBackend> WeakThis(this);
		Client->CallServerRpc(HttpKey, CrossguardNakamaSaveLocal::LeaseRenewRpc, BuildLeasePayload(PlayerId, CharacterId, ServerId, Token, LeaseTtlSeconds),
			[WeakThis, PlayerId, Token](const FCrossguardNakamaResponse& Response)
			{
				UCrossguardNakamaSaveBackend* Self = WeakThis.Get();
				FPlayerSaves* Renewed = Self != nullptr ? Self->Players.Find(PlayerId) : nullptr;
				if (Renewed == nullptr || Renewed->LeaseToken != Token || Response.IsOk())
				{
					return;
				}
				if (IsLeaseLost(Response))
				{
					Self->LoseLease(PlayerId, *Renewed, Response.Describe());
				}
				else
				{
					// No answer: the next tick tries again (the TTL is three renewals long).
					UE_LOG(LogCrossguard, Warning, TEXT("Nakama: save lease of %s not renewed (%s)"), *PlayerId, *Response.Describe());
				}
			});
	}
	return true;
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
			if (Pair.Value.bInFlight || Pair.Value.IsJointPending())
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
		if (Entry.IsJointPending())
		{
			// Unsent or unanswered: its outcome is Nakama's (version-checked); nothing more is written for it.
			FCrossguardNakamaResponse NoAnswer;
			NoAnswer.ErrorMessage = TEXT("no answer before shutdown");
			UE_LOG(LogCrossguard, Error, TEXT("Nakama: guild bank operation of %s unanswered at shutdown"), *Pair.Key);
			Entry.bJointInFlight = false;
			FinishJoint(Pair.Key, Entry, NoAnswer, false);
		}
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

	// Every lease goes back, so the players' next server does not wait for them to expire.
	if (RenewHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RenewHandle);
		RenewHandle.Reset();
	}
	for (TPair<FString, FPlayerSaves>& Pair : Players)
	{
		if (!Pair.Value.LeaseToken.IsEmpty() && !Pair.Value.bReleaseInFlight && Client.IsValid())
		{
			SendRelease(Pair.Key, Pair.Value);
		}
	}
	const auto AnyReleasing = [this]()
	{
		for (const TPair<FString, FPlayerSaves>& Pair : Players)
		{
			if (Pair.Value.bReleaseInFlight)
			{
				return true;
			}
		}
		return false;
	};
	const double ReleaseDeadline = FPlatformTime::Seconds() + FMath::Min(2.0f, TimeoutSeconds);
	while (AnyReleasing() && FPlatformTime::Seconds() < ReleaseDeadline)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.05f);
		FPlatformProcess::Sleep(0.05f);
	}
}
