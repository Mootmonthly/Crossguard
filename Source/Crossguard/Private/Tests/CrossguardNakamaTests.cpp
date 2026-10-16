// The Nakama client's, save backend's, matchmaking's and party mirror's pure helpers
// (docs/backend-setup.md): no network, no world.
// The answers parsed here are the exact shapes the Go module writes (Backend/nakama/modules; its tests
// pin the field order, TestLoadResponseFieldOrder). End-to-end checks against a running Nakama:
// Tools/backend_smoke.py.

#include "Tests/CrossguardTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Game/CrossguardMatchmaking.h"
#include "Game/CrossguardNakamaClient.h"
#include "Game/CrossguardNakamaMatchmaking.h"
#include "Game/CrossguardNakamaSaveBackend.h"
#include "Game/CrossguardNakamaSubsystem.h"
#include "Game/CrossguardPartySubsystem.h"
#include "HAL/PlatformTime.h"
#include "Misc/Base64.h"
#include "UObject/Package.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CrossguardNakamaTestsLocal
{
	const TCHAR* Alice = TEXT("11111111-2222-3333-4444-555555555555");
	const TCHAR* Bob = TEXT("66666666-7777-8888-9999-aaaaaaaaaaaa");

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

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
	const FString Payload = UCrossguardNakamaSaveBackend::BuildWritePayload(Alice, TEXT("main"), Document, TEXT("*"), TEXT("lease-token-1"));
	TestTrue(TEXT("Document embedded verbatim"), Payload.Contains(Document));
	TSharedPtr<FJsonObject> Parsed;
	TestTrue(TEXT("Write payload is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Payload), Parsed) && Parsed.IsValid());
	if (Parsed.IsValid())
	{
		TestEqual(TEXT("user_id"), Parsed->GetStringField(TEXT("user_id")), FString(Alice));
		TestEqual(TEXT("expected_version"), Parsed->GetStringField(TEXT("expected_version")), FString(TEXT("*")));
		TestEqual(TEXT("lease_token"), Parsed->GetStringField(TEXT("lease_token")), FString(TEXT("lease-token-1")));
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

	TestEqual(TEXT("savedAt value dropped"), UCrossguardNakamaSaveBackend::WithoutSavedAt(TEXT("{\"version\":1,\"savedAt\": 1760000000,\"gold\":1}")),
		FString(TEXT("{\"version\":1,\"savedAt\",\"gold\":1}")));
	TestEqual(TEXT("Two saves differing only in savedAt compare equal"), UCrossguardNakamaSaveBackend::WithoutSavedAt(TEXT("{\"savedAt\":1,\"gold\":1}")),
		UCrossguardNakamaSaveBackend::WithoutSavedAt(TEXT("{\"savedAt\":2,\"gold\":1}")));
	TestEqual(TEXT("No savedAt: unchanged"), UCrossguardNakamaSaveBackend::WithoutSavedAt(TEXT("{\"gold\":1}")), FString(TEXT("{\"gold\":1}")));

	TestFalse(TEXT("Garbage refused"), UCrossguardNakamaSaveBackend::ParseLoadResponse(TEXT("<html>"), bFound, Version, Record));
	TestFalse(TEXT("Found without a version refused"),
		UCrossguardNakamaSaveBackend::ParseLoadResponse(TEXT("{\"found\":true,\"version\":\"\",\"record\":{}}"), bFound, Version, Record));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNakamaLeaseTest, "Crossguard.Nakama.Leases", CROSSGUARD_TEST_FLAGS)

bool FCrossguardNakamaLeaseTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardNakamaTestsLocal;
	const TSharedPtr<FJsonObject> Lease = ParseJson(UCrossguardNakamaSaveBackend::BuildLeasePayload(Alice, TEXT("main"), TEXT("hub-1"), TEXT("tok"), 60));
	if (TestTrue(TEXT("Lease payload is JSON"), Lease.IsValid()))
	{
		TestEqual(TEXT("user_id"), Lease->GetStringField(TEXT("user_id")), FString(Alice));
		TestEqual(TEXT("server_id"), Lease->GetStringField(TEXT("server_id")), FString(TEXT("hub-1")));
		TestEqual(TEXT("lease_token"), Lease->GetStringField(TEXT("lease_token")), FString(TEXT("tok")));
		TestEqual(TEXT("ttl_sec"), static_cast<int32>(Lease->GetNumberField(TEXT("ttl_sec"))), 60);
	}

	// The module's refusal when the lease is gone (HTTP 400, FAILED_PRECONDITION), as Nakama sends it.
	FCrossguardNakamaResponse Lost;
	Lost.bConnected = true;
	Lost.HttpStatus = 400;
	Lost.Body = TEXT("{\"code\":9,\"error\":{\"Message\":\"save lease not held\",\"Code\":9},\"message\":\"save lease not held: it expired, was released or another server took it\"}");
	FCrossguardNakamaClient::ParseError(Lost);
	TestTrue(TEXT("Lease lost recognised"), UCrossguardNakamaSaveBackend::IsLeaseLost(Lost));
	TestFalse(TEXT("A lost lease is not retried"), Lost.IsRetryable());
	FCrossguardNakamaResponse BadRecord;
	BadRecord.bConnected = true;
	BadRecord.HttpStatus = 400;
	BadRecord.Body = TEXT("{\"code\":3,\"message\":\"record has no \\\"version\\\"\"}");
	FCrossguardNakamaClient::ParseError(BadRecord);
	TestFalse(TEXT("Another 400 is not a lost lease"), UCrossguardNakamaSaveBackend::IsLeaseLost(BadRecord));
	TestFalse(TEXT("No answer is not a lost lease"), UCrossguardNakamaSaveBackend::IsLeaseLost(FCrossguardNakamaResponse()));

	// Server ids: only letters, digits, '.', '-', '_'.
	TestEqual(TEXT("Server id from parts"), UCrossguardNakamaSubsystem::MakeServerId(TEXT("DEV PC#1"), 4242, TEXT("ab12cd34")),
		FString(TEXT("DEVPC1-4242-ab12cd34")));
	TestEqual(TEXT("Server id as given"), UCrossguardNakamaSubsystem::MakeServerId(TEXT("hub-1.eu"), 0, FString()), FString(TEXT("hub-1.eu")));
	TestEqual(TEXT("Server id capped"), UCrossguardNakamaSubsystem::MakeServerId(FString::ChrN(200, TEXT('a')), 0, FString()).Len(), 128);

	const TSharedPtr<FJsonObject> Register = ParseJson(UCrossguardNakamaSubsystem::BuildRegisterPayload(TEXT("m-1"), TEXT("match"), TEXT("127.0.0.1:7778"), 48));
	if (TestTrue(TEXT("Register payload is JSON"), Register.IsValid()))
	{
		TestEqual(TEXT("kind"), Register->GetStringField(TEXT("kind")), FString(TEXT("match")));
		TestEqual(TEXT("address"), Register->GetStringField(TEXT("address")), FString(TEXT("127.0.0.1:7778")));
		TestEqual(TEXT("capacity"), static_cast<int32>(Register->GetNumberField(TEXT("capacity"))), 48);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNakamaMatchmakingTest, "Crossguard.Nakama.Matchmaking", CROSSGUARD_TEST_FLAGS)

bool FCrossguardNakamaMatchmakingTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardNakamaTestsLocal;
	FCrossguardMatchRequest Request;
	Request.PartyKey = TEXT("79594019a4ca06249b69e8b9fe785c55");
	Request.PlayerIds = {Alice, Bob};
	TestEqual(TEXT("Search key: the party"), Request.SearchKey(), Request.PartyKey);
	const TSharedPtr<FJsonObject> Payload = ParseJson(FCrossguardNakamaMatchmaking::BuildRequestPayload(Request, TEXT("hub-1")));
	if (TestTrue(TEXT("Request payload is JSON"), Payload.IsValid()))
	{
		TestEqual(TEXT("party_id"), Payload->GetStringField(TEXT("party_id")), Request.PartyKey);
		TestEqual(TEXT("two user_ids"), Payload->GetArrayField(TEXT("user_ids")).Num(), 2);
		TestEqual(TEXT("server_id"), Payload->GetStringField(TEXT("server_id")), FString(TEXT("hub-1")));
	}
	FCrossguardMatchRequest Solo;
	Solo.PlayerIds = {Alice};
	TestEqual(TEXT("Search key: the solo player"), Solo.SearchKey(), FString(Alice));

	// Answers exactly as the module writes them (taken from a live run).
	FString State;
	FString RequestId;
	FCrossguardMatchAssignment Assignment;
	TestTrue(TEXT("Queued answer"), FCrossguardNakamaMatchmaking::ParseStatus(
		TEXT("{\"request_id\":\"8dbeb88e708b2be5ecb53bcf65c5196f\",\"state\":\"queued\",\"waited_sec\":0}"), State, RequestId, Assignment));
	TestEqual(TEXT("Queued"), State, FString(TEXT("queued")));
	TestEqual(TEXT("Request id"), RequestId, FString(TEXT("8dbeb88e708b2be5ecb53bcf65c5196f")));
	TestTrue(TEXT("Queued: not assigned"), Assignment.Status != ECrossguardMatchStatus::Assigned);

	const FString Assigned = FString::Printf(TEXT("{\"request_id\":\"9a6c\",\"state\":\"assigned\",\"waited_sec\":0,\"match_id\":\"fe0f\",")
		TEXT("\"server_id\":\"m-shape\",\"address\":\"127.0.0.1:19999\",\"tickets\":{\"%s\":\"d9b88179cfed7db0f333fd83b7db505d\",\"%s\":\"e0\"}}"), Alice, Bob);
	TestTrue(TEXT("Assigned answer"), FCrossguardNakamaMatchmaking::ParseStatus(Assigned, State, RequestId, Assignment));
	TestTrue(TEXT("Assigned"), Assignment.Status == ECrossguardMatchStatus::Assigned);
	TestEqual(TEXT("Address"), Assignment.ServerAddress, FString(TEXT("127.0.0.1:19999")));
	TestEqual(TEXT("Match id"), Assignment.MatchId, FString(TEXT("fe0f")));
	TestEqual(TEXT("Alice's ticket"), Assignment.TicketFor(Alice), FString(TEXT("d9b88179cfed7db0f333fd83b7db505d")));
	TestEqual(TEXT("Bob's ticket"), Assignment.TicketFor(Bob), FString(TEXT("e0")));
	TestTrue(TEXT("Nobody else's"), Assignment.TicketFor(TEXT("someone")).IsEmpty());
	TestEqual(TEXT("Travel URL with Alice's ticket"), UCrossguardMatchmakingSubsystem::BuildMatchTravelUrl(Assignment.ServerAddress, Assignment.TicketFor(Alice)),
		FString(TEXT("127.0.0.1:19999?MatchTicket=d9b88179cfed7db0f333fd83b7db505d")));
	TestFalse(TEXT("Assigned without an address refused"), FCrossguardNakamaMatchmaking::ParseStatus(
		TEXT("{\"request_id\":\"x\",\"state\":\"assigned\",\"tickets\":{}}"), State, RequestId, Assignment));
	TestFalse(TEXT("Error body refused"), FCrossguardNakamaMatchmaking::ParseStatus(
		TEXT("{\"code\":5,\"message\":\"no such match request\"}"), State, RequestId, Assignment));

	TestEqual(TEXT("Hub address"), FCrossguardNakamaMatchmaking::ParseHubAddress(TEXT("{\"server_id\":\"hub-1\",\"address\":\"127.0.0.1:7777\"}")),
		FString(TEXT("127.0.0.1:7777")));
	TestTrue(TEXT("No hub"), FCrossguardNakamaMatchmaking::ParseHubAddress(TEXT("{\"code\":5,\"message\":\"no hub server with room\"}")).IsEmpty());

	FString UserId;
	FString PartyId;
	FString MatchId;
	TestTrue(TEXT("Ticket answer"), UCrossguardNakamaSubsystem::ParseTicketAnswer(
		FString::Printf(TEXT("{\"user_id\":\"%s\",\"party_id\":\"\",\"match_id\":\"fe0f\",\"expires_at_ms\":1792184358967}"), Alice), UserId, PartyId, MatchId));
	TestEqual(TEXT("Ticket user"), UserId, FString(Alice));
	TestTrue(TEXT("Solo ticket: no party"), PartyId.IsEmpty());
	TestEqual(TEXT("Ticket match"), MatchId, FString(TEXT("fe0f")));
	TestFalse(TEXT("Ticket error refused"), UCrossguardNakamaSubsystem::ParseTicketAnswer(TEXT("{\"code\":16,\"message\":\"match ticket has expired\"}"), UserId, PartyId, MatchId));

	// Verified tickets are kept per ticket and player, once.
	// A game instance subsystem needs a game instance outer (an uninitialised one: no world).
	UCrossguardNakamaSubsystem* Nakama = NewObject<UCrossguardNakamaSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	Nakama->AddVerifiedTicketForTesting(TEXT("t1"), Alice, TEXT("p1"));
	TestFalse(TEXT("Another player's ticket"), Nakama->TakeVerifiedTicket(TEXT("t1"), Bob, PartyId));
	Nakama->AddVerifiedTicketForTesting(TEXT("t1"), Alice, TEXT("p1"));
	TestTrue(TEXT("The ticket's party"), Nakama->TakeVerifiedTicket(TEXT("t1"), Alice, PartyId) && PartyId == TEXT("p1"));
	TestFalse(TEXT("Taken once"), Nakama->TakeVerifiedTicket(TEXT("t1"), Alice, PartyId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNakamaPartyStateTest, "Crossguard.Nakama.PartyState", CROSSGUARD_TEST_FLAGS)

bool FCrossguardNakamaPartyStateTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardNakamaTestsLocal;
	TestTrue(TEXT("None"), UCrossguardPartySubsystem::ResultFromName(TEXT("None")) == ECrossguardPartyResult::None);
	TestTrue(TEXT("NotLeader"), UCrossguardPartySubsystem::ResultFromName(TEXT("NotLeader")) == ECrossguardPartyResult::NotLeader);
	TestTrue(TEXT("InviteExpired"), UCrossguardPartySubsystem::ResultFromName(TEXT("InviteExpired")) == ECrossguardPartyResult::InviteExpired);
	TestTrue(TEXT("Unknown name"), UCrossguardPartySubsystem::ResultFromName(TEXT("Dance")) == ECrossguardPartyResult::BackendUnavailable);
	TestTrue(TEXT("Empty name"), UCrossguardPartySubsystem::ResultFromName(FString()) == ECrossguardPartyResult::BackendUnavailable);

	// The registry here mirrors the backend's answers (a bare game instance, no world: no players to bring up to date).
	UCrossguardPartySubsystem* Parties = NewObject<UCrossguardPartySubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	const FString PartyId = TEXT("79594019a4ca06249b69e8b9fe785c55");
	// An invite (live run's shape): Alice founded the party, Bob has her invite for 180 s.
	const FString Invited = FString::Printf(TEXT("{\"result\":\"None\",\"party_id\":\"%s\",\"now_ms\":1792180158747,\"players\":{")
		TEXT("\"%s\":{\"party_id\":\"%s\",\"invites\":[]},")
		TEXT("\"%s\":{\"party_id\":\"\",\"invites\":[{\"party_id\":\"%s\",\"invited_by\":\"%s\",\"expires_at_ms\":1792180338747}]}},")
		TEXT("\"parties\":{\"%s\":{\"id\":\"%s\",\"leader_id\":\"%s\",\"members\":[\"%s\"],\"hub_server_id\":\"hub-1\"}}}"),
		*PartyId, Alice, *PartyId, Bob, *PartyId, Alice, *PartyId, *PartyId, Alice, Alice);
	TestEqual(TEXT("Invite answer's result"), Parties->ApplyBackendState(Invited), FString(TEXT("None")));
	const Crossguard::FPartyRegistry& Registry = Parties->GetRegistry();
	const std::string AliceId = TCHAR_TO_UTF8(Alice);
	const std::string BobId = TCHAR_TO_UTF8(Bob);
	TestTrue(TEXT("Alice leads the party"), Registry.IsLeader(AliceId) && Parties->PartyKeyOf(Alice) == PartyId);
	TestTrue(TEXT("Bob in none"), Registry.PartyOf(BobId) == nullptr);
	const double Clock = FPlatformTime::Seconds();
	const std::vector<const Crossguard::FParty*> Inbox = Registry.InvitesFor(BobId, Clock);
	TestEqual(TEXT("Bob has one invite"), static_cast<int32>(Inbox.size()), 1);
	TestTrue(TEXT("It lasts about 180 s here"), Inbox.size() == 1 && FMath::Abs(Inbox[0]->Invites[0].ExpiresAt - Clock - 180.0) < 5.0);

	// Accepted: both members; Bob's inbox empty.
	const FString Accepted = FString::Printf(TEXT("{\"result\":\"None\",\"party_id\":\"%s\",\"now_ms\":1792180158833,\"players\":{")
		TEXT("\"%s\":{\"party_id\":\"%s\",\"invites\":[]},\"%s\":{\"party_id\":\"%s\",\"invites\":[]}},")
		TEXT("\"parties\":{\"%s\":{\"id\":\"%s\",\"leader_id\":\"%s\",\"members\":[\"%s\",\"%s\"]}}}"),
		*PartyId, Alice, *PartyId, Bob, *PartyId, *PartyId, *PartyId, Alice, Alice, Bob);
	Parties->ApplyBackendState(Accepted);
	TestEqual(TEXT("Bob is a member"), Parties->PartyKeyOf(Bob), PartyId);
	TestTrue(TEXT("Bob's inbox is empty"), Registry.InvitesFor(BobId, Clock).empty());

	// A refusal comes back by name and changes nothing.
	TestEqual(TEXT("Refusal by name"), Parties->ApplyBackendState(TEXT("{\"result\":\"NotLeader\",\"now_ms\":1,\"players\":{},\"parties\":{}}")),
		FString(TEXT("NotLeader")));
	TestEqual(TEXT("Still two members"), static_cast<int32>(Registry.Find(TCHAR_TO_UTF8(*PartyId))->Members.size()), 2);

	// A get saying Alice is in no party any more (left on another hub) drops her here; Bob leads.
	Parties->ApplyBackendState(FString::Printf(TEXT("{\"now_ms\":1,\"players\":{\"%s\":{\"party_id\":\"\",\"invites\":[]}},\"parties\":{}}"), Alice));
	TestTrue(TEXT("Alice dropped"), Parties->PartyKeyOf(Alice).IsEmpty());
	TestTrue(TEXT("Bob leads what is left"), Registry.IsLeader(BobId));
	// Disbanded.
	Parties->ApplyBackendState(FString::Printf(TEXT("{\"result\":\"None\",\"party_id\":\"%s\",\"disbanded\":true,\"now_ms\":1,\"players\":{\"%s\":{\"party_id\":\"\",\"invites\":[]}},\"parties\":{}}"),
		*PartyId, Bob));
	TestTrue(TEXT("Gone"), Registry.Find(TCHAR_TO_UTF8(*PartyId)) == nullptr && Parties->PartyKeyOf(Bob).IsEmpty());
	TestEqual(TEXT("Garbage"), Parties->ApplyBackendState(TEXT("<html>")), FString(TEXT("<unreadable>")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
