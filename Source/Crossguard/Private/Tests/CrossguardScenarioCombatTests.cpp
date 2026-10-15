// Scenario tests: block and parry (docs/next-build-checklist.md section 4, skills table, "Block").
// Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"

namespace CrossguardScenarioCombat
{
	// Data/Items/WeaponTable.json Impact Power: shield 4, dagger 1. CrossguardRules FGuardConfig:
	// the attacker loses by 3 -> the hit is stopped (0% through, a shield deals nothing back) and the
	// attacker is stunned 1 s (StunAtDifference3).
	constexpr float StunSeconds = 1.0f;
	// A and B stand this far apart (melee reach is 220 cm + 60 cm radius).
	constexpr float Gap = 150.0f;
}

// Checklist: "Block: client 1 shield_one_handed off hand + sword_one_handed, hold right click facing
// client 2; client 2 hits with dagger_one_handed -> Log: 'blocked ... 0% through', and client 2 is
// stunned 1 s (Impact Power 4 vs 1). Turn away and get hit: full damage."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioShieldBlock, "Crossguard.Scenario.Combat.ShieldBlock", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioShieldBlock::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioCombat;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A (sword and shield) and B (dagger) face each other"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(0.0f, 3000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* B = C.SpawnPlayer(TEXT("B"), FVector(Gap, 3000.0f, 0.0f), 180.0f);
		if (!C.Require(A != nullptr && B != nullptr, TEXT("A and B spawned")))
		{
			return;
		}
		if (!C.Equip(A, TEXT("sword_one_handed")) || !C.Equip(A, TEXT("shield_one_handed"), ECrossguardEquipmentSlot::OffHand)
			|| !C.Equip(B, TEXT("dagger_one_handed")))
		{
			return;
		}
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Both face each other with level views; A raises the shield"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		if (A == nullptr || B == nullptr)
		{
			C.Fail(TEXT("A or B gone"));
			return;
		}
		// The guard faces A's view; B's eyes are about 23 degrees above that line, inside the guard's
		// 50 degree half angle.
		C.Face(A, 0.0f);
		C.Face(B, 180.0f);
		C.Require(C.RaiseGuard(A), TEXT("A's guard is up (State.Guarding)"));
	});

	// A guard reads the view through the camera manager, updated once a frame: let it catch up.
	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("B stabs the shield: blocked, A unhurt, B stunned"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		if (A == nullptr || B == nullptr)
		{
			C.Fail(TEXT("A or B gone"));
			return;
		}
		C.ClearLog();
		const float Before = C.Health(A);
		if (!C.Require(C.BasicAttack(B), TEXT("B's dagger attack activates")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("A takes nothing through the shield"), C.Health(A), Before, 0.001f);
		C.Test.TestTrue(TEXT("Log says the hit was blocked"), C.LogContains(TEXT("blocked")));
		C.Test.TestTrue(TEXT("Log says 0% went through"), C.LogContains(TEXT("0% through")));
		C.Test.TestTrue(TEXT("B is stunned (State.Stunned)"), C.HasTag(B, CrossguardTags::State_Stunned));
		C.Test.TestTrue(TEXT("A still guards (a shield win does not stun the defender)"), C.HasTag(A, CrossguardTags::State_Guarding));
	});

	Scenario.Wait(StunSeconds * 0.5f);

	Scenario.Step(TEXT("B is still stunned half-way; A turns its back to B"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		if (A == nullptr || B == nullptr)
		{
			C.Fail(TEXT("A or B gone"));
			return;
		}
		C.Test.TestTrue(TEXT("B is still stunned after 0.5 s"), C.HasTag(B, CrossguardTags::State_Stunned));
		C.Face(A, 180.0f);
	});

	// Past the 1 s stun (and the camera has caught up with A's turn).
	Scenario.Wait(StunSeconds * 0.5f + 0.5f);

	Scenario.Step(TEXT("Stun over; B stabs A from behind: full damage"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		if (A == nullptr || B == nullptr)
		{
			C.Fail(TEXT("A or B gone"));
			return;
		}
		C.Test.TestFalse(TEXT("B's stun is over after 1.5 s"), C.HasTag(B, CrossguardTags::State_Stunned));
		C.Test.TestTrue(TEXT("A still guards"), C.HasTag(A, CrossguardTags::State_Guarding));

		C.Face(B, 180.0f);
		const float Before = C.Health(A);
		const float Expected = FMath::Min(C.ExpectedDamage(B, A, C.WeaponValue(B)), Before);
		const int32 BlocksBefore = C.LogCount(TEXT("blocked"));
		if (!C.Require(C.BasicAttack(B), TEXT("B's second dagger attack activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Dagger hit on A's back: %.2f -> %.2f Health (expected -%.2f)"), Before, C.Health(A), Expected));
		C.Test.TestEqual(TEXT("A takes the full dagger hit from behind"), Before - C.Health(A), Expected, 0.05f);
		C.Test.TestTrue(TEXT("The hit from behind did some damage"), Expected > 0.0f);
		C.Test.TestEqual(TEXT("No new 'blocked' log line"), C.LogCount(TEXT("blocked")), BlocksBefore);
		C.Test.TestFalse(TEXT("B is not stunned by an unblocked hit"), C.HasTag(B, CrossguardTags::State_Stunned));
		C.LowerGuard(A);
		C.Test.TestFalse(TEXT("A's guard drops when released"), C.HasTag(A, CrossguardTags::State_Guarding));
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
