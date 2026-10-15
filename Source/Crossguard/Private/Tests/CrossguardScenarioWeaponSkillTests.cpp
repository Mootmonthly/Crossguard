// Scenario tests: weapon skills, next-attack riders and weapon passives (docs/next-build-checklist.md
// section 4, skills table). Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// Expected numbers come from Data/Skills/Skills.json, Data/Items/Weapons.json and the code that uses
// them (Skills/CrossguardWeaponSkills.cpp, Combat/CrossguardWeaponAttackAbility.cpp GatherRiders),
// through C.ExpectedDamage with both characters' current stats.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Combat/CrossguardGuard.h"
#include "CrossguardRules/Combat.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "Skills/CrossguardSkillAbility.h"
#include "Skills/CrossguardWall.h"

namespace CrossguardScenarioWeaponSkills
{
	using namespace CrossguardScenario;

	// --- Data (Data/Skills/Skills.json, Data/Items/Weapons.json, docs/design-decisions.md) ---------

	// Next-attack riders (GatherRiders).
	constexpr float SlashWeaponDamage = 7.0f;
	constexpr float MortalStrikeAttackDamage = 20.0f;
	constexpr float ShadeBlowMagicDamage = 20.0f;
	constexpr float ShadeBlowHealBlockSeconds = 5.0f;
	// Gouge: 25 Attack Damage over 5 s = 5 per second (a tick each second, none on application).
	constexpr float GougeDamagePerSecond = 5.0f;
	constexpr int32 GougeTicks = 5;
	constexpr float GougeSlow = 0.05f;
	constexpr float GougeSeconds = 5.0f;
	constexpr float PurgeShotMagicDamage = 10.0f;
	constexpr float PurgeShotMuteSeconds = 3.0f;
	// Bow buffs: 8 s.
	constexpr float BowBuffSeconds = 8.0f;
	constexpr float QuickshotMoveSpeed = 0.05f;
	constexpr float QuickshotActionSpeed = 0.25f;
	constexpr float ArcaneArrowsMagicDamage = 5.0f;
	// Cleave: 85% of the main-hand weapon, 3 m, 120 degree arc.
	constexpr float CleaveWeaponFraction = 0.85f;
	// Parting the Land: 65 Attack Damage, knock-up 1 s, two 15 s walls.
	constexpr float PartingDamage = 65.0f;
	constexpr float WallSeconds = 15.0f;
	// Hunter's Leap: 0.5 m per AGI point.
	constexpr float LeapCmPerAgility = 50.0f;
	// Like A Lion: 30 m, lands 1.5 m short, STR x1.2 for 6 s.
	constexpr float LionRange = 3000.0f;
	constexpr float LionStopShort = 150.0f;
	constexpr float LionStrength = 1.2f;
	constexpr float LionBuffSeconds = 6.0f;
	// Spear: 33 AD at Common, once every 1.2 s. Triple Thrust: three thrusts 0.15 s apart, +15 each.
	constexpr float SpearValue = 33.0f;
	constexpr float SpearSecondsPerAttack = 1.2f;
	constexpr int32 TripleThrustCount = 3;
	constexpr float TripleThrustBonus = 15.0f;
	constexpr float TripleThrustSeconds = 0.45f;
	// Passives.
	constexpr float HeroicMightStrength = 10.0f;
	constexpr float SwordMasteryAttackDamage = 5.0f;
	constexpr float SwordMasteryActionSpeed = 0.1f;
	constexpr float SwordMasteryMeleeCDR = 0.1f;
	// Heal (wand): 20 Magical Healing.
	constexpr float HealAmount = 20.0f;

	// Melee targets stand this far ahead (reach 220 cm + 60 cm radius).
	constexpr float MeleeGap = 150.0f;
	// Damage comparisons (floats through the GAS aggregator).
	constexpr float DamageTolerance = 0.05f;

	// --- Helpers ---------------------------------------------------------------------------------

	UAbilitySystemComponent* ASCOf(const AActor* Actor)
	{
		return Actor != nullptr ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor) : nullptr;
	}

	// Loadouts costing more than the level-1 Skill Capacity (6): use the debug progression at the max
	// level (capacity 6 + 1 per level, level clamped by the progression config). Stats are not
	// re-initialized; only the capacity changes.
	void AllowFullSkillCapacity(ACrossguardCharacter* Character)
	{
		if (Character != nullptr)
		{
			Character->bUseDebugProgression = true;
			Character->Level = 15;
		}
	}

	// Both named characters, or Fail.
	bool GetBoth(FScenarioContext& C, FName A, FName B, ACrossguardCharacter*& OutA, ACrossguardCharacter*& OutB)
	{
		OutA = C.Get<ACrossguardCharacter>(A);
		OutB = C.Get<ACrossguardCharacter>(B);
		if (OutA == nullptr || OutB == nullptr)
		{
			C.Fail(FString::Printf(TEXT("%s or %s is gone"), *A.ToString(), *B.ToString()));
			return false;
		}
		return true;
	}

	FName Key(const TCHAR* Prefix, const TCHAR* Suffix)
	{
		return FName(*FString::Printf(TEXT("%s.%s"), Prefix, Suffix));
	}

	// A basic attack from Attacker at Target, recording what it took off Target and what it should
	// have (weapon value x WeaponFraction + AttackExtra Attack, MagicExtra Magic) under Prefix.Taken /
	// .Expected / .Time. False while the attack cannot start (recovering): poll it with WaitUntil.
	// Melee only (the damage lands at once).
	bool TryMeleeHit(FScenarioContext& C, FName Attacker, FName Target, const TCHAR* Prefix, float AttackExtra = 0.0f, float MagicExtra = 0.0f)
	{
		ACrossguardCharacter* A = nullptr;
		ACrossguardCharacter* T = nullptr;
		if (!GetBoth(C, Attacker, Target, A, T))
		{
			return true;
		}
		const float Before = C.Health(T);
		const float Expected = FMath::Min(C.ExpectedDamage(A, T, C.WeaponValue(A) + AttackExtra, MagicExtra), Before);
		if (!C.BasicAttack(A))
		{
			return false;
		}
		C.SetNumber(Key(Prefix, TEXT("Before")), Before);
		C.SetNumber(Key(Prefix, TEXT("Taken")), Before - C.Health(T));
		C.SetNumber(Key(Prefix, TEXT("Expected")), Expected);
		C.SetNumber(Key(Prefix, TEXT("Time")), C.Now());
		return true;
	}

	void CheckRecordedHit(FScenarioContext& C, const TCHAR* Prefix, const TCHAR* What)
	{
		const float Taken = static_cast<float>(C.Number(Key(Prefix, TEXT("Taken"))));
		const float Expected = static_cast<float>(C.Number(Key(Prefix, TEXT("Expected"))));
		C.Test.AddInfo(FString::Printf(TEXT("%s: took %.2f (expected %.2f)"), What, Taken, Expected));
		C.Test.TestEqual(What, Taken, Expected, DamageTolerance);
	}

	// One Attack Damage hit with an extra Attack Damage Bonus on top of the source's ADB (a parry's
	// riposte, SetByCaller.Damage.AttackBonus), by the formula of UCrossguardDamageExecution.
	float ExpectedWithExtraAttackBonus(const FScenarioContext& C, const AActor* Source, const AActor* Target, float AttackBase, float ExtraBonus)
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

	// Magical healing from Healer (its AMH and MHB, UCrossguardHealingExecution), capped by the
	// target's missing Health.
	float ExpectedMagicalHealing(const FScenarioContext& C, const AActor* Healer, const AActor* Target, float Base)
	{
		const double Healing = Crossguard::CalculateHealing(Base, C.Stat(Healer, UCrossguardAttributeSet::GetAdditionalMagicalHealingAttribute()),
			C.Stat(Healer, UCrossguardAttributeSet::GetMagicalHealingBonusAttribute()), 1.0);
		return FMath::Min(static_cast<float>(Healing), FMath::Max(C.MaxHealth(Target) - C.Health(Target), 0.0f));
	}

	int32 CountWallsOwnedBy(const FScenarioContext& C, const AActor* Owner)
	{
		int32 Count = 0;
		for (const ACrossguardWall* Wall : C.FindAll<ACrossguardWall>())
		{
			if (IsValid(Wall) && Wall->GetOwner() == Owner)
			{
				++Count;
			}
		}
		return Count;
	}

	// Data passives (heroic_might, sword_mastery) are built from the Skill Catalog: without one there
	// is nothing to test.
	bool HasSkillCatalog(const ACrossguardCharacter* Character)
	{
		const UCrossguardLoadoutComponent* Loadout = Character != nullptr ? Character->GetLoadoutComponent() : nullptr;
		return Loadout != nullptr && Loadout->SkillCatalog != nullptr;
	}
}

// =====================================================================================================
// Checklist: "Next-attack: slash, mortal_strike, shade_blow (sword in hand), then left click the dummy
// -> States shows each ready tag; one hit uses them all: +7, +20 Attack, +20 Magic."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillNextAttackStack, "Crossguard.Scenario.Skills.NextAttackStack", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillNextAttackStack::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A swordsman with Slash, Mortal Strike and Shade Blow; a dummy in reach"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Swordsman"), FVector(0.0f, 1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(MeleeGap, 1000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("swordsman and dummy spawned")) || !C.Equip(Player, TEXT("sword_one_handed")))
		{
			return;
		}
		// 1 + 5 + 8 Skill Capacity.
		AllowFullSkillCapacity(Player);
		C.Require(C.GrantSkills(Player, {TEXT("slash"), TEXT("mortal_strike"), TEXT("shade_blow")}), TEXT("the three skills granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Ready all three: each ready tag is on, each on cooldown"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Swordsman"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("swordsman gone"));
			return;
		}
		for (const TCHAR* Skill : {TEXT("slash"), TEXT("mortal_strike"), TEXT("shade_blow")})
		{
			if (!C.Require(C.ActivateSkill(Player, Skill), FString::Printf(TEXT("%s activates"), Skill)))
			{
				return;
			}
			C.Test.TestTrue(*FString::Printf(TEXT("%s is on cooldown"), Skill), C.IsOnCooldown(Player, Skill));
		}
		C.Test.TestTrue(TEXT("State.NextAttack.Slash is on"), C.HasTag(Player, CrossguardTags::State_NextAttack_Slash));
		C.Test.TestTrue(TEXT("State.NextAttack.MortalStrike is on"), C.HasTag(Player, CrossguardTags::State_NextAttack_MortalStrike));
		C.Test.TestTrue(TEXT("State.NextAttack.ShadeBlow is on"), C.HasTag(Player, CrossguardTags::State_NextAttack_ShadeBlow));
	});

	Scenario.Step(TEXT("One sword hit uses them all: weapon +7, +20 Attack, +20 Magic"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Swordsman"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		C.Aim(Player, Dummy);
		const float Before = C.Health(Dummy);
		const float Plain = C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player));
		const float Full = C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player) + SlashWeaponDamage + MortalStrikeAttackDamage, ShadeBlowMagicDamage);
		const float Expected = FMath::Min(Full, Before);
		if (!C.Require(C.BasicAttack(Player), TEXT("the sword attack activates")))
		{
			return;
		}
		const float Taken = Before - C.Health(Dummy);
		C.Test.AddInfo(FString::Printf(TEXT("Stacked hit: %.2f (expected %.2f; a plain hit is %.2f)"), Taken, Expected, Plain));
		C.Test.TestEqual(TEXT("One hit carries Slash, Mortal Strike and Shade Blow"), Taken, Expected, DamageTolerance);
		C.Test.TestFalse(TEXT("Slash is used up"), C.HasTag(Player, CrossguardTags::State_NextAttack_Slash));
		C.Test.TestFalse(TEXT("Mortal Strike is used up"), C.HasTag(Player, CrossguardTags::State_NextAttack_MortalStrike));
		C.Test.TestFalse(TEXT("Shade Blow is used up"), C.HasTag(Player, CrossguardTags::State_NextAttack_ShadeBlow));
		C.Test.TestTrue(TEXT("Shade Blow's heal block is on the dummy"), C.HasTag(Dummy, CrossguardTags::State_HealBlocked));
		if (Full < Before)
		{
			C.Test.TestTrue(TEXT("Mortal Strike stays on cooldown without a kill"), C.IsOnCooldown(Player, TEXT("mortal_strike")));
		}
	});

	// The sword's next attack (1 attack per second): a plain hit, nothing left over.
	Scenario.WaitUntil(TEXT("The next sword attack is ready"), 3.0f, [](FScenarioContext& C)
	{
		return TryMeleeHit(C, TEXT("Swordsman"), TEXT("Dummy"), TEXT("Plain"));
	});

	Scenario.Step(TEXT("The next hit is a plain one"), [](FScenarioContext& C)
	{
		CheckRecordedHit(C, TEXT("Plain"), TEXT("The following hit has no riders left"));
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "Kill with Mortal Strike: it is castable again at once."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillMortalStrikeKill, "Crossguard.Scenario.Skills.MortalStrikeKillRefresh", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillMortalStrikeKill::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A swordsman with Mortal Strike; a dummy in reach"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Swordsman"), FVector(0.0f, 1500.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(MeleeGap, 1500.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("swordsman and dummy spawned")) || !C.Equip(Player, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Require(C.GrantSkills(Player, {TEXT("mortal_strike")}), TEXT("Mortal Strike granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Mortal Strike, the dummy at 1 HP, hit: it dies and the cooldown is gone"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Swordsman"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		if (!C.Require(C.ActivateSkill(Player, TEXT("mortal_strike")), TEXT("Mortal Strike activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Mortal Strike is on cooldown once readied"), C.IsOnCooldown(Player, TEXT("mortal_strike")));

		// One hit from death.
		UAbilitySystemComponent* DummyASC = ASCOf(Dummy);
		if (!C.Require(DummyASC != nullptr, TEXT("the dummy has an ability system")))
		{
			return;
		}
		DummyASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(), 1.0f);
		C.Aim(Player, Dummy);
		if (!C.Require(C.BasicAttack(Player), TEXT("the sword attack activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The dummy is dead"), C.IsDead(Dummy));
		C.Test.TestFalse(TEXT("Mortal Strike's cooldown is refreshed by the kill"), C.IsOnCooldown(Player, TEXT("mortal_strike")));
		C.Test.TestFalse(TEXT("The readied Mortal Strike was used by the hit"), C.HasTag(Player, CrossguardTags::State_NextAttack_MortalStrike));
		C.Test.TestTrue(TEXT("Mortal Strike can be cast again at once"), C.ActivateSkill(Player, TEXT("mortal_strike")));
		C.Test.TestTrue(TEXT("It is readied again"), C.HasTag(Player, CrossguardTags::State_NextAttack_MortalStrike));
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "gouge (dagger) then hit -> The dummy takes 5 per second for 5 s." (and -5% speed 5 s)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillGouge, "Crossguard.Scenario.Skills.Gouge", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillGouge::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A dagger with Gouge; a dummy in reach"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Rogue"), FVector(0.0f, 2000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(MeleeGap, 2000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("rogue and dummy spawned")) || !C.Equip(Player, TEXT("dagger_one_handed")))
		{
			return;
		}
		C.Require(C.GrantSkills(Player, {TEXT("gouge")}), TEXT("Gouge granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Gouge, then stab: a plain hit, the dummy slowed"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Rogue"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		if (!C.Require(C.ActivateSkill(Player, TEXT("gouge")), TEXT("Gouge activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("State.NextAttack.Gouge is on"), C.HasTag(Player, CrossguardTags::State_NextAttack_Gouge));
		C.Aim(Player, Dummy);
		const float MoveSpeedBefore = C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute());
		C.SetNumber(TEXT("Tick"), C.ExpectedDamage(Player, Dummy, GougeDamagePerSecond));
		if (!TryMeleeHit(C, TEXT("Rogue"), TEXT("Dummy"), TEXT("Stab")))
		{
			C.Fail(TEXT("the dagger attack does not activate"));
			return;
		}
		CheckRecordedHit(C, TEXT("Stab"), TEXT("The gouging stab itself is a plain dagger hit"));
		C.Test.TestFalse(TEXT("Gouge is used up"), C.HasTag(Player, CrossguardTags::State_NextAttack_Gouge));
		C.Test.TestTrue(TEXT("The dummy is slowed (State.Slowed)"), C.HasTag(Dummy, CrossguardTags::State_Slowed));
		C.Test.TestEqual(TEXT("The dummy's Move Speed is down 5%"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			MoveSpeedBefore - GougeSlow, 0.001f);
		C.SetNumber(TEXT("AfterStab"), C.Health(Dummy));
		C.SetNumber(TEXT("MoveSpeedBefore"), MoveSpeedBefore);
	});

	// Ticks every second, none on application.
	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("1.5 s later: one tick of 5"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			C.Fail(TEXT("dummy gone"));
			return;
		}
		const float AfterStab = static_cast<float>(C.Number(TEXT("AfterStab")));
		const float Tick = FMath::Min(static_cast<float>(C.Number(TEXT("Tick"))), AfterStab);
		C.Test.AddInfo(FString::Printf(TEXT("Gouge after 1.5 s: %.2f taken (one tick %.2f)"), AfterStab - C.Health(Dummy), Tick));
		C.Test.TestEqual(TEXT("One Gouge tick after 1.5 s"), AfterStab - C.Health(Dummy), Tick, DamageTolerance);
	});

	Scenario.Wait(GougeSeconds - 1.5f + 1.0f);

	Scenario.Step(TEXT("6 s later: 5 ticks (25) in all, the slow is over"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			C.Fail(TEXT("dummy gone"));
			return;
		}
		const float AfterStab = static_cast<float>(C.Number(TEXT("AfterStab")));
		const float Expected = FMath::Min(static_cast<float>(C.Number(TEXT("Tick"))) * GougeTicks, AfterStab);
		const float Taken = AfterStab - C.Health(Dummy);
		C.Test.AddInfo(FString::Printf(TEXT("Gouge over 5 s: %.2f taken (expected %.2f)"), Taken, Expected));
		C.Test.TestEqual(TEXT("Gouge deals 5 per second for 5 s"), Taken, Expected, DamageTolerance);
		C.Test.TestFalse(TEXT("The slow is over after 5 s"), C.HasTag(Dummy, CrossguardTags::State_Slowed));
		C.Test.TestEqual(TEXT("The dummy's Move Speed is back"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			static_cast<float>(C.Number(TEXT("MoveSpeedBefore"))), 0.001f);
		C.SetNumber(TEXT("AfterDot"), C.Health(Dummy));
	});

	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("No more ticks"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy")))
		{
			C.Test.TestEqual(TEXT("Gouge stopped after 5 s"), C.Health(Dummy), static_cast<float>(C.Number(TEXT("AfterDot"))), 0.001f);
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "shade_blow hit, then heal the dummy -> No healing for 5 s (no 'healed' log line)."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillShadeBlowHealBlock, "Crossguard.Scenario.Skills.ShadeBlowHealBlock", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillShadeBlowHealBlock::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A swordsman with Shade Blow, a dummy, two healers with wands to the sides"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Swordsman"), FVector(0.0f, 2500.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(MeleeGap, 2500.0f, 0.0f), 180.0f);
		// Heal has a 12 s cooldown: one healer during the block, the other after it.
		ACrossguardCharacter* HealerA = C.SpawnPlayer(TEXT("HealerA"), FVector(MeleeGap, 2500.0f + 600.0f, 0.0f), -90.0f);
		ACrossguardCharacter* HealerB = C.SpawnPlayer(TEXT("HealerB"), FVector(MeleeGap, 2500.0f - 600.0f, 0.0f), 90.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr && HealerA != nullptr && HealerB != nullptr, TEXT("everyone spawned")))
		{
			return;
		}
		if (!C.Equip(Player, TEXT("sword_one_handed")) || !C.Equip(HealerA, TEXT("wand_one_handed")) || !C.Equip(HealerB, TEXT("wand_one_handed")))
		{
			return;
		}
		AllowFullSkillCapacity(Player);
		C.Require(C.GrantSkills(Player, {TEXT("shade_blow")}) && C.GrantSkills(HealerA, {TEXT("heal")}) && C.GrantSkills(HealerB, {TEXT("heal")}),
			TEXT("Shade Blow and Heal granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Shade Blow and a sword hit: +20 Magic, the dummy heal-blocked"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Swordsman"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		if (!C.Require(C.ActivateSkill(Player, TEXT("shade_blow")), TEXT("Shade Blow activates")))
		{
			return;
		}
		C.Aim(Player, Dummy);
		if (!TryMeleeHit(C, TEXT("Swordsman"), TEXT("Dummy"), TEXT("Blow"), 0.0f, ShadeBlowMagicDamage))
		{
			C.Fail(TEXT("the sword attack does not activate"));
			return;
		}
		CheckRecordedHit(C, TEXT("Blow"), TEXT("Shade Blow hit: the sword +20 Magic Damage"));
		if (!C.Require(!C.IsDead(Dummy) && C.Health(Dummy) < C.MaxHealth(Dummy), TEXT("the dummy is hurt and alive")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The dummy is heal-blocked (State.HealBlocked)"), C.HasTag(Dummy, CrossguardTags::State_HealBlocked));
	});

	Scenario.Step(TEXT("Healer A heals the dummy: nothing"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Healer = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("HealerA"), TEXT("Dummy"), Healer, Dummy))
		{
			return;
		}
		C.Aim(Healer, Dummy);
		C.ClearLog();
		const float Before = C.Health(Dummy);
		const float HealerBefore = C.Health(Healer);
		if (!C.Require(C.ActivateSkill(Healer, TEXT("heal")), TEXT("Heal activates")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("A heal-blocked dummy gains nothing"), C.Health(Dummy), Before, 0.001f);
		C.Test.TestEqual(TEXT("The heal went to the dummy, not the healer"), C.Health(Healer), HealerBefore, 0.001f);
		C.Test.TestFalse(TEXT("No 'healed' log line"), C.LogContains(TEXT(" healed ")));
	});

	Scenario.Wait(ShadeBlowHealBlockSeconds + 0.5f);

	Scenario.Step(TEXT("5.5 s later: healer B's heal works again"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Healer = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("HealerB"), TEXT("Dummy"), Healer, Dummy))
		{
			return;
		}
		C.Test.TestFalse(TEXT("The heal block is over after 5 s"), C.HasTag(Dummy, CrossguardTags::State_HealBlocked));
		C.Aim(Healer, Dummy);
		C.ClearLog();
		const float Before = C.Health(Dummy);
		const float Expected = ExpectedMagicalHealing(C, Healer, Dummy, HealAmount);
		if (!C.Require(C.ActivateSkill(Healer, TEXT("heal")), TEXT("Heal activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Heal after the block: %.2f -> %.2f (expected +%.2f)"), Before, C.Health(Dummy), Expected));
		C.Test.TestEqual(TEXT("The dummy heals 20 once the block is over"), C.Health(Dummy) - Before, Expected, DamageTolerance);
		C.Test.TestTrue(TEXT("A 'healed' log line"), C.LogContains(TEXT(" healed ")));
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "purge_shot (bow) then shoot the second client -> They are Muted 3 s and +10 damage."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillPurgeShot, "Crossguard.Scenario.Skills.PurgeShot", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillPurgeShot::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("An archer with Purge Shot and arrows; a second player (sword, Slash) 8 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Archer = C.SpawnPlayer(TEXT("Archer"), FVector(0.0f, 3000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Target = C.SpawnPlayer(TEXT("Target"), FVector(800.0f, 3000.0f, 0.0f), 90.0f);
		if (!C.Require(Archer != nullptr && Target != nullptr, TEXT("archer and target spawned")))
		{
			return;
		}
		if (!C.Equip(Archer, TEXT("bow")) || !C.Equip(Target, TEXT("sword_one_handed")))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Archer, TEXT("arrow"), 10) == 0, TEXT("10 arrows in the archer's inventory")))
		{
			return;
		}
		C.Require(C.GrantSkills(Archer, {TEXT("purge_shot")}) && C.GrantSkills(Target, {TEXT("slash")}), TEXT("Purge Shot and Slash granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Purge Shot, then shoot the target"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Archer = nullptr;
		ACrossguardCharacter* Target = nullptr;
		if (!GetBoth(C, TEXT("Archer"), TEXT("Target"), Archer, Target))
		{
			return;
		}
		if (!C.Require(C.ActivateSkill(Archer, TEXT("purge_shot")), TEXT("Purge Shot activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("State.NextAttack.PurgeShot is on"), C.HasTag(Archer, CrossguardTags::State_NextAttack_PurgeShot));
		C.Aim(Archer, Target);
		const float Before = C.Health(Target);
		C.SetNumber(TEXT("Before"), Before);
		C.SetNumber(TEXT("Expected"), FMath::Min(C.ExpectedDamage(Archer, Target, C.WeaponValue(Archer), PurgeShotMagicDamage), Before));
		if (!C.Require(C.BasicAttack(Archer), TEXT("the bow shot activates")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("Purge Shot is used up by the shot"), C.HasTag(Archer, CrossguardTags::State_NextAttack_PurgeShot));
	});

	Scenario.WaitUntil(TEXT("The arrow hits the target"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Target = C.Get<ACrossguardCharacter>(TEXT("Target"));
		return Target == nullptr || C.Health(Target) < static_cast<float>(C.Number(TEXT("Before")));
	});

	Scenario.Step(TEXT("Bow +10 Magic; the target is Muted: no skills, left click still works"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Target = C.Get<ACrossguardCharacter>(TEXT("Target"));
		if (Target == nullptr)
		{
			C.Fail(TEXT("target gone"));
			return;
		}
		const float Taken = static_cast<float>(C.Number(TEXT("Before"))) - C.Health(Target);
		const float Expected = static_cast<float>(C.Number(TEXT("Expected")));
		C.Test.AddInfo(FString::Printf(TEXT("Purge Shot: %.2f taken (expected %.2f)"), Taken, Expected));
		C.Test.TestEqual(TEXT("Purge Shot hit: the bow +10 Magic Damage"), Taken, Expected, DamageTolerance);
		C.Test.TestTrue(TEXT("The target is Muted (State.Muted)"), C.HasTag(Target, CrossguardTags::State_Muted));
		C.Test.TestFalse(TEXT("A Muted target cannot cast Slash"), C.ActivateSkill(Target, TEXT("slash")));
		// Faced away from the archer: nobody in reach.
		C.Face(Target, 90.0f);
		C.Test.TestTrue(TEXT("A Muted target still attacks"), C.BasicAttack(Target));
	});

	Scenario.Wait(PurgeShotMuteSeconds + 0.3f);

	Scenario.Step(TEXT("3 s later: the Mute is over, Slash works"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Target = C.Get<ACrossguardCharacter>(TEXT("Target"));
		if (Target == nullptr)
		{
			C.Fail(TEXT("target gone"));
			return;
		}
		C.Test.TestFalse(TEXT("The Mute lasts 3 s"), C.HasTag(Target, CrossguardTags::State_Muted));
		C.Test.TestTrue(TEXT("Slash can be cast again"), C.ActivateSkill(Target, TEXT("slash")));
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "cleave with a sword next to the dummy -> About 85% of a normal hit." (120 degree arc,
// everyone in it; not behind.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillCleave, "Crossguard.Scenario.Skills.Cleave", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillCleave::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A swordsman with Cleave; a dummy ahead, one ahead-left, one behind"), [](FScenarioContext& C)
	{
		const FVector Origin(0.0f, 3500.0f, 0.0f);
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Swordsman"), Origin, 0.0f);
		ACrossguardCharacter* Front = C.SpawnDummy(TEXT("Front"), Origin + FVector(MeleeGap, 0.0f, 0.0f), 180.0f);
		// 2 m away, 45 degrees off: inside the 3 m, 60 degree half-angle arc, outside a basic swing.
		ACrossguardCharacter* Side = C.SpawnDummy(TEXT("Side"), Origin + FVector(141.0f, 141.0f, 0.0f), 225.0f);
		ACrossguardCharacter* Behind = C.SpawnDummy(TEXT("Behind"), Origin + FVector(-MeleeGap, 0.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr && Front != nullptr && Side != nullptr && Behind != nullptr, TEXT("everyone spawned"))
			|| !C.Equip(Player, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Require(C.GrantSkills(Player, {TEXT("cleave")}), TEXT("Cleave granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Cleave: 85% of the sword to the front and side dummies, nothing behind"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Swordsman"));
		ACrossguardCharacter* Front = C.Get<ACrossguardCharacter>(TEXT("Front"));
		ACrossguardCharacter* Side = C.Get<ACrossguardCharacter>(TEXT("Side"));
		ACrossguardCharacter* Behind = C.Get<ACrossguardCharacter>(TEXT("Behind"));
		if (Player == nullptr || Front == nullptr || Side == nullptr || Behind == nullptr)
		{
			C.Fail(TEXT("someone is gone"));
			return;
		}
		C.Face(Player, 0.0f);
		const float Base = C.WeaponValue(Player) * CleaveWeaponFraction;
		const float FrontBefore = C.Health(Front);
		const float SideBefore = C.Health(Side);
		const float BehindBefore = C.Health(Behind);
		const float FrontExpected = FMath::Min(C.ExpectedDamage(Player, Front, Base), FrontBefore);
		const float SideExpected = FMath::Min(C.ExpectedDamage(Player, Side, Base), SideBefore);
		if (!C.Require(C.ActivateSkill(Player, TEXT("cleave")), TEXT("Cleave activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Cleave is on cooldown"), C.IsOnCooldown(Player, TEXT("cleave")));
		C.Test.AddInfo(FString::Printf(TEXT("Cleave: front %.2f (expected %.2f), side %.2f (expected %.2f)"), FrontBefore - C.Health(Front),
			FrontExpected, SideBefore - C.Health(Side), SideExpected));
		C.Test.TestEqual(TEXT("Cleave hits the dummy in front for 85% of the sword"), FrontBefore - C.Health(Front), FrontExpected, DamageTolerance);
		C.Test.TestEqual(TEXT("Cleave hits the dummy 45 degrees off too"), SideBefore - C.Health(Side), SideExpected, DamageTolerance);
		C.Test.TestEqual(TEXT("Cleave misses the dummy behind"), C.Health(Behind), BehindBefore, 0.001f);
		C.SetNumber(TEXT("CleaveTaken"), FrontBefore - C.Health(Front));
	});

	Scenario.Step(TEXT("A normal sword hit on the front dummy, for comparison"), [](FScenarioContext& C)
	{
		if (!TryMeleeHit(C, TEXT("Swordsman"), TEXT("Front"), TEXT("Normal")))
		{
			C.Fail(TEXT("the sword attack does not activate"));
			return;
		}
		CheckRecordedHit(C, TEXT("Normal"), TEXT("A normal sword hit"));
		const float Normal = static_cast<float>(C.Number(TEXT("Normal.Taken")));
		if (Normal > 0.0f)
		{
			C.Test.AddInfo(FString::Printf(TEXT("Cleave / normal hit = %.3f (85%% of the weapon before stats)"),
				static_cast<float>(C.Number(TEXT("CleaveTaken"))) / Normal));
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "parting_the_land with sword_two_handed -> The dummy takes 65 and pops up; two walls
// (boxes) appear for 15 s."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillPartingTheLand, "Crossguard.Scenario.Skills.PartingTheLand", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillPartingTheLand::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);
	// The walls last 15 game seconds.
	Scenario.TimeDilation(4.0f);

	Scenario.Step(TEXT("A two-handed sword with Parting the Land; a dummy 4 m ahead"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Breaker"), FVector(0.0f, 4000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(400.0f, 4000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("breaker and dummy spawned")) || !C.Equip(Player, TEXT("sword_two_handed")))
		{
			return;
		}
		AllowFullSkillCapacity(Player);
		C.Require(C.GrantSkills(Player, {TEXT("parting_the_land")}), TEXT("Parting the Land granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Parting the Land: 65 to the dummy, knocked up, two walls"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Breaker"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		C.Face(Player, 0.0f);
		C.Test.TestEqual(TEXT("No walls before"), CountWallsOwnedBy(C, Player), 0);
		const float Before = C.Health(Dummy);
		const float Expected = FMath::Min(C.ExpectedDamage(Player, Dummy, PartingDamage), Before);
		C.SetNumber(TEXT("DummyZ"), Dummy->GetActorLocation().Z);
		if (!C.Require(C.ActivateSkill(Player, TEXT("parting_the_land")), TEXT("Parting the Land activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Parting the Land: %.2f taken (expected %.2f)"), Before - C.Health(Dummy), Expected));
		C.Test.TestEqual(TEXT("The dummy takes 65 Attack Damage"), Before - C.Health(Dummy), Expected, DamageTolerance);
		C.Test.TestTrue(TEXT("Parting the Land is on cooldown"), C.IsOnCooldown(Player, TEXT("parting_the_land")));
		if (!C.IsDead(Dummy))
		{
			C.Test.TestTrue(TEXT("The dummy is knocked up (State.KnockedUp)"), C.HasTag(Dummy, CrossguardTags::State_KnockedUp));
		}
		C.Test.TestEqual(TEXT("Two walls appear"), CountWallsOwnedBy(C, Player), 2);
	});

	Scenario.Wait(0.25f);

	Scenario.Step(TEXT("The dummy is in the air"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Dummy == nullptr || C.IsDead(Dummy))
		{
			return;
		}
		const float Rise = Dummy->GetActorLocation().Z - static_cast<float>(C.Number(TEXT("DummyZ")));
		C.Test.AddInfo(FString::Printf(TEXT("Knock-up: the dummy is %.0f cm up after 0.25 s"), Rise));
		C.Test.TestTrue(TEXT("The dummy pops up (30+ cm after 0.25 s)"), Rise > 30.0f);
	});

	Scenario.Wait(WallSeconds - 1.0f);

	Scenario.Step(TEXT("14 s: the walls still stand; the knock-up is over"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Breaker"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		C.Test.TestEqual(TEXT("Both walls stand at 14 s"), CountWallsOwnedBy(C, Player), 2);
		C.Test.TestFalse(TEXT("The knock-up has ended"), C.HasTag(Dummy, CrossguardTags::State_KnockedUp));
	});

	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("15.5 s: the walls are gone"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Breaker")))
		{
			C.Test.TestEqual(TEXT("The walls last 15 s"), CountWallsOwnedBy(C, Player), 0);
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "hunters_leap -> You leap forward (5 m at 10 AGI)."
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillHuntersLeap, "Crossguard.Scenario.Skills.HuntersLeap", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillHuntersLeap::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with Hunter's Leap facing +X"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Hunter"), FVector(-1000.0f, 1000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("hunter spawned")))
		{
			return;
		}
		C.Require(C.GrantSkills(Player, {TEXT("hunters_leap")}), TEXT("Hunter's Leap granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Leap"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Hunter"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("hunter gone"));
			return;
		}
		const float Agility = C.Stat(Player, UCrossguardAttributeSet::GetAgilityAttribute());
		if (!C.Require(Agility > 0.0f, FString::Printf(TEXT("the hunter has Agility to leap with (%.1f)"), Agility)))
		{
			return;
		}
		C.Face(Player, 0.0f);
		const FVector Start = Player->GetActorLocation();
		C.SetNumber(TEXT("StartX"), Start.X);
		C.SetNumber(TEXT("StartY"), Start.Y);
		C.SetNumber(TEXT("StartZ"), Start.Z);
		C.SetNumber(TEXT("Expected"), Agility * LeapCmPerAgility);
		if (!C.Require(C.ActivateSkill(Player, TEXT("hunters_leap")), TEXT("Hunter's Leap activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Hunter's Leap is on cooldown"), C.IsOnCooldown(Player, TEXT("hunters_leap")));
	});

	// 0.6 s leap, then the landing slide.
	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("Landed about 0.5 m per AGI point ahead"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Hunter"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("hunter gone"));
			return;
		}
		const FVector End = Player->GetActorLocation();
		const float Forward = End.X - static_cast<float>(C.Number(TEXT("StartX")));
		const float Sideways = FMath::Abs(End.Y - static_cast<float>(C.Number(TEXT("StartY"))));
		const float Expected = static_cast<float>(C.Number(TEXT("Expected")));
		C.Test.AddInfo(FString::Printf(TEXT("Hunter's Leap: %.0f cm forward, %.0f sideways (expected %.0f; at 10 AGI 500)"), Forward, Sideways, Expected));
		// The root motion covers the distance; the landing (velocity clamped to walking speed) may slide
		// a little further.
		C.Test.TestTrue(TEXT("Leapt at least 90% of 0.5 m x AGI forward"), Forward >= Expected * 0.9f);
		C.Test.TestTrue(TEXT("Leapt no more than 2 m past 0.5 m x AGI"), Forward <= Expected + 200.0f);
		C.Test.TestTrue(TEXT("Straight ahead"), Sideways < 50.0f);
		C.Test.TestEqual(TEXT("Back on the floor"), End.Z, static_cast<float>(C.Number(TEXT("StartZ"))), 20.0f);
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Not in the checklist table; docs/design-decisions.md "Like A Lion, He Leapt": leaps at the nearest
// enemy in sight within 30 m, lands 1.5 m short, STR x1.2 for 6 s; no enemy: fails, no cooldown.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillLikeALion, "Crossguard.Scenario.Skills.LikeALion", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillLikeALion::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	// Far (78 m) from the PIE player at (-30 m, -30 m), which would be a target within 30 m.
	static const FVector Origin(2500.0f, 2500.0f, 0.0f);

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A swordsman with Like A Lion, alone"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Lion"), Origin, 0.0f);
		if (!C.Require(Player != nullptr, TEXT("lion spawned")) || !C.Equip(Player, TEXT("sword_one_handed")))
		{
			return;
		}
		AllowFullSkillCapacity(Player);
		C.Require(C.GrantSkills(Player, {TEXT("like_a_lion_he_leapt")}), TEXT("Like A Lion granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("No enemy in range: it fails"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lion"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("lion gone"));
			return;
		}
		if (const ACrossguardCharacter* Local = C.LocalPlayer())
		{
			C.Test.TestTrue(TEXT("The PIE player is out of the leap's range"),
				FVector::Dist(Local->GetActorLocation(), Player->GetActorLocation()) > LionRange);
		}
		C.SetNumber(TEXT("StartX"), Player->GetActorLocation().X);
		C.SetNumber(TEXT("StartY"), Player->GetActorLocation().Y);
		// The activation itself may report success: the ability starts, then ends without committing.
		C.ActivateSkill(Player, TEXT("like_a_lion_he_leapt"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("No cooldown, no movement; a dummy appears 10 m ahead"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lion"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("lion gone"));
			return;
		}
		C.Test.TestFalse(TEXT("No target: no cooldown"), C.IsOnCooldown(Player, TEXT("like_a_lion_he_leapt")));
		const FVector Start(static_cast<float>(C.Number(TEXT("StartX"))), static_cast<float>(C.Number(TEXT("StartY"))), 0.0f);
		C.Test.TestTrue(TEXT("No target: no leap"), FVector::Dist2D(Player->GetActorLocation(), Start) < 10.0f);
		C.Require(C.SpawnDummy(TEXT("Prey"), Origin + FVector(1000.0f, 0.0f, 0.0f), 180.0f) != nullptr, TEXT("prey spawned"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Leap at the dummy: cooldown, STR x1.2"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Prey = nullptr;
		if (!GetBoth(C, TEXT("Lion"), TEXT("Prey"), Player, Prey))
		{
			return;
		}
		// Facing away: the leap finds its target itself.
		C.Face(Player, 90.0f);
		const float Strength = C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute());
		C.SetNumber(TEXT("Strength"), Strength);
		C.SetNumber(TEXT("StartX"), Player->GetActorLocation().X);
		C.SetNumber(TEXT("StartY"), Player->GetActorLocation().Y);
		if (!C.Require(C.ActivateSkill(Player, TEXT("like_a_lion_he_leapt")), TEXT("Like A Lion activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Like A Lion is on cooldown"), C.IsOnCooldown(Player, TEXT("like_a_lion_he_leapt")));
		C.Test.TestEqual(TEXT("Strength x1.2"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()), Strength * LionStrength, 0.05f);
	});

	// 0.4 s + 8.5 m / 25 m/s, then the landing.
	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("Landed about 1.5 m short of the dummy"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Prey = nullptr;
		if (!GetBoth(C, TEXT("Lion"), TEXT("Prey"), Player, Prey))
		{
			return;
		}
		const FVector Start(static_cast<float>(C.Number(TEXT("StartX"))), static_cast<float>(C.Number(TEXT("StartY"))), 0.0f);
		const float Moved = FVector::Dist2D(Player->GetActorLocation(), Start);
		const float Gap = FVector::Dist2D(Player->GetActorLocation(), Prey->GetActorLocation());
		C.Test.AddInfo(FString::Printf(TEXT("Like A Lion: moved %.0f cm, %.0f cm from the dummy (lands %.0f short)"), Moved, Gap, LionStopShort));
		C.Test.TestTrue(TEXT("Leapt most of the 10 m"), Moved >= 1000.0f - LionStopShort - 150.0f);
		C.Test.TestTrue(TEXT("Landed next to the dummy, not past it"), Gap <= LionStopShort + 150.0f && Gap >= 50.0f);
	});

	Scenario.Wait(LionBuffSeconds);

	Scenario.Step(TEXT("7.5 s later: Strength is back"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lion")))
		{
			C.Test.TestEqual(TEXT("The Strength buff lasts 6 s"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()),
				static_cast<float>(C.Number(TEXT("Strength"))), 0.05f);
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "spear_two_handed as DebugMainHand -> Hits for 33 once every 1.2 s" (first half).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillSpearRate, "Crossguard.Scenario.Skills.SpearAttackRate", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillSpearRate::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A spear and a dummy in reach"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Spearman"), FVector(-1000.0f, 2000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(-1000.0f + MeleeGap, 2000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("spearman and dummy spawned")))
		{
			return;
		}
		C.Equip(Player, TEXT("spear_two_handed"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("First thrust: 33 before stats"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Spearman"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		C.Test.TestEqual(TEXT("A Common spear is 33 Attack Damage"), C.WeaponValue(Player), SpearValue, 0.001f);
		const UCrossguardEquipmentComponent* Equipment = Player->GetEquipmentComponent();
		const float ActionSpeed = C.Stat(Player, UCrossguardAttributeSet::GetActionSpeedAttribute());
		const float Interval = Equipment != nullptr ? Equipment->GetAttackInterval(ECrossguardEquipmentSlot::MainHand) : 0.0f;
		C.Test.AddInfo(FString::Printf(TEXT("Spear interval %.3f s at Action Speed %.3f"), Interval, ActionSpeed));
		if (ActionSpeed == 0.0f)
		{
			C.Test.TestEqual(TEXT("Once every 1.2 s"), Interval, SpearSecondsPerAttack, 0.001f);
		}
		C.SetNumber(TEXT("Interval"), Interval);
		C.Aim(Player, Dummy);
		if (!TryMeleeHit(C, TEXT("Spearman"), TEXT("Dummy"), TEXT("First")))
		{
			C.Fail(TEXT("the spear attack does not activate"));
			return;
		}
		CheckRecordedHit(C, TEXT("First"), TEXT("The spear hits for its value"));
	});

	Scenario.Step(TEXT("Next frame: still recovering"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Spearman")))
		{
			C.Test.TestFalse(TEXT("No second thrust right away"), C.BasicAttack(Player));
		}
	});

	Scenario.WaitUntil(TEXT("The second thrust starts"), 3.0f, [](FScenarioContext& C)
	{
		return TryMeleeHit(C, TEXT("Spearman"), TEXT("Dummy"), TEXT("Second"));
	});

	Scenario.Step(TEXT("Second thrust one interval later, same damage"), [](FScenarioContext& C)
	{
		CheckRecordedHit(C, TEXT("Second"), TEXT("The second spear hit"));
		const float Interval = static_cast<float>(C.Number(TEXT("Interval")));
		const float Elapsed = static_cast<float>(C.Number(TEXT("Second.Time")) - C.Number(TEXT("First.Time")));
		C.Test.AddInfo(FString::Printf(TEXT("Second thrust after %.3f s (interval %.3f)"), Elapsed, Interval));
		// Polled once a frame.
		C.Test.TestTrue(TEXT("Not before the interval"), Elapsed >= Interval - 0.02f);
		C.Test.TestTrue(TEXT("Right after the interval"), Elapsed <= Interval + 0.15f);
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "spear_two_handed ... triple_thrust works." Three thrusts 0.15 s apart, spear +15 each.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillTripleThrust, "Crossguard.Scenario.Skills.TripleThrust", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillTripleThrust::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A spear with Triple Thrust and a dummy in reach"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Spearman"), FVector(-1000.0f, 3000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(-1000.0f + MeleeGap, 3000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("spearman and dummy spawned")) || !C.Equip(Player, TEXT("spear_two_handed")))
		{
			return;
		}
		AllowFullSkillCapacity(Player);
		C.Require(C.GrantSkills(Player, {TEXT("triple_thrust")}), TEXT("Triple Thrust granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Triple Thrust: the first thrust lands at once"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Spearman"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		C.Aim(Player, Dummy);
		const float Before = C.Health(Dummy);
		const float PerThrust = C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player) + TripleThrustBonus);
		C.SetNumber(TEXT("Before"), Before);
		C.SetNumber(TEXT("PerThrust"), PerThrust);
		if (!C.Require(C.ActivateSkill(Player, TEXT("triple_thrust")), TEXT("Triple Thrust activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Triple Thrust is on cooldown"), C.IsOnCooldown(Player, TEXT("triple_thrust")));
		C.Test.TestEqual(TEXT("First thrust: the spear +15"), Before - C.Health(Dummy), FMath::Min(PerThrust, Before), DamageTolerance);
	});

	Scenario.Wait(TripleThrustSeconds + 0.3f);

	Scenario.Step(TEXT("All three thrusts landed"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			C.Fail(TEXT("dummy gone"));
			return;
		}
		const float Before = static_cast<float>(C.Number(TEXT("Before")));
		const float Expected = FMath::Min(static_cast<float>(C.Number(TEXT("PerThrust"))) * TripleThrustCount, Before);
		C.Test.AddInfo(FString::Printf(TEXT("Triple Thrust: %.2f taken (expected %.2f)"), Before - C.Health(Dummy), Expected));
		C.Test.TestEqual(TEXT("Three thrusts of the spear +15"), Before - C.Health(Dummy), Expected, DamageTolerance);
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Not in the checklist table; Skills.json quickshot: +5% Move Speed and +25% Action Speed for 8 s
// (the bow shoots faster).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillQuickshot, "Crossguard.Scenario.Skills.Quickshot", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillQuickshot::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("An archer with Quickshot"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Archer"), FVector(-1000.0f, 4000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("archer spawned")) || !C.Equip(Player, TEXT("bow")))
		{
			return;
		}
		C.Require(C.GrantSkills(Player, {TEXT("quickshot")}), TEXT("Quickshot granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Quickshot: +0.25 Action Speed, +0.05 Move Speed, a shorter bow interval"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Archer"));
		const UCrossguardEquipmentComponent* Equipment = Player != nullptr ? Player->GetEquipmentComponent() : nullptr;
		if (Equipment == nullptr)
		{
			C.Fail(TEXT("archer gone"));
			return;
		}
		const float ActionSpeed = C.Stat(Player, UCrossguardAttributeSet::GetActionSpeedAttribute());
		const float MoveSpeed = C.Stat(Player, UCrossguardAttributeSet::GetMoveSpeedAttribute());
		const float Interval = Equipment->GetAttackInterval(ECrossguardEquipmentSlot::MainHand);
		C.SetNumber(TEXT("ActionSpeed"), ActionSpeed);
		C.SetNumber(TEXT("MoveSpeed"), MoveSpeed);
		C.SetNumber(TEXT("Interval"), Interval);
		if (!C.Require(C.ActivateSkill(Player, TEXT("quickshot")), TEXT("Quickshot activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Quickshot is on cooldown"), C.IsOnCooldown(Player, TEXT("quickshot")));
		C.Test.TestEqual(TEXT("+25% Action Speed"), C.Stat(Player, UCrossguardAttributeSet::GetActionSpeedAttribute()),
			ActionSpeed + QuickshotActionSpeed, 0.001f);
		C.Test.TestEqual(TEXT("+5% Move Speed"), C.Stat(Player, UCrossguardAttributeSet::GetMoveSpeedAttribute()), MoveSpeed + QuickshotMoveSpeed, 0.001f);
		const float Faster = Equipment->GetAttackInterval(ECrossguardEquipmentSlot::MainHand);
		C.Test.AddInfo(FString::Printf(TEXT("Bow interval %.3f -> %.3f s"), Interval, Faster));
		C.Test.TestTrue(TEXT("The bow shoots faster"), Faster < Interval);
		if (ActionSpeed >= 0.0f)
		{
			// (seconds per attack) / (1 + Action Speed).
			C.Test.TestEqual(TEXT("Interval / (1 + Action Speed)"), Faster, Interval * (1.0f + ActionSpeed) / (1.0f + ActionSpeed + QuickshotActionSpeed), 0.001f);
		}
	});

	Scenario.Wait(BowBuffSeconds + 0.3f);

	Scenario.Step(TEXT("8 s later: back to normal"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Archer"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("archer gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Action Speed back after 8 s"), C.Stat(Player, UCrossguardAttributeSet::GetActionSpeedAttribute()),
			static_cast<float>(C.Number(TEXT("ActionSpeed"))), 0.001f);
		C.Test.TestEqual(TEXT("Move Speed back after 8 s"), C.Stat(Player, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			static_cast<float>(C.Number(TEXT("MoveSpeed"))), 0.001f);
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Not in the checklist table; Skills.json arcane_arrows: bow attacks +5 Magic Damage for 8 s
// (State.ArcaneArrows).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillArcaneArrows, "Crossguard.Scenario.Skills.ArcaneArrows", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillArcaneArrows::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("An archer with Arcane Arrows and arrows; a dummy 8 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Archer"), FVector(-1000.0f, -1000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(-200.0f, -1000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("archer and dummy spawned")) || !C.Equip(Player, TEXT("bow")))
		{
			return;
		}
		if (!C.Require(C.GiveItem(Player, TEXT("arrow"), 10) == 0, TEXT("10 arrows in the archer's inventory")))
		{
			return;
		}
		C.Require(C.GrantSkills(Player, {TEXT("arcane_arrows")}), TEXT("Arcane Arrows granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Arcane Arrows, then shoot"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Archer"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		if (!C.Require(C.ActivateSkill(Player, TEXT("arcane_arrows")), TEXT("Arcane Arrows activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("State.ArcaneArrows is on"), C.HasTag(Player, CrossguardTags::State_ArcaneArrows));
		C.Test.TestTrue(TEXT("Arcane Arrows is on cooldown"), C.IsOnCooldown(Player, TEXT("arcane_arrows")));
		C.Aim(Player, Dummy);
		const float Before = C.Health(Dummy);
		C.SetNumber(TEXT("Before"), Before);
		C.SetNumber(TEXT("Expected"), FMath::Min(C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player), ArcaneArrowsMagicDamage), Before));
		C.Require(C.BasicAttack(Player), TEXT("the bow shot activates"));
	});

	Scenario.WaitUntil(TEXT("The arrow hits"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		return Dummy == nullptr || C.Health(Dummy) < static_cast<float>(C.Number(TEXT("Before")));
	});

	Scenario.Step(TEXT("Bow +5 Magic; the buff stays on"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Archer"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		const float Taken = static_cast<float>(C.Number(TEXT("Before"))) - C.Health(Dummy);
		C.Test.AddInfo(FString::Printf(TEXT("Arcane arrow: %.2f taken (expected %.2f)"), Taken, static_cast<float>(C.Number(TEXT("Expected")))));
		C.Test.TestEqual(TEXT("An arcane arrow: the bow +5 Magic Damage"), Taken, static_cast<float>(C.Number(TEXT("Expected"))), DamageTolerance);
		C.Test.TestTrue(TEXT("Arcane Arrows is not used up by a shot"), C.HasTag(Player, CrossguardTags::State_ArcaneArrows));
	});

	Scenario.Wait(BowBuffSeconds);

	Scenario.Step(TEXT("8 s later: the buff is over; shoot again"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Archer"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		C.Test.TestFalse(TEXT("Arcane Arrows lasts 8 s"), C.HasTag(Player, CrossguardTags::State_ArcaneArrows));
		C.Aim(Player, Dummy);
		const float Before = C.Health(Dummy);
		C.SetNumber(TEXT("Before"), Before);
		C.SetNumber(TEXT("Expected"), FMath::Min(C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player)), Before));
		C.Require(C.BasicAttack(Player), TEXT("the second bow shot activates"));
	});

	Scenario.WaitUntil(TEXT("The second arrow hits"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		return Dummy == nullptr || C.Health(Dummy) < static_cast<float>(C.Number(TEXT("Before")));
	});

	Scenario.Step(TEXT("A plain arrow"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy")))
		{
			C.Test.TestEqual(TEXT("After the buff: a plain bow hit"), static_cast<float>(C.Number(TEXT("Before"))) - C.Health(Dummy),
				static_cast<float>(C.Number(TEXT("Expected"))), DamageTolerance);
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "Passive skills: put heroic_might ... Strength +10" (and Mute turns passives off,
// docs/design-decisions.md Purge Shot / Mute).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillHeroicMight, "Crossguard.Scenario.Skills.HeroicMight", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillHeroicMight::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player learns Heroic Might"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Hero"), FVector(-1000.0f, -2000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("hero spawned")))
		{
			return;
		}
		if (!HasSkillCatalog(Player))
		{
			C.Skip(TEXT("The character Blueprint's LoadoutComponent has no Skill Catalog: data passives cannot be granted"));
			return;
		}
		C.SetNumber(TEXT("Strength"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()));
		C.Require(C.GrantSkills(Player, {TEXT("heroic_might")}), TEXT("Heroic Might granted"));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Strength +10; Muted: the bonus is off"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Hero"));
		UAbilitySystemComponent* ASC = ASCOf(Player);
		if (ASC == nullptr)
		{
			C.Fail(TEXT("hero gone"));
			return;
		}
		const float Strength = static_cast<float>(C.Number(TEXT("Strength")));
		C.Test.TestEqual(TEXT("Heroic Might: Strength +10"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()),
			Strength + HeroicMightStrength, 0.001f);
		UCrossguardSkillAbility::ApplyOnHitSpec(UCrossguardSkillAbility::MakeMuteSpec(ASC, 2.0f, 0.0f), ASC);
		C.Test.TestTrue(TEXT("Muted"), C.HasTag(Player, CrossguardTags::State_Muted));
		C.Test.TestEqual(TEXT("Muted: Heroic Might is off"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()), Strength, 0.001f);
	});

	Scenario.Wait(2.3f);

	Scenario.Step(TEXT("The Mute is over: Strength +10 again"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Hero")))
		{
			C.Test.TestFalse(TEXT("The Mute is over"), C.HasTag(Player, CrossguardTags::State_Muted));
			C.Test.TestEqual(TEXT("Heroic Might is back"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()),
				static_cast<float>(C.Number(TEXT("Strength"))) + HeroicMightStrength, 0.001f);
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist: "Passive skills: ... a mastery ... (masteries only while their weapon is held)".
// sword_mastery: +5 Additional Attack Damage, +10% Action Speed, -10% melee skill cooldowns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillSwordMastery, "Crossguard.Scenario.Skills.SwordMastery", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillSwordMastery::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	struct FMasteryStats
	{
		static void Record(FScenarioContext& C, const AActor* Actor, const TCHAR* Prefix)
		{
			C.SetNumber(Key(Prefix, TEXT("AAD")), C.Stat(Actor, UCrossguardAttributeSet::GetAdditionalAttackDamageAttribute()));
			C.SetNumber(Key(Prefix, TEXT("AS")), C.Stat(Actor, UCrossguardAttributeSet::GetActionSpeedAttribute()));
			C.SetNumber(Key(Prefix, TEXT("CDR")), C.Stat(Actor, UCrossguardAttributeSet::GetMeleeCooldownReductionAttribute()));
		}

		static void Check(FScenarioContext& C, const AActor* Actor, const TCHAR* Prefix, float AAD, float AS, float CDR, const TCHAR* What)
		{
			C.Test.TestEqual(*FString::Printf(TEXT("%s: Additional Attack Damage"), What),
				C.Stat(Actor, UCrossguardAttributeSet::GetAdditionalAttackDamageAttribute()), static_cast<float>(C.Number(Key(Prefix, TEXT("AAD")))) + AAD, 0.001f);
			C.Test.TestEqual(*FString::Printf(TEXT("%s: Action Speed"), What), C.Stat(Actor, UCrossguardAttributeSet::GetActionSpeedAttribute()),
				static_cast<float>(C.Number(Key(Prefix, TEXT("AS")))) + AS, 0.001f);
			C.Test.TestEqual(*FString::Printf(TEXT("%s: melee skill CDR"), What),
				C.Stat(Actor, UCrossguardAttributeSet::GetMeleeCooldownReductionAttribute()), static_cast<float>(C.Number(Key(Prefix, TEXT("CDR")))) + CDR, 0.001f);
		}
	};

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a dagger"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Fencer"), FVector(-1000.0f, -3000.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("fencer spawned")))
		{
			return;
		}
		if (!HasSkillCatalog(Player))
		{
			C.Skip(TEXT("The character Blueprint's LoadoutComponent has no Skill Catalog: data passives cannot be granted"));
			return;
		}
		C.Equip(Player, TEXT("dagger_one_handed"));
	});

	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("Stats with the dagger, then with a sword"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Fencer"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("fencer gone"));
			return;
		}
		FMasteryStats::Record(C, Player, TEXT("Dagger"));
		C.Equip(Player, TEXT("sword_one_handed"));
	});

	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("Learn Sword Mastery with the sword in hand"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Fencer"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("fencer gone"));
			return;
		}
		FMasteryStats::Record(C, Player, TEXT("Sword"));
		C.Require(C.GrantSkills(Player, {TEXT("sword_mastery")}), TEXT("Sword Mastery granted"));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Sword in hand: +5 AAD, +10% Action Speed, +10% melee CDR; switch to the dagger"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Fencer"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("fencer gone"));
			return;
		}
		FMasteryStats::Check(C, Player, TEXT("Sword"), SwordMasteryAttackDamage, SwordMasteryActionSpeed, SwordMasteryMeleeCDR, TEXT("Sword Mastery with a sword"));
		C.Equip(Player, TEXT("dagger_one_handed"));
	});

	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("Dagger in hand: no mastery bonus; back to the sword"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Fencer"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("fencer gone"));
			return;
		}
		FMasteryStats::Check(C, Player, TEXT("Dagger"), 0.0f, 0.0f, 0.0f, TEXT("Sword Mastery with a dagger"));
		C.Equip(Player, TEXT("sword_one_handed"));
	});

	Scenario.Wait(0.2f);

	Scenario.Step(TEXT("Sword again: the bonus is back"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Fencer")))
		{
			FMasteryStats::Check(C, Player, TEXT("Sword"), SwordMasteryAttackDamage, SwordMasteryActionSpeed, SwordMasteryMeleeCDR,
				TEXT("Sword Mastery with the sword again"));
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Checklist (Parry row, last part): "client 1's next hit gets +50% ADB". The parry itself is in the
// combat scenarios; this readies the riposte the way a parry does (State.NextAttack.Riposte,
// CrossguardGuard.cpp) and checks the rider in GatherRiders: +ParryAttackDamageBonus Attack Damage
// Bonus on the next attack only.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioWeaponSkillRiposteRider, "Crossguard.Scenario.Skills.RiposteRider", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioWeaponSkillRiposteRider::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioWeaponSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A swordsman and a dummy in reach"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Duelist"), FVector(-1000.0f, -4000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(-1000.0f + MeleeGap, -4000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("duelist and dummy spawned")))
		{
			return;
		}
		C.Equip(Player, TEXT("sword_one_handed"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("A readied riposte: the hit gets +50% Attack Damage Bonus"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = nullptr;
		ACrossguardCharacter* Dummy = nullptr;
		if (!GetBoth(C, TEXT("Duelist"), TEXT("Dummy"), Player, Dummy))
		{
			return;
		}
		UAbilitySystemComponent* ASC = ASCOf(Player);
		if (!C.Require(ASC != nullptr, TEXT("the duelist has an ability system")))
		{
			return;
		}
		// As a won parry does (its duration does not matter here).
		UCrossguardSkillAbility::ApplyStatus(ASC, ASC, CrossguardTags::State_NextAttack_Riposte, 3.0f);
		if (!C.Require(C.HasTag(Player, CrossguardTags::State_NextAttack_Riposte), TEXT("State.NextAttack.Riposte is on")))
		{
			return;
		}
		C.Aim(Player, Dummy);
		const float Bonus = static_cast<float>(CrossguardGuard::Config().ParryAttackDamageBonus);
		C.Test.TestEqual(TEXT("The riposte bonus is +50% ADB"), Bonus, 0.5f, 0.0001f);
		const float Before = C.Health(Dummy);
		const float Expected = FMath::Min(ExpectedWithExtraAttackBonus(C, Player, Dummy, C.WeaponValue(Player), Bonus), Before);
		const float Plain = C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player));
		if (!C.Require(C.BasicAttack(Player), TEXT("the sword attack activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Riposte: %.2f taken (expected %.2f; plain %.2f)"), Before - C.Health(Dummy), Expected, Plain));
		C.Test.TestEqual(TEXT("The riposte hit has +50% ADB"), Before - C.Health(Dummy), Expected, DamageTolerance);
		C.Test.TestFalse(TEXT("The riposte is used up"), C.HasTag(Player, CrossguardTags::State_NextAttack_Riposte));
	});

	Scenario.WaitUntil(TEXT("The next sword attack is ready"), 3.0f, [](FScenarioContext& C)
	{
		return TryMeleeHit(C, TEXT("Duelist"), TEXT("Dummy"), TEXT("Plain"));
	});

	Scenario.Step(TEXT("The following hit is plain"), [](FScenarioContext& C)
	{
		CheckRecordedHit(C, TEXT("Plain"), TEXT("Only the next attack gets the riposte"));
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
