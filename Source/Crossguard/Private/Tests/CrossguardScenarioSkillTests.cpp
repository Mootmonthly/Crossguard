// Scenario tests: skills (docs/next-build-checklist.md section 4, skills table). Framework:
// CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"

namespace CrossguardScenarioSkills
{
	// Data/Skills/Skills.json, fireball: impact 50 Magic Damage, coefficient 0.5 (also
	// UCrossguardFireballAbility::LaunchFireball). Splash 15 within 2 m, not on the direct target.
	constexpr float FireballDamage = 50.0f;
	constexpr float FireballCoefficient = 0.5f;
	// Flies at 30 m/s: 6 m takes 0.2 s.
	constexpr float FireballFlightWait = 1.0f;
}

// Checklist: "fireball at the dummy -> 50+ damage on hit" (and the 15 s cooldown starts).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioFireball, "Crossguard.Scenario.Skills.Fireball", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioFireball::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a wand and Fireball, a dummy 6 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Caster"), FVector(0.0f, 2000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(600.0f, 2000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("caster and dummy spawned")))
		{
			return;
		}
		// Spells need a wand or a two-handed staff in hand.
		if (!C.Equip(Player, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(C.GrantSkills(Player, {TEXT("fireball")}), TEXT("Fireball granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Cast Fireball at the dummy"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Caster"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("caster or dummy gone"));
			return;
		}
		C.Aim(Player, Dummy);
		C.SetNumber(TEXT("DummyBefore"), C.Health(Dummy));
		C.SetNumber(TEXT("CasterBefore"), C.Health(Player));
		C.SetNumber(TEXT("Expected"), C.ExpectedDamage(Player, Dummy, 0.0f, FireballDamage, FireballCoefficient));
		C.SetNumber(TEXT("DummyMR"), C.Stat(Dummy, UCrossguardAttributeSet::GetMagicResistanceAttribute()));
		C.Test.TestFalse(TEXT("Fireball is ready before the cast"), C.IsOnCooldown(Player, TEXT("fireball")));
		if (!C.Require(C.ActivateSkill(Player, TEXT("fireball")), TEXT("Fireball activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Fireball is on cooldown after the cast"), C.IsOnCooldown(Player, TEXT("fireball")));
		C.Test.TestTrue(TEXT("The caster has Cooldown.Skill.Fireball"), C.HasTag(Player, CrossguardTags::Cooldown_Skill_Fireball));
	});

	Scenario.Wait(FireballFlightWait);

	Scenario.Step(TEXT("The dummy took the impact damage; the caster nothing"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Caster"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		const float Before = static_cast<float>(C.Number(TEXT("DummyBefore")));
		const float Taken = Before - C.Health(Dummy);
		const float Expected = FMath::Min(static_cast<float>(C.Number(TEXT("Expected"))), Before);
		C.Test.AddInfo(FString::Printf(TEXT("Fireball: dummy %.2f -> %.2f Health (expected -%.2f)"), Before, C.Health(Dummy), Expected));
		C.Test.TestEqual(TEXT("Fireball impact damage on the dummy"), Taken, Expected, 0.05f);
		// 50 before the dummy's Magic Resistance (the caster's Magic Damage Bonus only adds).
		const float FloorBeforeResistance = FireballDamage * FMath::Max(0.0f, 1.0f - static_cast<float>(C.Number(TEXT("DummyMR"))));
		if (Before > FloorBeforeResistance)
		{
			C.Test.TestTrue(TEXT("At least 50 damage before resistance"), Taken >= FloorBeforeResistance - 0.05f);
		}
		C.Test.TestEqual(TEXT("The caster 6 m away is outside the 2 m splash"), C.Health(Player),
			static_cast<float>(C.Number(TEXT("CasterBefore"))), 0.001f);
		C.Test.TestTrue(TEXT("Fireball is still on cooldown 1 s later"), C.IsOnCooldown(Player, TEXT("fireball")));
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
