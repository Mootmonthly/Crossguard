#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/Function.h"

#include "CrossguardNakamaSubsystem.generated.h"

class FCrossguardNakamaClient;

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
UCLASS(Config = Game)
class CROSSGUARD_API UCrossguardNakamaSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Null without a game instance.
	static UCrossguardNakamaSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

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
	void PruneVerified();

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
