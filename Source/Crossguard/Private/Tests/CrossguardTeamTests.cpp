// UCrossguardTeamLibrary on class default objects: no world needed. The world test
// (CrossguardWorldTests.cpp) repeats the checks with spawned monsters, a minion and a character.

#include "Tests/CrossguardTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "Character/CrossguardMonster.h"
#include "Game/CrossguardTeamLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardTeamLibraryTest, "Crossguard.Teams.MonstersAndPlayerless", CROSSGUARD_TEST_FLAGS)

bool FCrossguardTeamLibraryTest::RunTest(const FString& Parameters)
{
	const ACrossguardMonster* Monster = GetDefault<ACrossguardMonster>();
	const ACrossguardCharacter* Character = GetDefault<ACrossguardCharacter>();
	const ACrossguardMinion* Minion = GetDefault<ACrossguardMinion>();

	TestEqual(TEXT("A monster is on MonsterTeamId"), UCrossguardTeamLibrary::GetTeamId(Monster), UCrossguardTeamLibrary::MonsterTeamId);
	TestTrue(TEXT("MonsterTeamId is a valid team (>= 0)"), UCrossguardTeamLibrary::MonsterTeamId >= 0);
	TestEqual(TEXT("A character without a player state has no team"), UCrossguardTeamLibrary::GetTeamId(Character), -1);
	TestEqual(TEXT("A minion without a summoner has no team"), UCrossguardTeamLibrary::GetTeamId(Minion), -1);
	TestEqual(TEXT("No actor has no team"), UCrossguardTeamLibrary::GetTeamId(nullptr), -1);

	TestTrue(TEXT("A monster is its own ally"), UCrossguardTeamLibrary::AreAllies(Monster, Monster));
	TestFalse(TEXT("A monster is not an ally of a player-less character"), UCrossguardTeamLibrary::AreAllies(Monster, Character));
	TestFalse(TEXT("A player-less character is not an ally of a monster"), UCrossguardTeamLibrary::AreAllies(Character, Monster));
	TestFalse(TEXT("A summoner-less minion is not a monster's ally"), UCrossguardTeamLibrary::AreAllies(Monster, Minion));
	TestFalse(TEXT("Null is nobody's ally"), UCrossguardTeamLibrary::AreAllies(Monster, nullptr));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
