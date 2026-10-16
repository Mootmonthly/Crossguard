#include "Game/CrossguardNakamaSubsystem.h"

#include "Crossguard.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardNakamaClient.h"
#include "Game/CrossguardSaveSubsystem.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CrossguardNakamaSubsystemLocal
{
	// A verified token waits this long for its InitNewPlayer (PreLoginAsync -> Login is immediate).
	constexpr double VerifiedSessionSeconds = 60.0;

	const TCHAR* VerifySessionRpc = TEXT("crossguard_verify_session");
	const TCHAR* RegisterRpc = TEXT("crossguard_server_register");
	const TCHAR* HeartbeatRpc = TEXT("crossguard_server_heartbeat");
	const TCHAR* DeregisterRpc = TEXT("crossguard_server_deregister");
	const TCHAR* MatchOverRpc = TEXT("crossguard_match_over");
	const TCHAR* TicketVerifyRpc = TEXT("crossguard_ticket_verify");

	// Until the registration answers with the backend's interval (placeholder; the module's default).
	constexpr float DefaultHeartbeatSeconds = 10.0f;

	bool IsServerIdChar(TCHAR Char)
	{
		return Char < 128 && (FChar::IsAlnum(Char) || Char == TEXT('.') || Char == TEXT('-') || Char == TEXT('_'));
	}
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

	// The backend switch follows the save backend (the same setting UCrossguardSaveSubsystem reads).
	FString SaveBackendName = GetDefault<UCrossguardSaveSubsystem>()->SaveBackend;
	FParse::Value(FCommandLine::Get(), TEXT("SaveBackend="), SaveBackendName);
	bBackendEnabled = IsServerConfigured() && SaveBackendName.Equals(TEXT("Nakama"), ESearchCase::IgnoreCase);

	FString RequestedId;
	if (FParse::Value(FCommandLine::Get(), TEXT("ServerId="), RequestedId))
	{
		ServerId = MakeServerId(RequestedId, 0, FString());
	}
	if (ServerId.IsEmpty())
	{
		ServerId = MakeServerId(FPlatformProcess::ComputerName(), FPlatformProcess::GetCurrentProcessId(),
			FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower());
	}
	if (IsServerConfigured())
	{
		UE_LOG(LogCrossguard, Log, TEXT("Nakama: server-to-server calls to %s (logins %s a verified ?%s=); backend %s; server id %s"), *Url,
			IsNakamaLoginRequired() ? TEXT("need") : TEXT("may bring"), TokenOption,
			bBackendEnabled ? TEXT("on (saves under lease, parties, matchmaking, tickets)") : TEXT("off (needs -SaveBackend=Nakama)"), *ServerId);
	}
}

void UCrossguardNakamaSubsystem::Deinitialize()
{
	DeregisterServer(2.0f);
	Super::Deinitialize();
}

FString UCrossguardNakamaSubsystem::MakeServerId(const FString& ComputerName, uint32 ProcessId, const FString& Suffix)
{
	FString Raw = ComputerName;
	if (ProcessId != 0)
	{
		Raw += FString::Printf(TEXT("-%u"), ProcessId);
	}
	if (!Suffix.IsEmpty())
	{
		Raw += TEXT("-") + Suffix;
	}
	FString Id;
	for (const TCHAR Char : Raw)
	{
		if (CrossguardNakamaSubsystemLocal::IsServerIdChar(Char))
		{
			Id.AppendChar(Char);
		}
	}
	return Id.Left(128);
}

void UCrossguardNakamaSubsystem::CallServerRpc(const FString& RpcId, const FString& PayloadJson, FCrossguardNakamaClient::FCallback OnDone) const
{
	if (!IsServerConfigured() || !Client.IsValid())
	{
		if (OnDone)
		{
			OnDone(FCrossguardNakamaResponse());
		}
		return;
	}
	Client->CallServerRpc(HttpKey, RpcId, PayloadJson, MoveTemp(OnDone));
}

// --- Game server registry ----------------------------------------------------------------------

FString UCrossguardNakamaSubsystem::GetPublicAddress(int32 Port)
{
	FString Address;
	if (FParse::Value(FCommandLine::Get(), TEXT("PublicAddress="), Address) && !Address.IsEmpty())
	{
		return Address;
	}
	return FString::Printf(TEXT("127.0.0.1:%d"), Port);
}

FString UCrossguardNakamaSubsystem::BuildRegisterPayload(const FString& InServerId, const FString& Kind, const FString& Address, int32 Capacity)
{
	return FString::Printf(TEXT("{\"server_id\":%s,\"kind\":%s,\"address\":%s,\"capacity\":%d}"), *FCrossguardNakamaClient::Quote(InServerId),
		*FCrossguardNakamaClient::Quote(Kind), *FCrossguardNakamaClient::Quote(Address), FMath::Max(0, Capacity));
}

void UCrossguardNakamaSubsystem::RegisterServer(const FString& Kind, const FString& Address, int32 Capacity, TFunction<int32()> CountPlayers)
{
	if (!bBackendEnabled)
	{
		return;
	}
	RegisteredKind = Kind;
	RegisteredAddress = Address;
	RegisteredCapacity = Capacity;
	PlayerCounter = MoveTemp(CountPlayers);
	SendRegistration();
}

void UCrossguardNakamaSubsystem::SendRegistration()
{
	using namespace CrossguardNakamaSubsystemLocal;
	TWeakObjectPtr<UCrossguardNakamaSubsystem> WeakThis(this);
	CallServerRpc(RegisterRpc, BuildRegisterPayload(ServerId, RegisteredKind, RegisteredAddress, RegisteredCapacity),
		[WeakThis](const FCrossguardNakamaResponse& Response)
		{
			UCrossguardNakamaSubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			double Interval = DefaultHeartbeatSeconds;
			TSharedPtr<FJsonObject> Object;
			if (Response.IsOk() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) && Object.IsValid())
			{
				Object->TryGetNumberField(TEXT("heartbeat_sec"), Interval);
			}
			Self->bRegistered = Response.IsOk();
			if (Self->bRegistered)
			{
				UE_LOG(LogCrossguard, Log, TEXT("Nakama: registered as %s server %s at %s (capacity %d)"), *Self->RegisteredKind, *Self->ServerId,
					*Self->RegisteredAddress, Self->RegisteredCapacity);
			}
			else
			{
				// The heartbeat tick tries again.
				UE_LOG(LogCrossguard, Warning, TEXT("Nakama: server registration failed (%s); retrying"), *Response.Describe());
			}
			if (!Self->HeartbeatHandle.IsValid())
			{
				Self->HeartbeatHandle = FTSTicker::GetCoreTicker().AddTicker(
					FTickerDelegate::CreateUObject(Self, &UCrossguardNakamaSubsystem::HeartbeatTick), FMath::Clamp(static_cast<float>(Interval), 1.0f, 300.0f));
			}
		});
}

bool UCrossguardNakamaSubsystem::HeartbeatTick(float DeltaSeconds)
{
	using namespace CrossguardNakamaSubsystemLocal;
	if (!bRegistered)
	{
		SendRegistration();
		return true;
	}
	const int32 Players = PlayerCounter ? FMath::Max(0, PlayerCounter()) : 0;
	TWeakObjectPtr<UCrossguardNakamaSubsystem> WeakThis(this);
	CallServerRpc(HeartbeatRpc, FString::Printf(TEXT("{\"server_id\":%s,\"players\":%d}"), *FCrossguardNakamaClient::Quote(ServerId), Players),
		[WeakThis](const FCrossguardNakamaResponse& Response)
		{
			UCrossguardNakamaSubsystem* Self = WeakThis.Get();
			if (Self == nullptr || !Response.IsOk())
			{
				// No answer: the next beat tries again (three misses and the backend drops this server).
				return;
			}
			bool bKnown = true;
			TSharedPtr<FJsonObject> Object;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) && Object.IsValid())
			{
				Object->TryGetBoolField(TEXT("known"), bKnown);
			}
			if (!bKnown)
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Nakama: this server is not registered any more (backend restarted?); registering again"));
				Self->bRegistered = false;
				Self->SendRegistration();
			}
		});
	return true;
}

void UCrossguardNakamaSubsystem::WaitForHttp(const TSharedRef<bool>& bDone, float TimeoutSeconds)
{
	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
	while (!*bDone && FPlatformTime::Seconds() < Deadline)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.05f);
		FPlatformProcess::Sleep(0.05f);
	}
}

void UCrossguardNakamaSubsystem::DeregisterServer(float TimeoutSeconds)
{
	if (HeartbeatHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatHandle);
		HeartbeatHandle.Reset();
	}
	if (!bRegistered)
	{
		return;
	}
	bRegistered = false;
	const TSharedRef<bool> bDone = MakeShared<bool>(false);
	CallServerRpc(CrossguardNakamaSubsystemLocal::DeregisterRpc, FString::Printf(TEXT("{\"server_id\":%s}"), *FCrossguardNakamaClient::Quote(ServerId)),
		[bDone](const FCrossguardNakamaResponse& Response)
		{
			*bDone = true;
			UE_LOG(LogCrossguard, Log, TEXT("Nakama: server deregistered (%s)"), *Response.Describe());
		});
	WaitForHttp(bDone, TimeoutSeconds);
}

void UCrossguardNakamaSubsystem::ReportMatchOver()
{
	if (!bRegistered)
	{
		return;
	}
	CallServerRpc(CrossguardNakamaSubsystemLocal::MatchOverRpc,
		FString::Printf(TEXT("{\"server_id\":%s,\"match_id\":%s}"), *FCrossguardNakamaClient::Quote(ServerId), *FCrossguardNakamaClient::Quote(LastMatchId)),
		[](const FCrossguardNakamaResponse& Response)
		{
			UE_LOG(LogCrossguard, Log, TEXT("Nakama: match over reported (%s)"), *Response.Describe());
		});
	LastMatchId.Reset();
}

// --- Match tickets ---------------------------------------------------------------------------------

bool UCrossguardNakamaSubsystem::ParseTicketAnswer(const FString& Body, FString& OutUserId, FString& OutPartyId, FString& OutMatchId)
{
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) || !Object.IsValid())
	{
		return false;
	}
	OutPartyId.Reset();
	OutMatchId.Reset();
	Object->TryGetStringField(TEXT("party_id"), OutPartyId);
	Object->TryGetStringField(TEXT("match_id"), OutMatchId);
	return Object->TryGetStringField(TEXT("user_id"), OutUserId) && FCrossguardNakamaClient::IsUserId(OutUserId);
}

void UCrossguardNakamaSubsystem::VerifyMatchTicket(const FString& Ticket, const FString& InUserId, FTicketCallback OnDone)
{
	using namespace CrossguardNakamaSubsystemLocal;
	if (!bBackendEnabled)
	{
		OnDone(false, FString(), TEXT("the backend is off on this server"));
		return;
	}
	PruneVerified();
	const FString Payload = FString::Printf(TEXT("{\"ticket\":%s,\"server_id\":%s,\"user_id\":%s}"), *FCrossguardNakamaClient::Quote(Ticket),
		*FCrossguardNakamaClient::Quote(ServerId), *FCrossguardNakamaClient::Quote(InUserId));
	TWeakObjectPtr<UCrossguardNakamaSubsystem> WeakThis(this);
	CallServerRpc(TicketVerifyRpc, Payload, [WeakThis, Ticket, InUserId, OnDone = MoveTemp(OnDone)](const FCrossguardNakamaResponse& Response)
	{
		FString TicketUser;
		FString PartyId;
		FString MatchId;
		if (!Response.IsOk() || !ParseTicketAnswer(Response.Body, TicketUser, PartyId, MatchId) || TicketUser != InUserId)
		{
			const FString Error = Response.IsOk() ? FString(TEXT("unexpected answer")) : Response.Describe();
			UE_LOG(LogCrossguard, Warning, TEXT("Nakama: match ticket of %s refused: %s"), *InUserId, *Error);
			OnDone(false, FString(), Error);
			return;
		}
		if (UCrossguardNakamaSubsystem* Self = WeakThis.Get())
		{
			Self->VerifiedTickets.Add(Ticket, {InUserId, PartyId, FPlatformTime::Seconds()});
			Self->LastMatchId = MatchId;
		}
		OnDone(true, PartyId, FString());
	});
}

bool UCrossguardNakamaSubsystem::TakeVerifiedTicket(const FString& Ticket, const FString& InUserId, FString& OutPartyId)
{
	PruneVerified();
	FVerifiedTicket Verified;
	if (Ticket.IsEmpty() || !VerifiedTickets.RemoveAndCopyValue(Ticket, Verified) || Verified.UserId != InUserId)
	{
		return false;
	}
	OutPartyId = Verified.PartyId;
	return true;
}

void UCrossguardNakamaSubsystem::AddVerifiedTicketForTesting(const FString& Ticket, const FString& InUserId, const FString& PartyId)
{
	VerifiedTickets.Add(Ticket, {InUserId, PartyId, FPlatformTime::Seconds()});
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
	for (auto It = VerifiedTickets.CreateIterator(); It; ++It)
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

void UCrossguardNakamaSubsystem::FindMyHub(FHubCallback OnDone)
{
	if (!IsLoggedIn() || !Client.IsValid())
	{
		OnDone(FString(), TEXT("not logged in"));
		return;
	}
	Client->CallClientRpc(SessionToken, TEXT("crossguard_hub_find_mine"), TEXT("{}"), [OnDone = MoveTemp(OnDone)](const FCrossguardNakamaResponse& Response)
	{
		FString Address;
		TSharedPtr<FJsonObject> Object;
		if (Response.IsOk() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) && Object.IsValid())
		{
			Object->TryGetStringField(TEXT("address"), Address);
		}
		if (Address.IsEmpty())
		{
			OnDone(FString(), Response.HttpStatus == 404 ? FString(TEXT("no hub server is running")) : Response.Describe());
			return;
		}
		OnDone(Address, FString());
	});
}
