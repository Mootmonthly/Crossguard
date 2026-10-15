// Scenario tests: parry, Impact Power differences, clashes and clanks, arrows on a shield, AI guards
// and stuns (docs/design-decisions.md, "Block and parry"; CrossguardGuard). Framework:
// CrossguardScenario.h, docs/editor-tests.md. The shield block itself is Crossguard.Scenario.Combat.ShieldBlock.
//
// Weapons (Data/Items/Weapons.json Impact Power; two-handed ones that parry): dagger 1, one-handed
// sword 3, one-handed axe 4, shield 4, staff 4 (parries), spear 5 (parries), two-handed sword 5
// (parries), two-handed axe 6 (parries). Each test checks the Impact Powers it relies on first.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "Character/CrossguardMonster.h"
#include "Combat/CrossguardGuard.h"
#include "CrossguardRules/Combat.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/CrossguardEquipmentComponent.h"

namespace CrossguardScenarioGuard
{
	using FContext = CrossguardScenario::FScenarioContext;

	// Attacker and defender stand this far apart, facing each other (melee reach is 220 cm + 60 cm radius).
	constexpr float Gap = 150.0f;
	// Rows of duels this far apart (a swing's sweep is 60 cm wide either side).
	constexpr float RowSpacing = 600.0f;

	// What the loser of an Impact Power check suffers, by the design table (docs/design-decisions.md,
	// Block and parry): difference 1: 25%; 2: 50% and 0.5 s stun; 3+: 100% and 1 s stun; 0: the hit
	// is stopped. Attacker winning: the defender takes that share of the hit. Defender winning or tie
	// (a parry): the hit is stopped, the attacker takes that share of the defender's weapon damage,
	// and the defender readies a riposte.
	struct FGuardTableOutcome
	{
		float DefenderShare = 1.0f;
		float DefenderStun = 0.0f;
		float AttackerShare = 0.0f;
		float AttackerStun = 0.0f;
		bool bRiposte = false;
	};

	FGuardTableOutcome ParryOutcome(int32 AttackerImpactPower, int32 DefenderImpactPower)
	{
		const auto Share = [](int32 By) { return By <= 0 ? 0.0f : By == 1 ? 0.25f : By == 2 ? 0.5f : 1.0f; };
		const auto StunFor = [](int32 By) { return By <= 1 ? 0.0f : By == 2 ? 0.5f : 1.0f; };
		const int32 Difference = AttackerImpactPower - DefenderImpactPower;
		FGuardTableOutcome Outcome;
		if (Difference > 0)
		{
			Outcome.DefenderShare = Share(Difference);
			Outcome.DefenderStun = StunFor(Difference);
			return Outcome;
		}
		Outcome.DefenderShare = 0.0f;
		Outcome.AttackerShare = Share(-Difference);
		Outcome.AttackerStun = StunFor(-Difference);
		Outcome.bRiposte = true;
		return Outcome;
	}

	// The Impact Power of the weapon in Character's main hand (its guard's when it parries).
	int32 MainHandImpactPower(const ACrossguardCharacter* Character)
	{
		return CrossguardGuard::ImpactPowerOf(Character, ECrossguardEquipmentSlot::MainHand);
	}

	// ExpectedDamage with ExtraBonus added to the source's Attack Damage Bonus (a riposte's +50%), by
	// the damage formula (Crossguard::CalculateDamage, as UCrossguardDamageExecution).
	float ExpectedWithExtraBonus(const FContext& C, const AActor* Source, const AActor* Target, float AttackBase, float ExtraBonus)
	{
		Crossguard::FDamageInput Input;
		Input.Base = AttackBase;
		Input.Flat = C.Stat(Source, UCrossguardAttributeSet::GetAdditionalAttackDamageAttribute());
		Input.Bonus = C.Stat(Source, UCrossguardAttributeSet::GetAttackDamageBonusAttribute()) + ExtraBonus;
		Input.Coefficient = 1.0;
		Input.Resistance = C.Stat(Target, UCrossguardAttributeSet::GetAttackResistanceAttribute());
		Input.Penetration = C.Stat(Source, UCrossguardAttributeSet::GetAttackPenetrationAttribute());
		Input.TrueDamage = C.Stat(Source, UCrossguardAttributeSet::GetTrueDamageAttribute());
		return static_cast<float>(Crossguard::CalculateDamage(Input));
	}

	// A captured log line holding every part.
	bool LogHasLine(const FContext& C, const FString& PartA, const FString& PartB)
	{
		for (const FString& Line : C.LogLines())
		{
			if (Line.Contains(PartA) && Line.Contains(PartB))
			{
				return true;
			}
		}
		return false;
	}

	// CrossguardGuard::ApplyHit logs "<Target> parried|blocked|clashed with <Attacker>'s hit (Impact
	// Power a vs b): N% through" for every Impact Power check, right before the clank
	// (MulticastWeaponClank -> OnWeaponClank). There is no native event to bind to, so the clanks are
	// counted from these lines.
	FString CheckLine(const AActor* Target, const TCHAR* Verb, const AActor* Attacker)
	{
		return FString::Printf(TEXT("%s %s %s's hit"), *GetNameSafe(Target), Verb, *GetNameSafe(Attacker));
	}

	FString ThroughText(float Share)
	{
		return FString::Printf(TEXT("%.0f%% through"), Share * 100.0f);
	}

	// A minion summoned by Summoner (on its team), run by its minion controller.
	ACrossguardMinion* SpawnMinionOf(FContext& C, FName Name, ACrossguardCharacter* Summoner, const FVector& FloorPoint, float Yaw)
	{
		const FVector Location = FloorPoint + FVector(0.0f, 0.0f, GetDefault<ACrossguardMinion>()->GetDefaultHalfHeight() + 5.0f);
		ACrossguardMinion* Minion = C.SpawnActor<ACrossguardMinion>(Name, Location, Yaw, [Summoner](ACrossguardMinion& Spawned)
		{
			Spawned.SetSummoner(Summoner);
		});
		if (Minion != nullptr && Minion->GetController() == nullptr)
		{
			Minion->SpawnDefaultController();
		}
		return Minion;
	}

	// One parry duel of the Impact Power table: Attacker swings Weapon at Defender parrying with
	// DefenderWeapon (two-handed), Difference = attacker's Impact Power - defender's.
	struct FGuardDuel
	{
		const TCHAR* AttackerWeapon;
		const TCHAR* DefenderWeapon;
		int32 Difference;
		const TCHAR* What;
	};

	const FGuardDuel Duels[] = {
		{TEXT("axe_two_handed"), TEXT("sword_two_handed"), 1, TEXT("attacker wins by 1: 25% through, no stun")},
		{TEXT("axe_two_handed"), TEXT("staff_two_handed"), 2, TEXT("attacker wins by 2: 50% through, defender stunned 0.5 s")},
		{TEXT("axe_one_handed"), TEXT("sword_two_handed"), -1, TEXT("defender wins by 1: attacker takes 25%, no stun")},
		{TEXT("sword_one_handed"), TEXT("sword_two_handed"), -2, TEXT("defender wins by 2: attacker takes 50%, stunned 0.5 s")},
		{TEXT("sword_one_handed"), TEXT("axe_two_handed"), -3, TEXT("defender wins by 3: attacker takes 100%, stunned 1 s")},
		{TEXT("spear_two_handed"), TEXT("sword_two_handed"), 0, TEXT("tie: the hit is stopped, nobody hurt or stunned")},
	};
	constexpr int32 DuelCount = UE_ARRAY_COUNT(Duels);

	FName DuelName(const TCHAR* Role, int32 Index)
	{
		return FName(*FString::Printf(TEXT("%s%d"), Role, Index));
	}

	// Stunned now iff the stun lasts longer than Elapsed.
	void CheckStun(FContext& C, const AActor* Actor, const FString& Who, float StunSeconds, float Elapsed)
	{
		const bool bExpected = StunSeconds > Elapsed;
		C.Test.TestTrue(*FString::Printf(TEXT("%s %s %.2f s after the hit (stun %.1f s)"), *Who, bExpected ? TEXT("stunned") : TEXT("not stunned"),
			Elapsed, StunSeconds), C.HasTag(Actor, CrossguardTags::State_Stunned) == bExpected);
	}
}

// --- Parry with a two-handed weapon ----------------------------------------------------------------

// A parries a dagger with a two-handed sword (Impact Power 5 vs 1): the dagger is stopped, the
// attacker takes A's full weapon damage and is stunned 1 s (mirror rule), and A readies a riposte:
// its next attack gets +50% Attack Damage Bonus (and +10% Action Speed for 3 s).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioParryTwoHanded, "Crossguard.Scenario.Combat.ParryTwoHanded", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioParryTwoHanded::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGuard;

	static constexpr float SceneY = 1000.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("Defender (two-handed sword), attacker (dagger), and a dummy beside the defender"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Attacker = C.SpawnPlayer(TEXT("Attacker"), FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* Defender = C.SpawnPlayer(TEXT("Defender"), FVector(Gap, SceneY, 0.0f), 180.0f);
		// Beside the defender, out of the attacker's swing.
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(Gap, SceneY + Gap, 0.0f), -90.0f);
		if (!C.Require(Attacker != nullptr && Defender != nullptr && Dummy != nullptr, TEXT("attacker, defender and dummy spawned")))
		{
			return;
		}
		if (!C.Equip(Attacker, TEXT("dagger_one_handed")) || !C.Equip(Defender, TEXT("sword_two_handed")))
		{
			return;
		}
		C.Require(MainHandImpactPower(Defender) - MainHandImpactPower(Attacker) >= 3,
			FString::Printf(TEXT("the two-handed sword out-weighs the dagger by 3 or more (Impact Power %d vs %d)"), MainHandImpactPower(Defender),
				MainHandImpactPower(Attacker)));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("They face each other; the defender raises its parry"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Attacker = C.Get<ACrossguardCharacter>(TEXT("Attacker"));
		ACrossguardCharacter* Defender = C.Get<ACrossguardCharacter>(TEXT("Defender"));
		if (Attacker == nullptr || Defender == nullptr)
		{
			C.Fail(TEXT("attacker or defender gone"));
			return;
		}
		C.Face(Attacker, 0.0f);
		C.Face(Defender, 180.0f);
		C.Require(C.RaiseGuard(Defender), TEXT("the defender parries (State.Guarding with a two-handed sword)"));
	});

	// The guard follows the view through the camera manager: let it catch up.
	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("The dagger meets the parry: stopped, mirrored, stunned, riposte ready"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Attacker = C.Get<ACrossguardCharacter>(TEXT("Attacker"));
		ACrossguardCharacter* Defender = C.Get<ACrossguardCharacter>(TEXT("Defender"));
		if (Attacker == nullptr || Defender == nullptr)
		{
			C.Fail(TEXT("attacker or defender gone"));
			return;
		}
		C.ClearLog();
		const float DefenderBefore = C.Health(Defender);
		const float AttackerBefore = C.Health(Attacker);
		const float Mirror = FMath::Min(C.ExpectedDamage(Defender, Attacker, C.WeaponValue(Defender)), AttackerBefore);
		C.SetNumber(TEXT("ActionSpeedBefore"), C.Stat(Defender, UCrossguardAttributeSet::GetActionSpeedAttribute()));
		if (!C.Require(C.BasicAttack(Attacker), TEXT("the dagger attack activates")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The defender takes nothing"), C.Health(Defender), DefenderBefore, 0.001f);
		C.Test.AddInfo(FString::Printf(TEXT("Mirror: attacker %.2f -> %.2f Health (expected -%.2f)"), AttackerBefore, C.Health(Attacker), Mirror));
		C.Test.TestEqual(TEXT("The attacker takes the defender's full weapon damage (mirror rule)"), AttackerBefore - C.Health(Attacker), Mirror, 0.05f);
		C.Test.TestTrue(TEXT("Log: parried, 0% through"), LogHasLine(C, CheckLine(Defender, TEXT("parried"), Attacker), ThroughText(0.0f)));
		C.Test.TestTrue(TEXT("The attacker is stunned"), C.HasTag(Attacker, CrossguardTags::State_Stunned));
		C.Test.TestFalse(TEXT("The defender is not stunned"), C.HasTag(Defender, CrossguardTags::State_Stunned));
		C.Test.TestTrue(TEXT("The defender still parries"), C.HasTag(Defender, CrossguardTags::State_Guarding));
		C.Test.TestTrue(TEXT("Riposte ready (State.NextAttack.Riposte)"), C.HasTag(Defender, CrossguardTags::State_NextAttack_Riposte));
		C.Test.TestTrue(TEXT("Riposte speed on (State.RiposteSpeed)"), C.HasTag(Defender, CrossguardTags::State_RiposteSpeed));
		C.Test.TestTrue(TEXT("The riposte raises Action Speed"),
			C.Stat(Defender, UCrossguardAttributeSet::GetActionSpeedAttribute()) > static_cast<float>(C.Number(TEXT("ActionSpeedBefore"))) + 0.001f);
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Half-way through the stun the defender ripostes on the dummy: +50% Attack Damage Bonus"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Attacker = C.Get<ACrossguardCharacter>(TEXT("Attacker"));
		ACrossguardCharacter* Defender = C.Get<ACrossguardCharacter>(TEXT("Defender"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Attacker == nullptr || Defender == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("someone is gone"));
			return;
		}
		C.Test.TestTrue(TEXT("The attacker is still stunned after 0.5 s (1 s stun)"), C.HasTag(Attacker, CrossguardTags::State_Stunned));
		C.LowerGuard(Defender);
		C.Face(Defender, 90.0f);
		const float Before = C.Health(Dummy);
		const float Plain = C.ExpectedDamage(Defender, Dummy, C.WeaponValue(Defender));
		const float Riposte = FMath::Min(ExpectedWithExtraBonus(C, Defender, Dummy, C.WeaponValue(Defender),
			static_cast<float>(CrossguardGuard::Config().ParryAttackDamageBonus)), Before);
		if (!C.Require(C.BasicAttack(Defender), TEXT("the defender's riposte activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Riposte: dummy %.2f -> %.2f (expected -%.2f; a plain hit -%.2f)"), Before, C.Health(Dummy), Riposte, Plain));
		C.Test.TestEqual(TEXT("The riposte hits with +50% Attack Damage Bonus"), Before - C.Health(Dummy), Riposte, 0.05f);
		C.Test.TestTrue(TEXT("...more than a plain hit"), Before - C.Health(Dummy) > Plain + 0.01f || Riposte >= Before);
		C.Test.TestFalse(TEXT("The riposte is used up"), C.HasTag(Defender, CrossguardTags::State_NextAttack_Riposte));
		C.Test.TestTrue(TEXT("The riposte speed lasts (3 s)"), C.HasTag(Defender, CrossguardTags::State_RiposteSpeed));
	});

	Scenario.Wait(0.8f);

	Scenario.Step(TEXT("The 1 s stun is over"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Attacker = C.Get<ACrossguardCharacter>(TEXT("Attacker"));
		if (Attacker != nullptr)
		{
			C.Test.TestFalse(TEXT("The attacker's stun ended after 1 s"), C.HasTag(Attacker, CrossguardTags::State_Stunned));
		}
	});

	Scenario.Run();
	return true;
}

// --- Impact Power differences -----------------------------------------------------------------------

// Six parry duels at once, one per row, covering every line of the Impact Power table: the attacker
// winning by 1 / 2, the defender winning by 1 / 2 / 3, and a tie (stops everything).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioImpactDifferences, "Crossguard.Scenario.Combat.ImpactDifferences", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioImpactDifferences::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGuard;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Six duels: attackers face parrying defenders"), [](FScenarioContext& C)
	{
		for (int32 Index = 0; Index < DuelCount; ++Index)
		{
			const FGuardDuel& Duel = Duels[Index];
			const float Y = 1000.0f + Index * RowSpacing;
			ACrossguardCharacter* Attacker = C.SpawnPlayer(DuelName(TEXT("Attacker"), Index), FVector(0.0f, Y, 0.0f), 0.0f);
			ACrossguardCharacter* Defender = C.SpawnPlayer(DuelName(TEXT("Defender"), Index), FVector(Gap, Y, 0.0f), 180.0f);
			if (!C.Require(Attacker != nullptr && Defender != nullptr, FString::Printf(TEXT("duel %d spawned"), Index)))
			{
				return;
			}
			if (!C.Equip(Attacker, Duel.AttackerWeapon) || !C.Equip(Defender, Duel.DefenderWeapon))
			{
				return;
			}
			const int32 Difference = MainHandImpactPower(Attacker) - MainHandImpactPower(Defender);
			if (!C.Require(Difference == Duel.Difference, FString::Printf(TEXT("%s vs %s differ by %d in Impact Power (are %d vs %d; Weapons.json changed?)"),
					Duel.AttackerWeapon, Duel.DefenderWeapon, Duel.Difference, MainHandImpactPower(Attacker), MainHandImpactPower(Defender))))
			{
				return;
			}
		}
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Defenders raise their parries"), [](FScenarioContext& C)
	{
		for (int32 Index = 0; Index < DuelCount; ++Index)
		{
			ACrossguardCharacter* Attacker = C.Get<ACrossguardCharacter>(DuelName(TEXT("Attacker"), Index));
			ACrossguardCharacter* Defender = C.Get<ACrossguardCharacter>(DuelName(TEXT("Defender"), Index));
			if (Attacker == nullptr || Defender == nullptr)
			{
				C.Fail(TEXT("a duellist is gone"));
				return;
			}
			C.Face(Attacker, 0.0f);
			C.Face(Defender, 180.0f);
			if (!C.Require(C.RaiseGuard(Defender), FString::Printf(TEXT("defender %d parries with %s"), Index, Duels[Index].DefenderWeapon)))
			{
				return;
			}
		}
	});

	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("Every attacker swings once"), [](FScenarioContext& C)
	{
		C.ClearLog();
		for (int32 Index = 0; Index < DuelCount; ++Index)
		{
			const FGuardDuel& Duel = Duels[Index];
			ACrossguardCharacter* Attacker = C.Get<ACrossguardCharacter>(DuelName(TEXT("Attacker"), Index));
			ACrossguardCharacter* Defender = C.Get<ACrossguardCharacter>(DuelName(TEXT("Defender"), Index));
			if (Attacker == nullptr || Defender == nullptr)
			{
				C.Fail(TEXT("a duellist is gone"));
				return;
			}
			const FGuardTableOutcome Outcome = ParryOutcome(MainHandImpactPower(Attacker), MainHandImpactPower(Defender));
			const float AttackerBefore = C.Health(Attacker);
			const float DefenderBefore = C.Health(Defender);
			const float DefenderLoss = Outcome.DefenderShare > 0.0f
				? FMath::Min(C.ExpectedDamage(Attacker, Defender, C.WeaponValue(Attacker), 0.0f, 1.0f, Outcome.DefenderShare), DefenderBefore) : 0.0f;
			const float AttackerLoss = Outcome.AttackerShare > 0.0f
				? FMath::Min(C.ExpectedDamage(Defender, Attacker, C.WeaponValue(Defender), 0.0f, 1.0f, Outcome.AttackerShare), AttackerBefore) : 0.0f;
			if (!C.Require(C.BasicAttack(Attacker), FString::Printf(TEXT("attacker %d's %s attack activates"), Index, Duel.AttackerWeapon)))
			{
				return;
			}
			const FString Case = FString::Printf(TEXT("[%s vs %s, %s]"), Duel.AttackerWeapon, Duel.DefenderWeapon, Duel.What);
			C.Test.AddInfo(FString::Printf(TEXT("%s defender -%.2f (expected -%.2f), attacker -%.2f (expected -%.2f)"), *Case,
				DefenderBefore - C.Health(Defender), DefenderLoss, AttackerBefore - C.Health(Attacker), AttackerLoss));
			C.Test.TestEqual(*FString::Printf(TEXT("%s defender's damage"), *Case), DefenderBefore - C.Health(Defender), DefenderLoss, 0.05f);
			C.Test.TestEqual(*FString::Printf(TEXT("%s attacker's damage (mirror)"), *Case), AttackerBefore - C.Health(Attacker), AttackerLoss, 0.05f);
			C.Test.TestTrue(*FString::Printf(TEXT("%s log: parried, %s"), *Case, *ThroughText(Outcome.DefenderShare)),
				LogHasLine(C, CheckLine(Defender, TEXT("parried"), Attacker), ThroughText(Outcome.DefenderShare)));
			CheckStun(C, Defender, Case + TEXT(" defender"), Outcome.DefenderStun, 0.0f);
			CheckStun(C, Attacker, Case + TEXT(" attacker"), Outcome.AttackerStun, 0.0f);
			// A stun drops the guard; otherwise it stays up.
			C.Test.TestTrue(*FString::Printf(TEXT("%s defender still guards unless stunned"), *Case),
				C.HasTag(Defender, CrossguardTags::State_Guarding) == (Outcome.DefenderStun <= 0.0f));
			C.Test.TestTrue(*FString::Printf(TEXT("%s riposte ready only when the defender did not lose"), *Case),
				C.HasTag(Defender, CrossguardTags::State_NextAttack_Riposte) == Outcome.bRiposte);
		}
		C.SetNumber(TEXT("HitTime"), C.Now());
	});

	// Stuns over time: 0.5 s ones end between the 2nd and 3rd checks, 1 s ones after the 3rd.
	for (const float Elapsed : {0.3f, 0.75f, 1.25f})
	{
		Scenario.WaitUntil(FString::Printf(TEXT("%.2f s after the hits"), Elapsed), Elapsed + 1.0f, [Elapsed](FScenarioContext& C)
		{
			return C.Now() - C.Number(TEXT("HitTime")) >= Elapsed;
		});
		Scenario.Step(FString::Printf(TEXT("Stuns %.2f s after the hits"), Elapsed), [Elapsed](FScenarioContext& C)
		{
			for (int32 Index = 0; Index < DuelCount; ++Index)
			{
				const ACrossguardCharacter* Attacker = C.Get<ACrossguardCharacter>(DuelName(TEXT("Attacker"), Index));
				const ACrossguardCharacter* Defender = C.Get<ACrossguardCharacter>(DuelName(TEXT("Defender"), Index));
				if (Attacker == nullptr || Defender == nullptr)
				{
					return;
				}
				const FGuardTableOutcome Outcome = ParryOutcome(MainHandImpactPower(Attacker), MainHandImpactPower(Defender));
				const FString Case = FString::Printf(TEXT("[%s vs %s]"), Duels[Index].AttackerWeapon, Duels[Index].DefenderWeapon);
				CheckStun(C, Defender, Case + TEXT(" defender"), Outcome.DefenderStun, Elapsed);
				CheckStun(C, Attacker, Case + TEXT(" attacker"), Outcome.AttackerStun, Elapsed);
			}
		});
	}

	Scenario.Run();
	return true;
}

// --- Arrows on a shield ---------------------------------------------------------------------------

// An archer's arrow (Impact Power 1) on a raised shield (4): blocked, nothing through, but the
// archer is not stunned or hurt: a stopped arrow does not reach back (a melee hit at that difference
// would stun the attacker 1 s).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioArrowOnShield, "Crossguard.Scenario.Combat.ArrowBlockedNoStun", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioArrowOnShield::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGuard;

	static constexpr float SceneY = 2000.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with sword and shield; a Ranged monster 7 m away whose arrows weigh 1"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardMonster* Archer = C.SpawnMonster(TEXT("Archer"), NAME_None, FVector(700.0f, SceneY, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Archer != nullptr, TEXT("player and archer spawned")))
		{
			return;
		}
		if (!C.Equip(Player, TEXT("sword_one_handed")) || !C.Equip(Player, TEXT("shield_one_handed"), ECrossguardEquipmentSlot::OffHand))
		{
			return;
		}
		// No Mob Id: built-in numbers, made an archer here. Within its Keep Distance: it stays put.
		Archer->CombatRole = ECrossguardCombatRole::Ranged;
		Archer->AttackRange = 2000.0f;
		Archer->KeepDistance = 800.0f;
		Archer->AttackInterval = 1.0f;
		Archer->ImpactPower = 1;
		const UCrossguardEquipmentComponent* Equipment = Player->GetEquipmentComponent();
		const FCrossguardWeaponRow* Shield = Equipment != nullptr ? Equipment->GetWeaponRow(ECrossguardEquipmentSlot::OffHand) : nullptr;
		C.Require(Shield != nullptr && Shield->ImpactPower - Archer->ImpactPower >= 3,
			TEXT("the shield out-weighs the arrows by 3 or more (a melee hit would be stunned 1 s)"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The player raises the shield toward the archer"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		const AActor* Archer = C.FindActor(TEXT("Archer"));
		if (Player == nullptr || !C.Require(Archer != nullptr, TEXT("the archer is there")))
		{
			return;
		}
		C.Aim(Player, Archer);
		C.Require(C.RaiseGuard(Player), TEXT("the shield is up"));
		C.ClearLog();
	});

	Scenario.WaitUntil(TEXT("an arrow hits the shield"), 5.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		const AActor* Archer = C.FindActor(TEXT("Archer"));
		if (Player == nullptr || Archer == nullptr)
		{
			return false;
		}
		// Keep the shield toward the archer.
		C.Aim(Player, Archer);
		if (C.HasTag(Archer, CrossguardTags::State_Stunned))
		{
			C.SetNumber(TEXT("ArcherStunned"), 1.0);
		}
		return C.LogContains(CheckLine(Player, TEXT("blocked"), Archer));
	});

	Scenario.WaitUntil(TEXT("a moment after the block"), 1.0f, [](FScenarioContext& C)
	{
		const AActor* Archer = C.FindActor(TEXT("Archer"));
		if (Archer != nullptr && C.HasTag(Archer, CrossguardTags::State_Stunned))
		{
			C.SetNumber(TEXT("ArcherStunned"), 1.0);
		}
		if (C.Number(TEXT("WatchEnd")) <= 0.0)
		{
			C.SetNumber(TEXT("WatchEnd"), C.Now() + 0.3);
		}
		return C.Now() >= C.Number(TEXT("WatchEnd"));
	});

	Scenario.Step(TEXT("Blocked, nothing through, archer neither stunned nor hurt"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		const AActor* Archer = C.FindActor(TEXT("Archer"));
		if (Player == nullptr || Archer == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("Log: blocked, 0% through"), LogHasLine(C, CheckLine(Player, TEXT("blocked"), Archer), ThroughText(0.0f)));
		C.Test.TestEqual(TEXT("The player took nothing"), C.Health(Player), C.MaxHealth(Player), 0.01f);
		C.Test.TestTrue(TEXT("The archer was never stunned (a stopped arrow does not reach back)"), C.Number(TEXT("ArcherStunned")) <= 0.0);
		C.Test.TestEqual(TEXT("The archer is unhurt"), C.Health(Archer), C.MaxHealth(Archer), 0.01f);
		C.Test.TestFalse(TEXT("The player is not stunned"), C.HasTag(Player, CrossguardTags::State_Stunned));
	});

	Scenario.Run();
	return true;
}

// --- Clashes and clanks ---------------------------------------------------------------------------

// Two swings within 0.25 s clash like a parry without the riposte: same Impact Power stops the
// second hit; a much lighter second swing is stopped and its swinger takes the heavier weapon's
// damage and a 1 s stun. A second swing 0.4 s later lands normally. Every clash clanks (one log
// line per check, both pushed apart).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSwingClash, "Crossguard.Scenario.Combat.SwingClash", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSwingClash::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGuard;

	FScenario Scenario(*this);

	// Row 1: sword vs sword within 0.1 s (tie). Row 2: two-handed sword then dagger within 0.1 s.
	// Row 3: sword vs sword 0.4 s apart (no clash).
	Scenario.Step(TEXT("Three pairs face each other"), [](FScenarioContext& C)
	{
		const TCHAR* const FirstWeapons[] = {TEXT("sword_one_handed"), TEXT("sword_two_handed"), TEXT("sword_one_handed")};
		const TCHAR* const SecondWeapons[] = {TEXT("sword_one_handed"), TEXT("dagger_one_handed"), TEXT("sword_one_handed")};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Y = 1000.0f + Index * RowSpacing;
			ACrossguardCharacter* First = C.SpawnPlayer(DuelName(TEXT("First"), Index), FVector(0.0f, Y, 0.0f), 0.0f);
			ACrossguardCharacter* Second = C.SpawnPlayer(DuelName(TEXT("Second"), Index), FVector(Gap, Y, 0.0f), 180.0f);
			if (!C.Require(First != nullptr && Second != nullptr, TEXT("pair spawned")) || !C.Equip(First, FirstWeapons[Index])
				|| !C.Equip(Second, SecondWeapons[Index]))
			{
				return;
			}
		}
		const ACrossguardCharacter* Heavy = C.Get<ACrossguardCharacter>(TEXT("First1"));
		const ACrossguardCharacter* Light = C.Get<ACrossguardCharacter>(TEXT("Second1"));
		C.Require(Heavy != nullptr && Light != nullptr && MainHandImpactPower(Heavy) - MainHandImpactPower(Light) >= 3,
			TEXT("the two-handed sword out-weighs the dagger by 3 or more"));
		C.Require(CrossguardGuard::Config().ClashWindowSeconds > 0.15 && CrossguardGuard::Config().ClashWindowSeconds < 0.35,
			TEXT("the clash window is about 0.25 s"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Every first swinger swings: full hits"), [](FScenarioContext& C)
	{
		C.ClearLog();
		for (int32 Index = 0; Index < 3; ++Index)
		{
			ACrossguardCharacter* First = C.Get<ACrossguardCharacter>(DuelName(TEXT("First"), Index));
			ACrossguardCharacter* Second = C.Get<ACrossguardCharacter>(DuelName(TEXT("Second"), Index));
			if (First == nullptr || Second == nullptr)
			{
				C.Fail(TEXT("a swinger is gone"));
				return;
			}
			C.Face(First, 0.0f);
			C.Face(Second, 180.0f);
			const float Before = C.Health(Second);
			const float Expected = FMath::Min(C.ExpectedDamage(First, Second, C.WeaponValue(First)), Before);
			if (!C.Require(C.BasicAttack(First), TEXT("the first swing activates")))
			{
				return;
			}
			C.Test.TestEqual(*FString::Printf(TEXT("Pair %d: the first swing lands in full (nothing in its way)"), Index), Before - C.Health(Second), Expected,
				0.05f);
		}
		C.Test.TestEqual(TEXT("No clash yet"), C.LogCount(TEXT("clashed with")), 0);
		C.SetNumber(TEXT("SwingTime"), C.Now());
	});

	Scenario.Wait(0.1f);

	Scenario.Step(TEXT("Pairs 1 and 2 answer within 0.25 s: clashes"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* First0 = C.Get<ACrossguardCharacter>(TEXT("First0"));
		ACrossguardCharacter* Second0 = C.Get<ACrossguardCharacter>(TEXT("Second0"));
		ACrossguardCharacter* First1 = C.Get<ACrossguardCharacter>(TEXT("First1"));
		ACrossguardCharacter* Second1 = C.Get<ACrossguardCharacter>(TEXT("Second1"));
		if (First0 == nullptr || Second0 == nullptr || First1 == nullptr || Second1 == nullptr)
		{
			C.Fail(TEXT("a swinger is gone"));
			return;
		}
		const float Since = C.Now() - static_cast<float>(C.Number(TEXT("SwingTime")));
		C.Require(Since < CrossguardGuard::Config().ClashWindowSeconds, FString::Printf(TEXT("still within the clash window (%.2f s)"), Since));

		// Same Impact Power: stopped, nobody hurt or stunned.
		const float First0Before = C.Health(First0);
		const float Second0Before = C.Health(Second0);
		C.SetNumber(TEXT("Pair0Distance"), FVector::Dist2D(First0->GetActorLocation(), Second0->GetActorLocation()));
		if (!C.Require(C.BasicAttack(Second0), TEXT("pair 1's answer activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Pair 1: log: clashed, 0% through"), LogHasLine(C, CheckLine(First0, TEXT("clashed with"), Second0), ThroughText(0.0f)));
		C.Test.TestEqual(TEXT("Pair 1: the answer is stopped (tie)"), C.Health(First0), First0Before, 0.001f);
		C.Test.TestEqual(TEXT("Pair 1: a tie hurts nobody"), C.Health(Second0), Second0Before, 0.001f);
		C.Test.TestFalse(TEXT("Pair 1: nobody stunned (first)"), C.HasTag(First0, CrossguardTags::State_Stunned));
		C.Test.TestFalse(TEXT("Pair 1: nobody stunned (second)"), C.HasTag(Second0, CrossguardTags::State_Stunned));
		C.Test.TestFalse(TEXT("Pair 1: a clash is no parry: no riposte"), C.HasTag(First0, CrossguardTags::State_NextAttack_Riposte));

		// Dagger into a two-handed sword swing: the dagger loses by 3+.
		const float First1Before = C.Health(First1);
		const float Second1Before = C.Health(Second1);
		const float Mirror = FMath::Min(C.ExpectedDamage(First1, Second1, C.WeaponValue(First1)), Second1Before);
		if (!C.Require(C.BasicAttack(Second1), TEXT("pair 2's answer activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Pair 2: log: clashed, 0% through"), LogHasLine(C, CheckLine(First1, TEXT("clashed with"), Second1), ThroughText(0.0f)));
		C.Test.TestEqual(TEXT("Pair 2: the dagger is stopped"), C.Health(First1), First1Before, 0.001f);
		C.Test.TestEqual(TEXT("Pair 2: the dagger's swinger takes the two-handed sword's damage"), Second1Before - C.Health(Second1), Mirror, 0.05f);
		C.Test.TestTrue(TEXT("Pair 2: the dagger's swinger is stunned"), C.HasTag(Second1, CrossguardTags::State_Stunned));
		C.Test.TestFalse(TEXT("Pair 2: the heavy swinger is not stunned"), C.HasTag(First1, CrossguardTags::State_Stunned));
		C.Test.TestFalse(TEXT("Pair 2: a clash is no parry: no riposte"), C.HasTag(First1, CrossguardTags::State_NextAttack_Riposte));

		C.Test.TestEqual(TEXT("Two clanks so far (one Impact Power check line each)"), C.LogCount(TEXT("clashed with")), 2);
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("The clank pushed pair 1 apart; pair 3 answers 0.4 s late: no clash"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* First0 = C.Get<ACrossguardCharacter>(TEXT("First0"));
		ACrossguardCharacter* Second0 = C.Get<ACrossguardCharacter>(TEXT("Second0"));
		ACrossguardCharacter* First2 = C.Get<ACrossguardCharacter>(TEXT("First2"));
		ACrossguardCharacter* Second2 = C.Get<ACrossguardCharacter>(TEXT("Second2"));
		if (First0 == nullptr || Second0 == nullptr || First2 == nullptr || Second2 == nullptr)
		{
			C.Fail(TEXT("a swinger is gone"));
			return;
		}
		const double Before = C.Number(TEXT("Pair0Distance"));
		const double After = FVector::Dist2D(First0->GetActorLocation(), Second0->GetActorLocation());
		C.Test.AddInfo(FString::Printf(TEXT("Pair 1 distance %.1f -> %.1f cm after the clank"), Before, After));
		C.Test.TestTrue(TEXT("Pair 1: the clank pushed them apart"), After > Before + 1.0);

		const float Since = C.Now() - static_cast<float>(C.Number(TEXT("SwingTime")));
		C.Require(Since > CrossguardGuard::Config().ClashWindowSeconds, FString::Printf(TEXT("past the clash window (%.2f s)"), Since));
		C.Face(First2, 0.0f);
		C.Face(Second2, 180.0f);
		const float Before2 = C.Health(First2);
		const float Expected = FMath::Min(C.ExpectedDamage(Second2, First2, C.WeaponValue(Second2)), Before2);
		if (!C.Require(C.BasicAttack(Second2), TEXT("pair 3's answer activates")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Pair 3: the late answer lands in full"), Before2 - C.Health(First2), Expected, 0.05f);
		C.Test.TestFalse(TEXT("Pair 3: no clash line"), C.LogContains(CheckLine(First2, TEXT("clashed with"), Second2)));
		C.Test.TestEqual(TEXT("Still two clanks in all"), C.LogCount(TEXT("clashed with")), 2);
	});

	Scenario.Run();
	return true;
}

// --- AI guards ------------------------------------------------------------------------------------

// Monsters guard against melee swings aimed at them about half the time (1 s, their own Impact
// Power): over 20 dagger swings at a monster, some are parried and some land.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioAIGuardsAgainstSwings, "Crossguard.Scenario.Combat.AIGuardsAgainstSwings", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioAIGuardsAgainstSwings::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGuard;

	static constexpr float SceneY = 3000.0f;
	static constexpr int32 Swings = 20;
	FScenario Scenario(*this);
	Scenario.TimeDilation(2.0f);

	Scenario.Step(TEXT("A player with a dagger and a harmless monster"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), NAME_None, FVector(Gap, SceneY, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Monster != nullptr, TEXT("player and monster spawned")) || !C.Equip(Player, TEXT("dagger_one_handed")))
		{
			return;
		}
		// No damage back (its hits and its parry's mirror deal Attack Damage) and one swing only.
		Monster->AttackDamage = 0.0f;
		Monster->AttackInterval = 1000.0f;
		C.Require(Monster->ImpactPower > MainHandImpactPower(Player), TEXT("the monster's guard out-weighs a dagger (a parry stops it)"));
		C.SetNumber(TEXT("Swings"), 0.0);
		C.SetNumber(TEXT("Parried"), 0.0);
		C.SetNumber(TEXT("Landed"), 0.0);
	});

	// It takes its one swing at the player when it sees them; let that swing's clash window pass.
	Scenario.Wait(1.5f);

	Scenario.WaitUntil(TEXT("20 dagger swings at the monster"), 60.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		if (Player == nullptr || Monster == nullptr)
		{
			C.Fail(TEXT("player or monster gone"));
			return false;
		}
		if (C.HasTag(Monster, CrossguardTags::State_Guarding))
		{
			C.SetNumber(TEXT("SawGuard"), 1.0);
		}
		// A parry (Impact Power difference 2) stuns the player 0.5 s.
		if (!C.HasTag(Player, CrossguardTags::State_Stunned))
		{
			C.Aim(Player, Monster);
			const int32 ParriedBefore = C.LogCount(CheckLine(Monster, TEXT("parried"), Player));
			const float Before = C.Health(Monster);
			if (C.BasicAttack(Player))
			{
				C.SetNumber(TEXT("Swings"), C.Number(TEXT("Swings")) + 1.0);
				if (C.LogCount(CheckLine(Monster, TEXT("parried"), Player)) > ParriedBefore)
				{
					C.SetNumber(TEXT("Parried"), C.Number(TEXT("Parried")) + 1.0);
				}
				else if (C.Health(Monster) < Before - 0.01f)
				{
					C.SetNumber(TEXT("Landed"), C.Number(TEXT("Landed")) + 1.0);
				}
				Monster->RestoreFullHealth();
			}
		}
		return C.Number(TEXT("Swings")) >= Swings;
	}, 0.05f);

	Scenario.Step(TEXT("Some swings parried, some landed"), [](FScenarioContext& C)
	{
		const int32 Parried = static_cast<int32>(C.Number(TEXT("Parried")));
		const int32 Landed = static_cast<int32>(C.Number(TEXT("Landed")));
		C.Test.AddInfo(FString::Printf(TEXT("%d swings: %d parried, %d landed (a guard is raised for half the swings and held 1 s)"), Swings, Parried,
			Landed));
		C.Test.TestTrue(TEXT("The monster raised its guard (State.Guarding seen)"), C.Number(TEXT("SawGuard")) > 0.0);
		C.Test.TestTrue(TEXT("Some swings were parried"), Parried > 0);
		C.Test.TestTrue(TEXT("Some swings landed (it does not guard every time)"), Landed > 0);
		// Not asserted: a swing can also miss when a clank has just pushed the monster out of reach.
		if (Parried + Landed != Swings)
		{
			C.Test.AddInfo(FString::Printf(TEXT("%d swings neither parried nor landed (missed)"), Swings - Parried - Landed));
		}
	});

	Scenario.Run();
	return true;
}

// AIs never guard against an ally's swing: a player hitting a party member's minion (friendly fire
// is on) is never parried.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioAIIgnoresAllySwings, "Crossguard.Scenario.Combat.AIIgnoresAllySwings", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioAIIgnoresAllySwings::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGuard;

	static constexpr float SceneY = 4000.0f;
	static constexpr float SceneX = 2000.0f;
	// Unguarded 12 times in a row by chance: 1 in 4096.
	static constexpr int32 Swings = 12;
	FScenario Scenario(*this);
	Scenario.TimeDilation(2.0f);

	Scenario.Step(TEXT("Party members P (dagger) and Q, and Q's minion in front of P"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* P = C.SpawnPlayer(TEXT("P"), FVector(SceneX + Gap, SceneY, 0.0f), 180.0f, TEXT("Allies"));
		ACrossguardCharacter* Q = C.SpawnPlayer(TEXT("Q"), FVector(SceneX, SceneY + 200.0f, 0.0f), 0.0f, TEXT("Allies"));
		if (!C.Require(P != nullptr && Q != nullptr, TEXT("P and Q spawned")) || !C.Equip(P, TEXT("dagger_one_handed")))
		{
			return;
		}
		ACrossguardMinion* Minion = SpawnMinionOf(C, TEXT("Minion"), Q, FVector(SceneX, SceneY, 0.0f), 0.0f);
		if (!C.Require(Minion != nullptr && Minion->GetController() != nullptr, TEXT("Q's minion spawned with its controller")))
		{
			return;
		}
		C.SetNumber(TEXT("Swings"), 0.0);
		C.SetNumber(TEXT("Landed"), 0.0);
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("P and the minion are allies"), [](FScenarioContext& C)
	{
		const AActor* P = C.FindActor(TEXT("P"));
		const AActor* Minion = C.FindActor(TEXT("Minion"));
		C.Require(P != nullptr && Minion != nullptr && UCrossguardTeamLibrary::AreAllies(P, Minion), TEXT("P and Q's minion are on one team (same Party)"));
		C.ClearLog();
	});

	Scenario.WaitUntil(TEXT("12 dagger swings at the allied minion"), 30.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* P = C.Get<ACrossguardCharacter>(TEXT("P"));
		ACrossguardMinion* Minion = C.Get<ACrossguardMinion>(TEXT("Minion"));
		if (P == nullptr || Minion == nullptr)
		{
			C.Fail(TEXT("P or the minion gone"));
			return false;
		}
		if (C.HasTag(Minion, CrossguardTags::State_Guarding))
		{
			C.SetNumber(TEXT("SawGuard"), 1.0);
		}
		C.Aim(P, Minion);
		const float Before = C.Health(Minion);
		if (C.BasicAttack(P))
		{
			C.SetNumber(TEXT("Swings"), C.Number(TEXT("Swings")) + 1.0);
			if (C.Health(Minion) < Before - 0.01f)
			{
				C.SetNumber(TEXT("Landed"), C.Number(TEXT("Landed")) + 1.0);
			}
			if (C.HasTag(Minion, CrossguardTags::State_Guarding))
			{
				C.SetNumber(TEXT("SawGuard"), 1.0);
			}
			Minion->RestoreFullHealth();
		}
		return C.Number(TEXT("Swings")) >= Swings;
	}, 0.05f);

	Scenario.Step(TEXT("Never guarded, every swing landed"), [](FScenarioContext& C)
	{
		C.Test.TestTrue(TEXT("The minion never raised its guard against its ally"), C.Number(TEXT("SawGuard")) <= 0.0);
		C.Test.TestEqual(TEXT("No parry line"), C.LogCount(TEXT(" parried ")), 0);
		C.Test.TestEqual(TEXT("Every swing landed (friendly fire)"), static_cast<int32>(C.Number(TEXT("Landed"))), static_cast<int32>(Swings));
	});

	Scenario.Run();
	return true;
}

// --- Stun -----------------------------------------------------------------------------------------

// A stunned character cannot attack, use skills, jump, move or raise a guard, and a stun drops a
// raised guard; all of it comes back when the stun ends.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioStunBlocksActions, "Crossguard.Scenario.Combat.StunBlocksActions", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioStunBlocksActions::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioGuard;

	static constexpr float SceneY = 5000.0f;
	static const FName Skill(TEXT("unleash"));
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A (two-handed axe) parries; B (dagger, Unleash) will be stunned by it; C (two-handed sword) guards alone"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* B = C.SpawnPlayer(TEXT("B"), FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(Gap, SceneY, 0.0f), 180.0f);
		ACrossguardCharacter* Guard = C.SpawnPlayer(TEXT("C"), FVector(0.0f, SceneY + 800.0f, 0.0f), 0.0f);
		if (!C.Require(A != nullptr && B != nullptr && Guard != nullptr, TEXT("A, B and C spawned")))
		{
			return;
		}
		if (!C.Equip(A, TEXT("axe_two_handed")) || !C.Equip(B, TEXT("dagger_one_handed")) || !C.Equip(Guard, TEXT("sword_two_handed")))
		{
			return;
		}
		C.Require(C.GrantSkills(B, {Skill}), TEXT("B has Unleash"));
		C.Require(MainHandImpactPower(A) - MainHandImpactPower(B) >= 3, TEXT("the two-handed axe out-weighs the dagger by 3 or more (1 s stun)"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("A and C raise their guards"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		ACrossguardCharacter* Guard = C.Get<ACrossguardCharacter>(TEXT("C"));
		if (A == nullptr || B == nullptr || Guard == nullptr)
		{
			C.Fail(TEXT("someone is gone"));
			return;
		}
		C.Face(B, 0.0f);
		C.Face(A, 180.0f);
		C.Require(C.RaiseGuard(A), TEXT("A parries"));
		C.Require(C.RaiseGuard(Guard), TEXT("C parries"));
		C.Test.TestTrue(TEXT("B can jump before the stun"), B->CanJump());
	});

	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("B's dagger meets A's parry (1 s stun); C is stunned directly"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		ACrossguardCharacter* Guard = C.Get<ACrossguardCharacter>(TEXT("C"));
		if (A == nullptr || B == nullptr || Guard == nullptr)
		{
			C.Fail(TEXT("someone is gone"));
			return;
		}
		if (!C.Require(C.BasicAttack(B), TEXT("B's dagger attack activates")))
		{
			return;
		}
		C.Require(C.HasTag(B, CrossguardTags::State_Stunned), TEXT("B is stunned by the parry"));
		CrossguardGuard::Stun(nullptr, Guard->GetAbilitySystemComponent(), 1.0f);
		C.Require(C.HasTag(Guard, CrossguardTags::State_Stunned), TEXT("C is stunned"));
		C.Test.TestFalse(TEXT("The stun dropped C's guard"), C.HasTag(Guard, CrossguardTags::State_Guarding));
		C.LowerGuard(A);
	});

	// Past the dagger's own recovery (3 attacks a second) and the clank's bounce, well within the stun.
	Scenario.Wait(0.45f);

	Scenario.Step(TEXT("While stunned: no attack, skill, jump, movement or guard"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		ACrossguardCharacter* Guard = C.Get<ACrossguardCharacter>(TEXT("C"));
		if (B == nullptr || Guard == nullptr)
		{
			C.Fail(TEXT("B or C gone"));
			return;
		}
		if (!C.Require(C.HasTag(B, CrossguardTags::State_Stunned) && C.HasTag(Guard, CrossguardTags::State_Stunned), TEXT("B and C are still stunned")))
		{
			return;
		}
		// Away from A, so nothing could be parried.
		C.Face(B, 180.0f);
		C.Test.TestFalse(TEXT("A stunned B cannot attack"), C.BasicAttack(B));
		C.Test.TestFalse(TEXT("A stunned B cannot use Unleash"), C.ActivateSkill(B, Skill));
		C.Test.TestFalse(TEXT("A stunned B cannot jump"), B->CanJump());
		C.Test.TestEqual(TEXT("A stunned B cannot move (walk speed 0)"), B->GetCharacterMovement()->MaxWalkSpeed, 0.0f, 0.01f);
		C.Test.TestFalse(TEXT("A stunned C cannot raise its guard"), C.RaiseGuard(Guard));
	});

	Scenario.Wait(0.9f);

	Scenario.Step(TEXT("After the stun everything works again"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* B = C.Get<ACrossguardCharacter>(TEXT("B"));
		ACrossguardCharacter* Guard = C.Get<ACrossguardCharacter>(TEXT("C"));
		if (B == nullptr || Guard == nullptr)
		{
			C.Fail(TEXT("B or C gone"));
			return;
		}
		C.Test.TestFalse(TEXT("B's stun is over"), C.HasTag(B, CrossguardTags::State_Stunned));
		C.Test.TestFalse(TEXT("C's stun is over"), C.HasTag(Guard, CrossguardTags::State_Stunned));
		C.Test.TestTrue(TEXT("B can jump again"), B->CanJump());
		C.Test.TestTrue(TEXT("B can move again"), B->GetCharacterMovement()->MaxWalkSpeed > 0.0f);
		C.Test.TestTrue(TEXT("B can attack again"), C.BasicAttack(B));
		C.Test.TestTrue(TEXT("B can use Unleash again"), C.ActivateSkill(B, Skill));
		C.Test.TestTrue(TEXT("C can raise its guard again"), C.RaiseGuard(Guard));
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
