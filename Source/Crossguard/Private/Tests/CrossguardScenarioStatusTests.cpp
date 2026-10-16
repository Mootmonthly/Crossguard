// Scenario tests: status effects not covered elsewhere (Root).
// Framework: CrossguardScenario.h, docs/editor-tests.md.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace CrossguardScenarioStatus
{
	constexpr float RootTestSeconds = 1.0f;
}

// Root (design-decisions.md, "Root"): Movement Speed × 0, no jumping, no movement skills (Hunter's
// Leap, Like A Lion, Flight); attacks and other skills still work. Crowd control.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRootStopsMovement, "Crossguard.Scenario.Status.Root", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRootStopsMovement::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioStatus;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A with a sword and Hunter's Leap"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(0.0f, 4000.0f, 0.0f), 0.0f);
		if (!C.Require(A != nullptr, TEXT("A spawned")) || !C.Equip(A, TEXT("sword_one_handed")))
		{
			return;
		}
		C.GrantSkills(A, {TEXT("hunters_leap")});
		C.SetNumber(TEXT("WalkSpeed"), A->GetCharacterMovement()->MaxWalkSpeed);
	});
	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Rooted: no walking, jumping or leaping; attacks still work"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		if (!C.Require(A != nullptr, TEXT("A")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("A walks before the root"), C.Number(TEXT("WalkSpeed")) > 0.0);
		C.ApplyStatus(A, CrossguardTags::State_Rooted, RootTestSeconds);
		C.Test.TestTrue(TEXT("State.Rooted is on"), C.HasTag(A, CrossguardTags::State_Rooted));
		C.Test.TestEqual(TEXT("Walk speed 0 while rooted"), A->GetCharacterMovement()->MaxWalkSpeed, 0.0f, 0.01f);
		C.Test.TestFalse(TEXT("Cannot jump while rooted"), A->CanJump());
		C.Test.TestFalse(TEXT("Cannot Hunter's Leap while rooted"), C.ActivateSkill(A, TEXT("hunters_leap")));
		C.Test.TestTrue(TEXT("Can still attack while rooted"), C.BasicAttack(A));
	});
	Scenario.Wait(RootTestSeconds + 0.3f);

	Scenario.Step(TEXT("The root wears off"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		if (!C.Require(A != nullptr, TEXT("A")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("State.Rooted is gone"), C.HasTag(A, CrossguardTags::State_Rooted));
		C.Test.TestEqual(TEXT("Walk speed is back"), A->GetCharacterMovement()->MaxWalkSpeed, static_cast<float>(C.Number(TEXT("WalkSpeed"))), 1.0f);
		C.Test.TestTrue(TEXT("Can jump again"), A->CanJump());
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
