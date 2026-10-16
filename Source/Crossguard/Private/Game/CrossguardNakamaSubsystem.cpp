#include "Game/CrossguardNakamaSubsystem.h"

#include "Crossguard.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardNakamaClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CrossguardNakamaSubsystemLocal
{
	// A verified token waits this long for its InitNewPlayer (PreLoginAsync -> Login is immediate).
	constexpr double VerifiedSessionSeconds = 60.0;

	const TCHAR* VerifySessionRpc = TEXT("crossguard_verify_session");
}

const TCHAR* UCrossguardNakamaSubsystem::TokenOption = TEXT("NakamaToken");

UCrossguardNakamaSubsystem* UCrossguardNakamaSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject != nullptr ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardNakamaSubsystem>() : nullptr;
}

void UCrossguardNakamaSubsystem::ReadServerSettings(FString& OutUrl, FString& OutHttpKey)
{
	OutUrl = GetDefault<UCrossguardNakamaSubsystem>()->NakamaUrl;
	FParse::Value(FCommandLine::Get(), TEXT("NakamaUrl="), OutUrl);
	OutHttpKey.Reset();
	if (!FParse::Value(FCommandLine::Get(), TEXT("NakamaHttpKey="), OutHttpKey) || OutHttpKey.IsEmpty())
	{
		OutHttpKey = FPlatformMisc::GetEnvironmentVariable(TEXT("CROSSGUARD_NAKAMA_HTTP_KEY"));
	}
}

void UCrossguardNakamaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Url;
	ReadServerSettings(Url, HttpKey);
	FParse::Value(FCommandLine::Get(), TEXT("NakamaServerKey="), ServerKey);
	Client = MakeShared<FCrossguardNakamaClient>(Url, RequestTimeoutSeconds);
	if (IsServerConfigured())
	{
		UE_LOG(LogCrossguard, Log, TEXT("Nakama: server-to-server calls to %s (logins %s a verified ?%s=)"), *Url,
			IsNakamaLoginRequired() ? TEXT("need") : TEXT("may bring"), TokenOption);
	}
}

bool UCrossguardNakamaSubsystem::IsNakamaLoginRequired() const
{
	if (!IsServerConfigured())
	{
		return false;
	}
#if UE_BUILD_SHIPPING
	return true;
#else
	return !FParse::Param(FCommandLine::Get(), TEXT("AllowDevPlayerIds"));
#endif
}

void UCrossguardNakamaSubsystem::VerifySessionToken(const FString& Token, FVerifyCallback OnDone)
{
	using namespace CrossguardNakamaSubsystemLocal;
	if (!IsServerConfigured() || !Client.IsValid())
	{
		OnDone(false, FString(), TEXT("this server has no Nakama HTTP key"));
		return;
	}
	PruneVerified();
	const FString Payload = FString::Printf(TEXT("{\"token\":%s}"), *FCrossguardNakamaClient::Quote(Token));
	TWeakObjectPtr<UCrossguardNakamaSubsystem> WeakThis(this);
	Client->CallServerRpc(HttpKey, VerifySessionRpc, Payload, [WeakThis, Token, OnDone = MoveTemp(OnDone)](const FCrossguardNakamaResponse& Response)
	{
		FString VerifiedId;
		TSharedPtr<FJsonObject> Object;
		if (Response.IsOk() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) && Object.IsValid())
		{
			Object->TryGetStringField(TEXT("user_id"), VerifiedId);
		}
		if (!FCrossguardNakamaClient::IsUserId(VerifiedId))
		{
			const FString Error = Response.IsOk() ? FString(TEXT("unexpected answer")) : Response.Describe();
			UE_LOG(LogCrossguard, Warning, TEXT("Nakama: session not verified: %s"), *Error);
			OnDone(false, FString(), Error);
			return;
		}
		if (UCrossguardNakamaSubsystem* Self = WeakThis.Get())
		{
			Self->VerifiedSessions.Add(Token, {VerifiedId, FPlatformTime::Seconds()});
		}
		OnDone(true, VerifiedId, FString());
	});
}

FString UCrossguardNakamaSubsystem::TakeVerifiedUserId(const FString& Token)
{
	PruneVerified();
	FVerifiedSession Session;
	return !Token.IsEmpty() && VerifiedSessions.RemoveAndCopyValue(Token, Session) ? Session.UserId : FString();
}

void UCrossguardNakamaSubsystem::AddVerifiedSessionForTesting(const FString& Token, const FString& InUserId)
{
	VerifiedSessions.Add(Token, {InUserId, FPlatformTime::Seconds()});
}

void UCrossguardNakamaSubsystem::PruneVerified()
{
	const double Now = FPlatformTime::Seconds();
	for (auto It = VerifiedSessions.CreateIterator(); It; ++It)
	{
		if (Now - It.Value().VerifiedAt > CrossguardNakamaSubsystemLocal::VerifiedSessionSeconds)
		{
			It.RemoveCurrent();
		}
	}
}

FString UCrossguardNakamaSubsystem::GetDevDeviceId()
{
	FString DeviceId;
	if (FParse::Value(FCommandLine::Get(), TEXT("NakamaDeviceId="), DeviceId) && !DeviceId.IsEmpty())
	{
		return DeviceId.Left(128);
	}
	// One PC runs several clients (Tools/run_local_cluster): their -CrossguardPlayerId= tells them apart.
	FString Name;
	FParse::Value(FCommandLine::Get(), TEXT("CrossguardPlayerId="), Name);
	if (Name.IsEmpty())
	{
		Name = FPlatformMisc::GetLoginId();
	}
	// Nakama wants 10-128 characters; the prefix alone is 15.
	return (TEXT("crossguard-dev-") + Name).Left(128);
}

void UCrossguardNakamaSubsystem::LoginWithDevice(FLoginCallback OnDone)
{
	if (LoginState == ECrossguardNakamaLoginState::LoggingIn || !Client.IsValid())
	{
		if (OnDone)
		{
			OnDone(false, TEXT("already logging in"));
		}
		return;
	}
	LoginState = ECrossguardNakamaLoginState::LoggingIn;
	LastError.Reset();
	TWeakObjectPtr<UCrossguardNakamaSubsystem> WeakThis(this);
	Client->AuthenticateDevice(ServerKey, GetDevDeviceId(), [WeakThis, OnDone = MoveTemp(OnDone)](const FCrossguardNakamaResponse& Response)
	{
		UCrossguardNakamaSubsystem* Self = WeakThis.Get();
		if (Self == nullptr)
		{
			return;
		}
		FString Token;
		TSharedPtr<FJsonObject> Object;
		if (Response.IsOk() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) && Object.IsValid())
		{
			Object->TryGetStringField(TEXT("token"), Token);
		}
		FString NewUserId;
		FString NewUsername;
		int64 ExpiresAt = 0;
		if (!FCrossguardNakamaClient::ReadTokenClaims(Token, NewUserId, NewUsername, ExpiresAt))
		{
			Self->LoginState = ECrossguardNakamaLoginState::Failed;
			Self->LastError = Response.IsOk() ? FString(TEXT("unreadable session")) : Response.Describe();
			UE_LOG(LogCrossguard, Warning, TEXT("Nakama login failed: %s"), *Self->LastError);
			if (OnDone)
			{
				OnDone(false, Self->LastError);
			}
			return;
		}
		Self->SessionToken = Token;
		Self->UserId = NewUserId;
		Self->Username = NewUsername;
		Self->SessionExpiresAt = ExpiresAt;
		Self->LoginState = ECrossguardNakamaLoginState::LoggedIn;
		UE_LOG(LogCrossguard, Log, TEXT("Nakama: logged in as %s (%s)"), *NewUsername, *NewUserId);
		if (OnDone)
		{
			OnDone(true, FString());
		}
	});
}

bool UCrossguardNakamaSubsystem::IsLoggedIn() const
{
	return LoginState == ECrossguardNakamaLoginState::LoggedIn && FDateTime::UtcNow().ToUnixTimestamp() < SessionExpiresAt;
}

FString UCrossguardNakamaSubsystem::DescribeLogin() const
{
	switch (LoginState)
	{
	case ECrossguardNakamaLoginState::LoggingIn:
		return TEXT("Logging in...");
	case ECrossguardNakamaLoginState::LoggedIn:
		return IsLoggedIn() ? FString::Printf(TEXT("Logged in as %s"), *Username) : FString(TEXT("Session expired: log in again"));
	case ECrossguardNakamaLoginState::Failed:
		return FString::Printf(TEXT("Login failed: %s"), *LastError);
	default:
		return TEXT("Not logged in");
	}
}

FString UCrossguardNakamaSubsystem::GetLoginOption() const
{
	// TODO: refresh the session (refresh token, /v2/account/session/refresh) before it expires; until
	// then an expired session sends no token and the player logs in again from the menu.
	return IsLoggedIn() ? FString::Printf(TEXT("%s=%s"), TokenOption, *SessionToken) : FString();
}
