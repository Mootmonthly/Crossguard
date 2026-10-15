// Scenario tests: raids (docs/next-build-checklist.md section 4, first table). Framework:
// CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardCharacter.h"
#include "Game/CrossguardPlayerState.h"
#include "Items/CrossguardLootContainer.h"

namespace CrossguardScenarioRaid
{
	// CrossguardRules FXpConfig::BasicMonster: what killing a non-player character gives (its
	// KillXpSource defaults to BasicMonster), held as raid XP during a raid.
	constexpr int32 BasicMonsterXp = 5;
	// Sword: 1 attack per second at 0 Action Speed; generous for a level 1 dummy.
	constexpr float KillTimeoutSeconds = 30.0f;
	// A body is spawned at the character's transform when it dies.
	constexpr float BodySearchRadius = 300.0f;
}

// Checklist: "Kill the dummy -> It ragdolls and leaves a body; you get 5 XP (raid XP, GetRaidXp)."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioKillDummyLeavesBody, "Crossguard.Scenario.Raid.KillDummyLeavesBody", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioKillDummyLeavesBody::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioRaid;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a one-handed sword faces a dummy 1.5 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150.0f, 1000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("player and dummy spawned")) || !C.Equip(Player, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Aim(Player, Dummy);

		const ACrossguardPlayerState* Progression = C.PlayerState(Player);
		if (!C.Require(Progression != nullptr, TEXT("the player has an ACrossguardPlayerState")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The player is in a raid"), Progression->IsInRaid());
		C.Test.TestFalse(TEXT("The dummy is not player-controlled"), Dummy->IsPlayerControlled());
		C.SetNumber(TEXT("RaidXpBefore"), Progression->GetRaidXp());
	});

	// Let both land on the floor.
	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The first swing takes the damage formula's amount"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("player or dummy gone"));
			return;
		}
		C.Aim(Player, Dummy);
		const float Before = C.Health(Dummy);
		const float Expected = FMath::Min(C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player)), Before);
		if (!C.Require(C.BasicAttack(Player), TEXT("the sword attack activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Sword hit: %.2f -> %.2f Health (expected -%.2f)"), Before, C.Health(Dummy), Expected));
		C.Test.TestEqual(TEXT("Damage of the first sword hit"), Before - C.Health(Dummy), Expected, 0.05f);
	});

	// Attacks fail while the previous one recovers (the attack interval), so just keep trying.
	Scenario.WaitUntil(TEXT("the dummy dies from sword hits"), KillTimeoutSeconds, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("player or dummy gone"));
			return false;
		}
		if (!C.IsDead(Dummy))
		{
			C.Aim(Player, Dummy);
			C.BasicAttack(Player);
		}
		return C.IsDead(Dummy);
	}, 0.1f);

	Scenario.Step(TEXT("Dead, a body lies there, 5 raid XP"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("The dummy has 0 Health"), C.Health(Dummy), 0.0f, 0.001f);
		C.Test.TestTrue(TEXT("The dummy has State.Dead"), C.IsDead(Dummy));
		C.Test.TestNotNull(TEXT("A body (loot container) lies where the dummy died"),
			C.FindNearest<ACrossguardLootContainer>(Dummy->GetActorLocation(), BodySearchRadius));

		const ACrossguardPlayerState* Progression = C.PlayerState(Player);
		if (C.Require(Progression != nullptr, TEXT("the player still has its player state")))
		{
			C.Test.TestEqual(TEXT("Raid XP gained for the kill"), Progression->GetRaidXp() - static_cast<int32>(C.Number(TEXT("RaidXpBefore"))),
				BasicMonsterXp);
		}
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
