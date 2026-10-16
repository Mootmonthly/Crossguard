#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Game/CrossguardNakamaClient.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/Function.h"

#include "CrossguardNakamaSubsystem.generated.h"

UENUM(BlueprintType)
enum class ECrossguardNakamaLoginState : uint8
{
	LoggedOut,
	LoggingIn,
	LoggedIn,
	Failed,
};

// The game's link to Nakama (docs/backend-setup.md, docs/anti-cheat.md "Player identity").
//
// Client: logs the player in (development: a device id; Steam: TODO) and keeps the session token,
// which UCrossguardLocalPlayer adds to every server login as ?NakamaToken=<token>.
//
// Dedicated server: holds the server-to-server settings (-NakamaUrl=, -NakamaHttpKey= or the
// CROSSGUARD_NAKAMA_HTTP_KEY environment variable) and verifies the session tokens of connecting players
// (crossguard_verify_session) for ACrossguardGameMode::PreLoginAsync. The verified Nakama user id becomes
// the player id (UCrossguardGuildSubsystem::PlayerIdOf), ahead of Steam, development and session ids.
//
// With the backend on (IsBackendEnabled: the HTTP key and -SaveBackend=Nakama) a dedicated server also
// registers itself with the backend's server registry (hub or match server, address, capacity; heartbeat;
// deregister at shutdown) and match servers check the match tickets players bring (?MatchTicket=,
// crossguard_ticket_verify). Docs: docs/backend-setup.md "Step 3".
UCLASS(Config = Game)
class CROSSGUARD_API UCrossguardNakamaSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Null without a game instance.
	static UCrossguardNakamaSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Login URL option carrying the client's session token.
	static const TCHAR* TokenOption;

	// --- Settings (DefaultGame.ini, [/Script/Crossguard.CrossguardNakamaSubsystem]) ----------------

	// The Nakama API. -NakamaUrl= overrides it.
	UPROPERTY(Config)
	FString NakamaUrl = TEXT("http://127.0.0.1:7350");

	// Clients log in with it (Nakama socket.server_key; public). -NakamaServerKey= overrides it.
	UPROPERTY(Config)
	FString ServerKey = TEXT("crossguard-dev");

	UPROPERTY(Config)
	float RequestTimeoutSeconds = 10.0f;

	// The server settings: Url from -NakamaUrl= / config; HttpKey from -NakamaHttpKey=, else the
	// CROSSGUARD_NAKAMA_HTTP_KEY environment variable (preferred on hosted servers: a command line is
	// visible to other processes). Static so the save backend reads the same thing.
	static void ReadServerSettings(FString& OutUrl, FString& OutHttpKey);

	// --- Dedicated server --------------------------------------------------------------------------

	// This process has the HTTP key: it can verify players and use the Nakama save backend.
	bool IsServerConfigured() const { return !HttpKey.IsEmpty(); }
	// Remote players must bring a verified ?NakamaToken= to log in: whenever the server is configured,
	// except development servers started with -AllowDevPlayerIds (they may still join without one).
	bool IsNakamaLoginRequired() const;

	using FVerifyCallback = TFunction<void(bool bVerified, const FString& UserId, const FString& Error)>;
	// Asks Nakama who Token belongs to. On success the answer is kept briefly for TakeVerifiedUserId.
	// Callbacks on the game thread.
	void VerifySessionToken(const FString& Token, FVerifyCallback OnDone);
	// The user id verified for Token (and forgets it), or empty. ACrossguardGameMode::InitNewPlayer.
	FString TakeVerifiedUserId(const FString& Token);
	// Tests: marks Token as verified for UserId.
	void AddVerifiedSessionForTesting(const FString& Token, const FString& UserId);

	TSharedPtr<FCrossguardNakamaClient> GetClient() const { return Client; }

	// --- Backend switch, server identity ------------------------------------------------------------

	// The backend runs this server's online side: it has the HTTP key and saves go to Nakama
	// (-SaveBackend=Nakama or SaveBackend=Nakama in config). Then saves are under a lease, parties live in
	// Nakama (UCrossguardPartySubsystem), matchmaking is Nakama's (FCrossguardNakamaMatchmaking), match
	// servers take only verified match tickets and the server registers itself. Off: everything stays
	// in this process (development, PIE, tests).
	bool IsBackendEnabled() const { return bBackendEnabled; }
	// This process's id for leases, the server registry and tickets: -ServerId=, else MakeServerId.
	const FString& GetServerId() const { return ServerId; }
	// "<computer>-<process id>-<suffix>", only letters, digits, '.', '-', '_' (others dropped), 128 at most.
	static FString MakeServerId(const FString& ComputerName, uint32 ProcessId, const FString& Suffix);

	// A server-to-server RPC with this server's HTTP key. Without the key it answers at once (no
	// connection). Callback on the game thread.
	void CallServerRpc(const FString& RpcId, const FString& PayloadJson, FCrossguardNakamaClient::FCallback OnDone) const;

	// --- Game server registry (Backend/nakama/modules/fleet.go) --------------------------------------

	// Registers this server (crossguard_server_register) and reports CountPlayers() every heartbeat; a
	// heartbeat answered "unknown" (Nakama restarted) registers again. Kind: "hub" or "match". Calling it
	// again replaces the registration. Only with the backend on.
	void RegisterServer(const FString& Kind, const FString& Address, int32 Capacity, TFunction<int32()> CountPlayers);
	// crossguard_server_deregister; waits up to TimeoutSeconds for the answer (shutdown).
	void DeregisterServer(float TimeoutSeconds);
	// The match on this match server is over (crossguard_match_over): free for the next match
	// (-KeepMatchServer; a server that exits deregisters instead).
	void ReportMatchOver();
	bool IsRegistered() const { return bRegistered; }
	// The address players connect to: -PublicAddress=<host:port>, else 127.0.0.1:<Port> (one PC).
	static FString GetPublicAddress(int32 Port);
	// The crossguard_server_register payload.
	static FString BuildRegisterPayload(const FString& InServerId, const FString& Kind, const FString& Address, int32 Capacity);

	// --- Match tickets (match servers; Backend/nakama/modules/matchmaking.go) ------------------------

	using FTicketCallback = TFunction<void(bool bValid, const FString& PartyId, const FString& Error)>;
	// Asks Nakama whether Ticket lets UserId (their verified session's) into this match server. On
	// success the ticket's party is kept for TakeVerifiedTicket.
	void VerifyMatchTicket(const FString& Ticket, const FString& InUserId, FTicketCallback OnDone);
	// The verified ticket's party (empty: solo) and forgets it; false if Ticket was not verified for
	// InUserId. ACrossguardGameMode::InitNewPlayer.
	bool TakeVerifiedTicket(const FString& Ticket, const FString& InUserId, FString& OutPartyId);
	// Tests: marks Ticket as verified for InUserId and PartyId.
	void AddVerifiedTicketForTesting(const FString& Ticket, const FString& InUserId, const FString& PartyId);
	// Reads a crossguard_ticket_verify answer. False if it is not one.
	static bool ParseTicketAnswer(const FString& Body, FString& OutUserId, FString& OutPartyId, FString& OutMatchId);

	// --- Client login -----------------------------------------------------------------------------

	using FLoginCallback = TFunction<void(bool bLoggedIn, const FString& Error)>;
	// Development login: Nakama device authentication (refused by servers without CROSSGUARD_DEV_AUTH).
	// The device id is -NakamaDeviceId=, else "crossguard-dev-<-CrossguardPlayerId= or this PC's login id>".
	// TODO(Steam): LoginWithSteam: get a Web API auth ticket from the Steam online subsystem
	// (ISteamUser::GetAuthTicketForWebApi, needs OnlineSubsystemSteam, docs/steam-setup.md) and POST it
	// to /v2/account/authenticate/steam?create=true&sync=false.
	void LoginWithDevice(FLoginCallback OnDone);

	UFUNCTION(BlueprintPure, Category = "Crossguard|Online")
	ECrossguardNakamaLoginState GetLoginState() const { return LoginState; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|Online")
	bool IsLoggedIn() const;

	const FString& GetSessionToken() const { return SessionToken; }
	const FString& GetUserId() const { return UserId; }
	const FString& GetUsername() const { return Username; }
	const FString& GetLastError() const { return LastError; }
	// "Logged in as <name>", "Not logged in", "Login failed: ...".
	FString DescribeLogin() const;
	// The device id LoginWithDevice uses.
	static FString GetDevDeviceId();

	// For a login URL (UCrossguardLocalPlayer::GetGameLoginOptions): "NakamaToken=<token>", or empty.
	FString GetLoginOption() const;

private:
	struct FVerifiedSession
	{
		FString UserId;
		double VerifiedAt = 0.0;
	};
	struct FVerifiedTicket
	{
		FString UserId;
		FString PartyId;
		double VerifiedAt = 0.0;
	};
	void PruneVerified();
	void SendRegistration();
	bool HeartbeatTick(float DeltaSeconds);
	// Ticks the HTTP module until bDone or the deadline (shutdown only).
	static void WaitForHttp(const TSharedRef<bool>& bDone, float TimeoutSeconds);

	bool bBackendEnabled = false;
	FString ServerId;
	// Registration (dedicated servers).
	bool bRegistered = false;
	FString RegisteredKind;
	FString RegisteredAddress;
	int32 RegisteredCapacity = 0;
	TFunction<int32()> PlayerCounter;
	FString LastMatchId;
	FTSTicker::FDelegateHandle HeartbeatHandle;
	TMap<FString, FVerifiedTicket> VerifiedTickets;

	FString HttpKey;
	TSharedPtr<FCrossguardNakamaClient> Client;
	TMap<FString, FVerifiedSession> VerifiedSessions;

	ECrossguardNakamaLoginState LoginState = ECrossguardNakamaLoginState::LoggedOut;
	FString SessionToken;
	FString UserId;
	FString Username;
	int64 SessionExpiresAt = 0;
	FString LastError;
};
