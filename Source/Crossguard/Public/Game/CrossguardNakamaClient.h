#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

// A small Nakama REST client on UE's HTTP module (docs/backend-setup.md). No third-party plugin: the
// game needs only a handful of calls. The official nakama-unreal SDK is the option once realtime
// sockets (parties, matchmaker, chat) are needed.
//
// Two kinds of call:
// - Server-to-server (dedicated servers only): Crossguard RPCs (Backend/nakama/modules) authorized
//   with the runtime HTTP key, sent as HTTP Basic auth ("<key>:"), never in the URL. The key comes from
//   the server's command line or environment (UCrossguardNakamaSubsystem) and never ships to clients.
// - Client login: device-id authentication with the (public) server key.
//
// Completion callbacks run on the game thread (the HTTP module's default).

// The answer to one call.
struct CROSSGUARD_API FCrossguardNakamaResponse
{
	// An HTTP response arrived (false: no connection, timeout).
	bool bConnected = false;
	int32 HttpStatus = 0;
	FString Body;
	// Error answers: Nakama's gRPC code ({"code": N, "message": ...}; 10 = ABORTED, the save version
	// conflict) and message. 0 / empty on success.
	int32 ErrorCode = 0;
	FString ErrorMessage;

	bool IsOk() const { return bConnected && HttpStatus >= 200 && HttpStatus < 300; }
	// Worth trying again: no answer, a server error (5xx) or too many requests (429).
	bool IsRetryable() const;
	// For logs: "HTTP 409 (code 10): version conflict ...", "no response (timeout or connection refused)".
	FString Describe() const;
};

class CROSSGUARD_API FCrossguardNakamaClient
{
public:
	using FCallback = TFunction<void(const FCrossguardNakamaResponse& Response)>;

	// gRPC status code for a refused save write (stale version): HTTP 409.
	static constexpr int32 CodeAborted = 10;

	FCrossguardNakamaClient(const FString& InBaseUrl, float InTimeoutSeconds);

	const FString& GetBaseUrl() const { return BaseUrl; }

	// POST <url>/v2/rpc/<RpcId>?unwrap with the raw JSON payload, authorized with the runtime HTTP key.
	void CallServerRpc(const FString& HttpKey, const FString& RpcId, const FString& PayloadJson, FCallback OnDone) const;

	// POST <url>/v2/account/authenticate/device?create=true, authorized with the server key. The answer
	// holds "token" and "refresh_token".
	void AuthenticateDevice(const FString& ServerKey, const FString& DeviceId, FCallback OnDone) const;

	// --- Helpers (pure; unit tested in CrossguardNakamaTests.cpp) ---

	// A JSON string literal, quotes included.
	static FString Quote(const FString& Value);
	// A Nakama user id: a lowercase UUID ("1f2e...-...").
	static bool IsUserId(const FString& Value);
	// "Basic base64(<Key>:)".
	static FString BasicAuth(const FString& Key);
	// The claims of a Nakama session token (not verified: the client reads its own token; servers
	// verify with crossguard_verify_session). False if it is not a readable JWT.
	static bool ReadTokenClaims(const FString& Token, FString& OutUserId, FString& OutUsername, int64& OutExpiresAt);
	// Fills ErrorCode / ErrorMessage from an error body.
	static void ParseError(FCrossguardNakamaResponse& Response);

private:
	void Post(const FString& Url, const FString& Authorization, const FString& Body, FCallback OnDone) const;

	FString BaseUrl;
	float TimeoutSeconds = 10.0f;
};
