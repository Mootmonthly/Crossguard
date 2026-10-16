// Scenario test: a stealthed character's net relevancy (docs/anti-cheat.md, "Stealth position leak").
// Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// Standalone PIE has no client connections, so this calls ACrossguardCharacter::IsNetRelevantFor
// directly with the viewers' PlayerControllers, as the net driver would on a server. Whether enemy
// clients really drop and re-create the actor is a two-client check (next-build-checklist.md).

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace CrossguardScenarioStealthRelevancy
{
	const TCHAR* const SwitchName = TEXT("crossguard.StealthHidesFromEnemies");
	constexpr float StealthTestSeconds = 30.0f;

	// Is Target relevant to Viewer's connection (viewing through its own pawn)?
	bool IsRelevantTo(const ACrossguardCharacter* Target, const APlayerController* Viewer)
	{
		const APawn* ViewPawn = Viewer->GetPawn();
		const FVector Source = ViewPawn != nullptr ? ViewPawn->GetActorLocation() : Target->GetActorLocation();
		return Target->IsNetRelevantFor(Viewer, ViewPawn != nullptr ? static_cast<const AActor*>(ViewPawn) : Viewer, Source);
	}

	// Sets the switch; false if something with a higher priority (the console) holds it.
	bool SetSwitch(IConsoleVariable* Switch, bool bOn)
	{
		Switch->Set(bOn ? 1 : 0, ECVF_SetByCode);
		return Switch->GetBool() == bOn;
	}
}

// Stealth (design-decisions.md): invisible to everyone outside your party. With the switch on, a
// stealthed character is not relevant to an enemy's connection, still relevant to its own and an
// ally's; without stealth, or with the switch off, everyone gets it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioStealthNetRelevancy, "Crossguard.Scenario.Status.StealthNetRelevancy", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioStealthNetRelevancy::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioStealthRelevancy;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Sneak and Ally in one party, Enemy on its own, 3 m apart"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Sneak = C.SpawnPlayer(TEXT("Sneak"), FVector(0.0f, 5000.0f, 0.0f), 0.0f, TEXT("stealth_relevancy_party"));
		ACrossguardCharacter* Ally = C.SpawnPlayer(TEXT("Ally"), FVector(300.0f, 5000.0f, 0.0f), 180.0f, TEXT("stealth_relevancy_party"));
		ACrossguardCharacter* Enemy = C.SpawnPlayer(TEXT("Enemy"), FVector(0.0f, 5300.0f, 0.0f), -90.0f);
		C.Require(Sneak != nullptr && Ally != nullptr && Enemy != nullptr, TEXT("three players spawned"));
		C.Require(IConsoleManager::Get().FindConsoleVariable(SwitchName) != nullptr, TEXT("crossguard.StealthHidesFromEnemies exists"));
	});
	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("Relevancy with and without stealth, switch on and off"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Sneak = C.Get<ACrossguardCharacter>(TEXT("Sneak"));
		APlayerController* SneakPC = C.Get<APlayerController>(TEXT("Sneak.Controller"));
		APlayerController* AllyPC = C.Get<APlayerController>(TEXT("Ally.Controller"));
		APlayerController* EnemyPC = C.Get<APlayerController>(TEXT("Enemy.Controller"));
		IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(SwitchName);
		if (!C.Require(Sneak != nullptr && SneakPC != nullptr && AllyPC != nullptr && EnemyPC != nullptr && Switch != nullptr, TEXT("players")))
		{
			return;
		}
		const bool bWasOn = Switch->GetBool();
		if (!SetSwitch(Switch, true))
		{
			C.Skip(TEXT("crossguard.StealthHidesFromEnemies is held by the console; reset it to run this test"));
			return;
		}

		C.Test.TestTrue(TEXT("Not stealthed: relevant to the enemy"), IsRelevantTo(Sneak, EnemyPC));
		C.Test.TestTrue(TEXT("Not stealthed: relevant to the ally"), IsRelevantTo(Sneak, AllyPC));

		C.ApplyStatus(Sneak, CrossguardTags::State_Stealthed, StealthTestSeconds);
		C.Test.TestTrue(TEXT("State.Stealthed is on"), C.HasTag(Sneak, CrossguardTags::State_Stealthed));
		C.Test.TestFalse(TEXT("Stealthed: not relevant to the enemy"), IsRelevantTo(Sneak, EnemyPC));
		C.Test.TestTrue(TEXT("Stealthed: relevant to the ally"), IsRelevantTo(Sneak, AllyPC));
		C.Test.TestTrue(TEXT("Stealthed: relevant to its own player"), IsRelevantTo(Sneak, SneakPC));

		SetSwitch(Switch, false);
		C.Test.TestTrue(TEXT("Switch off: stealthed but relevant to the enemy"), IsRelevantTo(Sneak, EnemyPC));
		SetSwitch(Switch, true);

		// Stealth ends: relevant to the enemy again.
		C.Kill(Sneak, nullptr);
		Switch->Set(bWasOn ? 1 : 0, ECVF_SetByCode);
	});

	Scenario.Run();
	return true;
}

#endif
