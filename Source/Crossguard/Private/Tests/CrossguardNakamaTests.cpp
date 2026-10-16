// The Nakama client's and save backend's pure helpers (docs/backend-setup.md): no network, no world.
// The answers parsed here are the exact shapes the Go module writes (Backend/nakama/modules; its tests
// pin the field order, TestLoadResponseFieldOrder). End-to-end checks against a running Nakama:
// Tools/backend_smoke.py.

#include "Tests/CrossguardTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Game/CrossguardNakamaClient.h"
#include "Game/CrossguardNakamaSaveBackend.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CrossguardNakamaTestsLocal
{
	const TCHAR* Alice = TEXT("11111111-2222-3333-4444-555555555555");

	FString Base64Url(const FString& Text)
	{
		return FBase64::Encode(Text).Replace(TEXT("+"), TEXT("-")).Replace(TEXT("/"), TEXT("_")).Replace(TEXT("="), TEXT(""));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNakamaHelpersTest, "Crossguard.Nakama.Helpers", CROSSGUARD_TEST_FLAGS)

bool FCrossguardNakamaHelpersTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardNakamaTestsLocal;

	TestEqual(TEXT("Quote escapes quotes, backslashes and control characters"), FCrossguardNakamaClient::Quote(TEXT("a\"b\\c\nd\x01")),
		FString(TEXT("\"a\\\"b\\\\c\\nd\\u0001\"")));
	TestTrue(TEXT("A Nakama user id"), FCrossguardNakamaClient::IsUserId(Alice));
	TestFalse(TEXT("Uppercase is not one"), FCrossguardNakamaClient::IsUserId(TEXT("11111111-2222-3333-4444-55555555555A")));
	TestFalse(TEXT("A development id is not one"), FCrossguardNakamaClient::IsUserId(TEXT("dev-alice")));
	TestFalse(TEXT("A Steam id is not one"), FCrossguardNakamaClient::IsUserId(TEXT("76561198000000001")));
	TestEqual(TEXT("Basic auth with an empty password"), FCrossguardNakamaClient::BasicAuth(TEXT("key")), FString(TEXT("Basic a2V5Og==")));

	// Token claims (Nakama: tid, uid, usn, exp), unpadded base64url.
	const FString Token = Base64Url(TEXT("{\"alg\":\"HS256\",\"typ\":\"JWT\"}")) + TEXT(".")
		+ Base64Url(FString::Printf(TEXT("{\"tid\":\"t\",\"uid\":\"%s\",\"usn\":\"alice?\",\"exp\":1999999999}"), Alice)) + TEXT(".sig");
	FString UserId;
	FString Username;
	int64 ExpiresAt = 0;
	TestTrue(TEXT("Token claims read"), FCrossguardNakamaClient::ReadTokenClaims(Token, UserId, Username, ExpiresAt));
	TestEqual(TEXT("uid"), UserId, FString(Alice));
	TestEqual(TEXT("usn"), Username, FString(TEXT("alice?")));
	TestEqual(TEXT("exp"), ExpiresAt, static_cast<int64>(1999999999));
	TestFalse(TEXT("Not a token"), FCrossguardNakamaClient::ReadTokenClaims(TEXT("abc"), UserId, Username, ExpiresAt));

	// Error answers.
	FCrossguardNakamaResponse Conflict;
	Conflict.bConnected = true;
	Conflict.HttpStatus = 409;
	Conflict.Body = TEXT("{\"code\":10,\"error\":{\"Message\":\"version conflict\",\"Code\":10},\"message\":\"version conflict: the save changed\"}");
	FCrossguardNakamaClient::ParseError(Conflict);
	TestEqual(TEXT("Error code read"), Conflict.ErrorCode, FCrossguardNakamaClient::CodeAborted);
	TestEqual(TEXT("Error message read"), Conflict.ErrorMessage, FString(TEXT("version conflict: the save changed")));
	TestFalse(TEXT("A conflict is not retried"), Conflict.IsRetryable());
	FCrossguardNakamaResponse NoAnswer;
	TestTrue(TEXT("No answer is retried"), NoAnswer.IsRetryable());
	FCrossguardNakamaResponse ServerError;
	ServerError.bConnected = true;
	ServerError.HttpStatus = 500;
	TestTrue(TEXT("A server error is retried"), ServerError.IsRetryable());

	TestEqual(TEXT("Retry 1 after 1 s"), UCrossguardNakamaSaveBackend::RetryDelaySeconds(1), 1.0f);
	TestEqual(TEXT("Retry 5 after 16 s"), UCrossguardNakamaSaveBackend::RetryDelaySeconds(5), 16.0f);
	TestEqual(TEXT("Retries cap at 30 s"), UCrossguardNakamaSaveBackend::RetryDelaySeconds(12), 30.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNakamaSavePayloadTest, "Crossguard.Nakama.SavePayloads", CROSSGUARD_TEST_FLAGS)

bool FCrossguardNakamaSavePayloadTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardNakamaTestsLocal;
	const FString Document = FString::Printf(TEXT("{\"version\": 1, \"playerId\": \"%s\", \"gold\": 4321, \"affix\": 0.03}"), Alice);

	// The write payload embeds the document verbatim (numbers untouched) and is valid JSON.
	const FString Payload = UCrossguardNakamaSaveBackend::BuildWritePayload(Alice, TEXT("main"), Document, TEXT("*"));
	TestTrue(TEXT("Document embedded verbatim"), Payload.Contains(Document));
	TSharedPtr<FJsonObject> Parsed;
	TestTrue(TEXT("Write payload is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Payload), Parsed) && Parsed.IsValid());
	if (Parsed.IsValid())
	{
		TestEqual(TEXT("user_id"), Parsed->GetStringField(TEXT("user_id")), FString(Alice));
		TestEqual(TEXT("expected_version"), Parsed->GetStringField(TEXT("expected_version")), FString(TEXT("*")));
		TestTrue(TEXT("record is an object"), Parsed->HasTypedField<EJson::Object>(TEXT("record")));
	}

	// Load answers, as the module writes them (record last; its spacing does not matter).
	bool bFound = true;
	FString Version;
	FString Record;
	TestTrue(TEXT("New character answer"), UCrossguardNakamaSaveBackend::ParseLoadResponse(TEXT("{\"found\":false,\"version\":\"*\",\"record\":null}"), bFound, Version, Record));
	TestFalse(TEXT("New character: not found"), bFound);
	TestEqual(TEXT("New character: version *"), Version, FString(TEXT("*")));
	TestTrue(TEXT("New character: no record"), Record.IsEmpty());

	const FString Stored = FString::Printf(TEXT("{\"gold\": 4321, \"affix\": 0.03, \"version\": 1, \"playerId\": \"%s\", \"stash\": {\"tabs\": [{\"items\": []}]}}"), Alice);
	TestTrue(TEXT("Found answer"), UCrossguardNakamaSaveBackend::ParseLoadResponse(
		FString::Printf(TEXT("{\"found\":true,\"version\":\"ca17727a51c632e908dee6498ae9fb05\",\"record\":%s}"), *Stored), bFound, Version, Record));
	TestTrue(TEXT("Found"), bFound);
	TestEqual(TEXT("Version"), Version, FString(TEXT("ca17727a51c632e908dee6498ae9fb05")));
	TestEqual(TEXT("Record text exactly as stored"), Record, Stored);

	TestFalse(TEXT("Garbage refused"), UCrossguardNakamaSaveBackend::ParseLoadResponse(TEXT("<html>"), bFound, Version, Record));
	TestFalse(TEXT("Found without a version refused"),
		UCrossguardNakamaSaveBackend::ParseLoadResponse(TEXT("{\"found\":true,\"version\":\"\",\"record\":{}}"), bFound, Version, Record));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
