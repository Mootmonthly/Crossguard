// Scenario tests: support, utility, form and summoning skills (docs/next-build-checklist.md
// section 4, skills table). Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// Expected numbers come from Data/Skills/Skills.json, docs/design-decisions.md ("Skill numbers
// filled in") and the skills' C++ (Source/Crossguard/Private/Skills/). Damage goes through
// C.ExpectedDamage, healing through Crossguard::CalculateHealing, with the characters' current stats.
//
// Skill Capacity: a scenario character starts at level 1 (6 Capacity) unless its Blueprint says
// otherwise, too little for most of these skills. EnsureSkillCapacity below raises the character's
// Level (with bUseDebugProgression) until the loadout fits, without re-initializing its attributes.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CrossguardRules/Combat.h"
#include "CrossguardRules/Stats.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayAbilitySpec.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Skills/CrossguardLightOrb.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "Skills/CrossguardSkillAbility.h"
#include "Skills/CrossguardTrackingComponent.h"
#include "Skills/CrossguardWall.h"

namespace CrossguardScenarioSupportSkills
{
	using CrossguardScenario::FScenarioContext;

	// The scene's row (cm): every test runs in its own map; this keeps it well away from the PIE player
	// at (-30 m, -30 m) and outside minion aggro range (30 m) of it.
	constexpr float Row = 1000.0f;

	// Skill Capacity costs (Data/Skills/Skills.json, capacityCost).
	constexpr int32 CostHeal = 2;
	constexpr int32 CostMute = 10;
	constexpr int32 CostEldritchShield = 5;
	constexpr int32 CostChopDown = 5;
	constexpr int32 CostShootingStar = 6;
	constexpr int32 CostDragonification = 13;
	constexpr int32 CostFlight = 8;
	constexpr int32 CostWall = 1;
	constexpr int32 CostLight = 1;
	constexpr int32 CostStealth = 6;
	constexpr int32 CostImmutable = 9;
	constexpr int32 CostUnleash = 11;
	constexpr int32 CostBenediction = 10;
	constexpr int32 CostMultiscale = 9;
	constexpr int32 CostHuntersTracking = 2;
	constexpr int32 CostSummonSkeletons = 4;
	constexpr int32 CostSummonTiger = 5;
	constexpr int32 CostSummonGreaterDemon = 10;

	// Skill numbers (Skills.json; design-decisions.md; the skills' constants in C++). Names the skills'
	// .cpp files also use for their file-local constants (anonymous namespaces) carry an "Expected"
	// prefix: in a unity build both would be visible in RunTest and the name ambiguous.
	constexpr float HealAmount = 20.0f;
	constexpr float ExpectedMuteSeconds = 10.0f;
	constexpr float ExpectedMuteDamagePerSecond = 5.0f;
	constexpr float ExpectedEldritchShieldAmount = 45.0f;
	constexpr float ExpectedEldritchShieldHealthCost = 10.0f;
	constexpr float ExpectedEldritchShieldSeconds = 12.0f;
	constexpr float ChopDownMultiplier = 1.1f;
	constexpr float ExpectedChopDownSlow = 0.5f;
	constexpr float ExpectedChopDownSlowSeconds = 2.0f;
	constexpr float ExpectedShootingStarDistance = 1000.0f;
	constexpr float ExpectedShootingStarDamage = 50.0f;
	constexpr float ExpectedShootingStarBlindSeconds = 2.0f;
	constexpr float ExpectedDragonSeconds = 45.0f;
	constexpr float ExpectedDragonMaxHealthBonus = 0.5f;
	constexpr float ExpectedDragonResistance = 0.25f;
	constexpr float ExpectedFlightSeconds = 20.0f;
	constexpr float ExpectedWallDistance = 300.0f;
	constexpr float WallSeconds = 15.0f;
	constexpr float ExpectedLightHeight = 120.0f;
	constexpr float LightSeconds = 120.0f;
	constexpr float ExpectedStealthSeconds = 12.0f;
	constexpr float ExpectedStealthExitSeconds = 3.0f;
	constexpr float ExpectedStealthExitMoveSpeed = 0.1f;
	constexpr float ExpectedStealthExitAttackBonus = 0.25f;
	constexpr float ExpectedUnleashSeconds = 15.0f;
	constexpr float ExpectedMultiscaleResistance = 0.15f;
	constexpr float TrackingTrailSeconds = 30.0f;
	constexpr int32 SkeletonCount = 6;
	constexpr int32 ExpectedSkeletonWarriors = 5;
	constexpr float ExpectedSkeletonWarriorHealth = 40.0f;
	constexpr float ExpectedSkeletonMageHealth = 30.0f;
	constexpr float ExpectedSkeletonSeconds = 60.0f;
	constexpr float TigerHealth = 100.0f;
	constexpr float TigerAttackDamage = 20.0f;
	constexpr float TigerSeconds = 180.0f;
	// Ruled: 1.3 claw attacks a second.
	constexpr float TigerAttackInterval = 1.0f / 1.3f;
	constexpr float DemonHealth = 220.0f;
	constexpr float DemonAttackDamage = 40.0f;
	// Ruled: 3x the player's size.
	constexpr float DemonScale = 3.0f;
	// Ruled: the reach scales with the size. A player-size companion (the tiger) reaches 2 m centre to
	// centre; the 3x demon 6 m.
	constexpr float CompanionBaseReach = 200.0f;
	constexpr float DemonSeconds = 60.0f;
	constexpr float SilenceSeconds = 2.0f;

	const FName TigerPetGem(TEXT("pet_gem_tiger"));

	// Health taken off a target before it is healed.
	constexpr float HealthMissing = 50.0f;
	// Game-time speed-up for long cooldowns and durations (the engine caps it at 20).
	constexpr float FastForward = 10.0f;
	// Aimed skills look along the player's view, which a PlayerController reads from its camera
	// manager, updated once a frame: wait this long after Aim / Face before casting.
	constexpr float ViewCatchUp = 0.2f;

	UAbilitySystemComponent* AbilitySystemOf(const AActor* Actor)
	{
		return Actor != nullptr ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor) : nullptr;
	}

	// A remembered character, reporting a missing one (C.Get).
	ACrossguardCharacter* Character(FScenarioContext& C, FName Name)
	{
		return C.Get<ACrossguardCharacter>(Name);
	}

	// The same without an error (WaitUntil conditions).
	ACrossguardCharacter* FindCharacter(const FScenarioContext& C, FName Name)
	{
		return Cast<ACrossguardCharacter>(C.FindActor(Name));
	}

	// Raises Character's Level until its Skill Capacity covers Cost. bUseDebugProgression keeps the
	// player state from setting the level back; the attributes are not re-initialized, so stats stay
	// as they were. Without a Skill Catalog GrantSkills grants native classes and checks no Capacity.
	bool EnsureSkillCapacity(FScenarioContext& C, ACrossguardCharacter* Target, int32 Cost)
	{
		UCrossguardLoadoutComponent* Loadout = Target != nullptr ? Target->GetLoadoutComponent() : nullptr;
		const UCrossguardAbilitySystemComponent* ASC = Target != nullptr ? Target->GetCrossguardAbilitySystemComponent() : nullptr;
		if (Loadout == nullptr || ASC == nullptr || Loadout->SkillCatalog == nullptr)
		{
			return true;
		}
		if (Loadout->GetCapacity() >= Cost)
		{
			return true;
		}
		Target->bUseDebugProgression = true;
		const int32 MaxLevel = ASC->GetProgressionConfig().MaxLevel;
		while (Loadout->GetCapacity() < Cost && Target->Level < MaxLevel)
		{
			++Target->Level;
		}
		if (Loadout->GetCapacity() < Cost)
		{
			C.Fail(FString::Printf(TEXT("%s: Skill Capacity %d at level %d is below the %d these skills cost"), *Target->GetName(),
				Loadout->GetCapacity(), Target->Level, Cost));
			return false;
		}
		C.Test.AddInfo(FString::Printf(TEXT("%s raised to level %d (Skill Capacity %d) for a loadout costing %d"), *Target->GetName(), Target->Level,
			Loadout->GetCapacity(), Cost));
		return true;
	}

	bool GrantSkillsAtCost(FScenarioContext& C, ACrossguardCharacter* Target, const TArray<FName>& Skills, int32 Cost)
	{
		return EnsureSkillCapacity(C, Target, Cost) && C.GrantSkills(Target, Skills);
	}

	// Unequips every skill (an empty loadout). Without a Skill Catalog (native fallback): clears
	// SkillId's ability by hand, cancelling it first as the loadout does.
	bool UnequipSkills(FScenarioContext& C, ACrossguardCharacter* Target, FName SkillId)
	{
		UCrossguardLoadoutComponent* Loadout = Target != nullptr ? Target->GetLoadoutComponent() : nullptr;
		if (Loadout != nullptr && Loadout->SkillCatalog != nullptr)
		{
			return C.GrantSkills(Target, TArray<FName>());
		}
		UAbilitySystemComponent* ASC = AbilitySystemOf(Target);
		if (ASC == nullptr)
		{
			return false;
		}
		TArray<FGameplayAbilitySpecHandle> Handles;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const UCrossguardGameplayAbility* Ability = Cast<UCrossguardGameplayAbility>(Spec.Ability);
			if (Ability != nullptr && Ability->SkillId == SkillId)
			{
				Handles.Add(Spec.Handle);
			}
		}
		for (const FGameplayAbilitySpecHandle& Handle : Handles)
		{
			ASC->CancelAbilityHandle(Handle);
			ASC->ClearAbility(Handle);
		}
		return true;
	}

	// Any ability with this skill id is granted (active or passive).
	bool HasSkill(const AActor* Actor, FName SkillId)
	{
		UAbilitySystemComponent* ASC = AbilitySystemOf(Actor);
		if (ASC == nullptr)
		{
			return false;
		}
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const UCrossguardGameplayAbility* Ability = Cast<UCrossguardGameplayAbility>(Spec.Ability);
			if (Ability != nullptr && Ability->SkillId == SkillId)
			{
				return true;
			}
		}
		return false;
	}

	// Server: sets Health (clamped to 0..Max HP by the attribute set).
	void SetHealth(const AActor* Actor, float Value)
	{
		if (UAbilitySystemComponent* ASC = AbilitySystemOf(Actor))
		{
			ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(), Value);
		}
	}

	// One Magical Healing from Source, as UCrossguardHealingExecution computes it (before the Max HP
	// cap): (Base + Additional Magical Healing) x (1 + Magical Healing Bonus x Coefficient).
	float ExpectedHealing(const FScenarioContext& C, const AActor* Source, float Base, float Coefficient = 1.0f)
	{
		return static_cast<float>(Crossguard::CalculateHealing(Base, C.Stat(Source, UCrossguardAttributeSet::GetAdditionalMagicalHealingAttribute()),
			C.Stat(Source, UCrossguardAttributeSet::GetMagicalHealingBonusAttribute()), Coefficient));
	}

	// Living minions summoned by Summoner.
	TArray<ACrossguardMinion*> MinionsOf(const FScenarioContext& C, const AActor* Summoner)
	{
		TArray<ACrossguardMinion*> Result;
		for (ACrossguardMinion* Minion : C.FindAll<ACrossguardMinion>())
		{
			if (IsValid(Minion) && !Minion->IsActorBeingDestroyed() && Minion->GetSummoner() == Summoner)
			{
				Result.Add(Minion);
			}
		}
		return Result;
	}

	bool IsFlyingMode(const ACrossguardCharacter* Target)
	{
		const UCharacterMovementComponent* Movement = Target != nullptr ? Target->GetCharacterMovement() : nullptr;
		return Movement != nullptr && Movement->MovementMode == MOVE_Flying;
	}

	// Equips a fresh armor piece (e.g. "plate_cuirass") in its slot, without the inventory. Skips the
	// scenario if the character has no Armor Table.
	bool EquipArmorPiece(FScenarioContext& C, ACrossguardCharacter* Target, FName ArmorId)
	{
		UCrossguardEquipmentComponent* Equipment = Target != nullptr ? Target->GetEquipmentComponent() : nullptr;
		if (Equipment == nullptr)
		{
			C.Fail(TEXT("EquipArmorPiece: no character or equipment component"));
			return false;
		}
		if (Equipment->ArmorTable == nullptr)
		{
			C.Skip(FString::Printf(TEXT("%s's EquipmentComponent has no Armor Table (DT_Armor): cannot wear %s"), *Target->GetClass()->GetName(),
				*ArmorId.ToString()));
			return false;
		}
		ECrossguardArmorSlot Slot = ECrossguardArmorSlot::Head;
		if (!Equipment->GetArmorSlotFor(ArmorId, Slot))
		{
			C.Fail(FString::Printf(TEXT("EquipArmorPiece: %s is not in the Armor Table"), *ArmorId.ToString()));
			return false;
		}
		FCrossguardArmorItem Piece;
		Piece.ArmorId = ArmorId;
		const ECrossguardEquipError Error = Equipment->EquipArmor(Slot, Piece);
		if (Error != ECrossguardEquipError::None)
		{
			C.Fail(FString::Printf(TEXT("EquipArmorPiece %s: %s"), *ArmorId.ToString(),
				*StaticEnum<ECrossguardEquipError>()->GetNameStringByValue(static_cast<int64>(Error))));
			return false;
		}
		return true;
	}
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "heal at a damaged dummy, then at nothing -> The dummy heals 20; then you heal 20."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportHeal, "Crossguard.Scenario.Skills.Heal", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportHeal::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A healer with a wand and Heal, a dummy 5 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Healer = C.SpawnPlayer(TEXT("Healer"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(500.0f, Row, 0.0f), 180.0f);
		if (!C.Require(Healer != nullptr && Dummy != nullptr, TEXT("healer and dummy spawned")) || !C.Equip(Healer, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Healer, {TEXT("heal")}, CostHeal), TEXT("Heal granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Hurt the dummy; the healer aims at it"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Healer = Character(C, TEXT("Healer"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Healer == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("healer or dummy gone"));
			return;
		}
		SetHealth(Dummy, FMath::Max(1.0f, C.MaxHealth(Dummy) - HealthMissing));
		C.Aim(Healer, Dummy);
	});

	Scenario.Wait(ViewCatchUp);

	Scenario.Step(TEXT("Heal the damaged dummy"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Healer = Character(C, TEXT("Healer"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Healer == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("healer or dummy gone"));
			return;
		}
		const float Max = C.MaxHealth(Dummy);
		const float Before = C.Health(Dummy);
		const float Expected = FMath::Min(ExpectedHealing(C, Healer, HealAmount), Max - Before);
		const float HealerBefore = C.Health(Healer);
		if (!C.Require(C.ActivateSkill(Healer, TEXT("heal")), TEXT("Heal activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Heal on the dummy: %.2f -> %.2f (expected +%.2f)"), Before, C.Health(Dummy), Expected));
		C.Test.TestTrue(TEXT("Heal heals something (20 before bonuses)"), Expected > 0.0f);
		C.Test.TestEqual(TEXT("The dummy under the crosshair is healed"), C.Health(Dummy) - Before, Expected, 0.05f);
		C.Test.TestEqual(TEXT("The healer's own Health is unchanged"), C.Health(Healer), HealerBefore, 0.001f);
		C.Test.TestTrue(TEXT("Log shows the healing"), C.LogContains(TEXT("healed")));
		C.Test.TestTrue(TEXT("Heal is on cooldown"), C.IsOnCooldown(Healer, TEXT("heal")));
		C.SetTimeDilation(FastForward);
	});

	// 12 s cooldown (less with Spell CDR).
	Scenario.WaitUntil(TEXT("Heal is ready again"), 15.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Healer = FindCharacter(C, TEXT("Healer"));
		return Healer != nullptr && !C.IsOnCooldown(Healer, TEXT("heal"));
	});

	Scenario.Step(TEXT("The healer turns to face nothing"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		// Nobody along +Y within 15 m.
		if (ACrossguardCharacter* Healer = Character(C, TEXT("Healer")))
		{
			C.Face(Healer, 90.0f);
		}
	});

	Scenario.Wait(ViewCatchUp);

	Scenario.Step(TEXT("Hurt, the healer casts Heal at nothing: it heals itself"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Healer = Character(C, TEXT("Healer"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Healer == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("healer or dummy gone"));
			return;
		}
		const float Max = C.MaxHealth(Healer);
		SetHealth(Healer, FMath::Max(1.0f, Max - HealthMissing));
		const float Before = C.Health(Healer);
		const float DummyBefore = C.Health(Dummy);
		const float Expected = FMath::Min(ExpectedHealing(C, Healer, HealAmount), Max - Before);
		if (!C.Require(C.ActivateSkill(Healer, TEXT("heal")), TEXT("Heal activates the second time")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Heal at nothing: healer %.2f -> %.2f (expected +%.2f)"), Before, C.Health(Healer), Expected));
		C.Test.TestEqual(TEXT("With nobody under the crosshair the healer heals itself"), C.Health(Healer) - Before, Expected, 0.05f);
		C.Test.TestEqual(TEXT("The dummy is not healed this time"), C.Health(Dummy), DummyBefore, 0.001f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "mute at the second client, who then tries a skill and left click -> Skills fail, left
// click still attacks; 5 damage per second for 10 s. mute at nothing: no cooldown."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportMute, "Crossguard.Scenario.Skills.Mute", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportMute::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Caster (wand, Mute) and target (sword, Unleash) 1.5 m apart, facing each other"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = C.SpawnPlayer(TEXT("Caster"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Target = C.SpawnPlayer(TEXT("Target"), FVector(150.0f, Row, 0.0f), 180.0f);
		if (!C.Require(Caster != nullptr && Target != nullptr, TEXT("caster and target spawned")))
		{
			return;
		}
		if (!C.Equip(Caster, TEXT("wand_one_handed")) || !C.Equip(Target, TEXT("sword_one_handed")))
		{
			return;
		}
		if (!C.Require(GrantSkillsAtCost(C, Caster, {TEXT("mute")}, CostMute), TEXT("Mute granted")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Target, {TEXT("unleash")}, CostUnleash), TEXT("Unleash granted to the target"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The caster turns to face nothing"), [](FScenarioContext& C)
	{
		// Nobody along +Y within 20 m.
		if (ACrossguardCharacter* Caster = Character(C, TEXT("Caster")))
		{
			C.Face(Caster, 90.0f);
		}
	});

	Scenario.Wait(ViewCatchUp);

	Scenario.Step(TEXT("Mute at nothing: it fails and no cooldown starts"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = Character(C, TEXT("Caster"));
		ACrossguardCharacter* Target = Character(C, TEXT("Target"));
		if (Caster == nullptr || Target == nullptr)
		{
			C.Fail(TEXT("caster or target gone"));
			return;
		}
		C.ActivateSkill(Caster, TEXT("mute"));
		C.Test.TestFalse(TEXT("Mute at nothing starts no cooldown"), C.IsOnCooldown(Caster, TEXT("mute")));
		C.Test.TestFalse(TEXT("Nobody is Muted"), C.HasTag(Target, CrossguardTags::State_Muted));
		C.Aim(Caster, Target);
	});

	Scenario.Wait(ViewCatchUp);

	Scenario.Step(TEXT("Mute the target"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = Character(C, TEXT("Caster"));
		ACrossguardCharacter* Target = Character(C, TEXT("Target"));
		if (Caster == nullptr || Target == nullptr)
		{
			C.Fail(TEXT("caster or target gone"));
			return;
		}
		C.SetNumber(TEXT("TargetBefore"), C.Health(Target));
		// One tick of the Mute's damage over time: 5 Magic Damage, coefficient 1, from the caster.
		C.SetNumber(TEXT("Tick"), C.ExpectedDamage(Caster, Target, 0.0f, ExpectedMuteDamagePerSecond, 1.0f));
		if (!C.Require(C.ActivateSkill(Caster, TEXT("mute")), TEXT("Mute activates at the target")))
		{
			return;
		}
		C.SetNumber(TEXT("MutedAt"), C.Now());
		C.Test.TestTrue(TEXT("The target is Muted (State.Muted)"), C.HasTag(Target, CrossguardTags::State_Muted));
		C.Test.TestTrue(TEXT("Mute is on cooldown after landing"), C.IsOnCooldown(Caster, TEXT("mute")));
		C.Test.TestTrue(TEXT("Cooldown.Skill.Mute is on the caster"), C.HasTag(Caster, CrossguardTags::Cooldown_Skill_Mute));
	});

	Scenario.Step(TEXT("The Muted target: skills fail, the basic attack works"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = Character(C, TEXT("Caster"));
		ACrossguardCharacter* Target = Character(C, TEXT("Target"));
		if (Caster == nullptr || Target == nullptr)
		{
			C.Fail(TEXT("caster or target gone"));
			return;
		}
		C.Test.TestFalse(TEXT("A Muted character cannot activate a skill (Unleash)"), C.ActivateSkill(Target, TEXT("unleash")));
		C.Face(Target, 180.0f);
		const float Before = C.Health(Caster);
		const float Expected = FMath::Min(C.ExpectedDamage(Target, Caster, C.WeaponValue(Target)), Before);
		C.Test.TestTrue(TEXT("A Muted character's basic attack still activates"), C.BasicAttack(Target));
		C.Test.TestEqual(TEXT("The Muted character's sword hit lands in full"), Before - C.Health(Caster), Expected, 0.05f);
	});

	// Ticks at 1, 2 and 3 s after the Mute landed.
	Scenario.Wait(3.4f);

	Scenario.Step(TEXT("Three ticks of 5 Magic Damage per second so far"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Target = Character(C, TEXT("Target"));
		if (Target == nullptr)
		{
			C.Fail(TEXT("target gone"));
			return;
		}
		const float Taken = static_cast<float>(C.Number(TEXT("TargetBefore"))) - C.Health(Target);
		const float Tick = static_cast<float>(C.Number(TEXT("Tick")));
		C.Test.AddInfo(FString::Printf(TEXT("Mute after %.2f s: %.2f damage (%.2f per tick)"), C.Now() - C.Number(TEXT("MutedAt")), Taken, Tick));
		C.Test.TestTrue(TEXT("A tick does damage"), Tick > 0.0f);
		C.Test.TestEqual(TEXT("Mute damage after about 3.4 s is three ticks"), Taken, 3.0f * Tick, 0.1f);
		C.Test.TestTrue(TEXT("Still Muted"), C.HasTag(Target, CrossguardTags::State_Muted));
		C.SetTimeDilation(FastForward * 0.5f);
	});

	Scenario.WaitUntil(TEXT("The Mute wears off"), ExpectedMuteSeconds, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Target = FindCharacter(C, TEXT("Target"));
		return Target != nullptr && !C.HasTag(Target, CrossguardTags::State_Muted);
	});

	Scenario.Step(TEXT("10 s of Mute: about ten ticks, and skills work again"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		ACrossguardCharacter* Target = Character(C, TEXT("Target"));
		if (Target == nullptr)
		{
			C.Fail(TEXT("target gone"));
			return;
		}
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("MutedAt")));
		const float Taken = static_cast<float>(C.Number(TEXT("TargetBefore"))) - C.Health(Target);
		const float Tick = static_cast<float>(C.Number(TEXT("Tick")));
		const float Ticks = Tick > 0.0f ? Taken / Tick : 0.0f;
		C.Test.AddInfo(FString::Printf(TEXT("Mute lasted %.2f s and dealt %.2f (%.2f ticks)"), Elapsed, Taken, Ticks));
		C.Test.TestEqual(TEXT("Mute lasts 10 s"), Elapsed, ExpectedMuteSeconds, 0.5f);
		// The last tick and the expiry fall on the same moment: 9 or 10 ticks.
		C.Test.TestTrue(TEXT("Mute dealt 9 to 10 ticks of 5 per second"), Ticks > 8.95f && Ticks < 10.05f);
		C.Test.TestTrue(TEXT("Skills work again once the Mute is over (Unleash)"), C.ActivateSkill(Target, TEXT("unleash")));
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "eldritch_shield, then take a sword hit -> -10 HP on cast; the hit is absorbed."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportEldritchShield, "Crossguard.Scenario.Skills.EldritchShield", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportEldritchShield::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("Caster (wand, Eldritch Shield) and a swordsman 1.5 m apart"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = C.SpawnPlayer(TEXT("Caster"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Swordsman = C.SpawnPlayer(TEXT("Swordsman"), FVector(150.0f, Row, 0.0f), 180.0f);
		if (!C.Require(Caster != nullptr && Swordsman != nullptr, TEXT("caster and swordsman spawned")))
		{
			return;
		}
		if (!C.Equip(Caster, TEXT("wand_one_handed")) || !C.Equip(Swordsman, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Caster, {TEXT("eldritch_shield")}, CostEldritchShield), TEXT("Eldritch Shield granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Cast Eldritch Shield: -10 HP and a 45 Attack Damage barrier"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = Character(C, TEXT("Caster"));
		if (Caster == nullptr)
		{
			C.Fail(TEXT("caster gone"));
			return;
		}
		const float Before = C.Health(Caster);
		if (!C.Require(C.ActivateSkill(Caster, TEXT("eldritch_shield")), TEXT("Eldritch Shield activates")))
		{
			return;
		}
		C.SetNumber(TEXT("CastAt"), C.Now());
		C.Test.TestEqual(TEXT("Casting costs 10 HP (never below 1)"), C.Health(Caster), FMath::Max(1.0f, Before - ExpectedEldritchShieldHealthCost), 0.01f);
		C.Test.TestEqual(TEXT("AttackShield is 45"), C.Stat(Caster, UCrossguardAttributeSet::GetAttackShieldAttribute()), ExpectedEldritchShieldAmount, 0.01f);
		C.Test.TestTrue(TEXT("Eldritch Shield is on cooldown"), C.IsOnCooldown(Caster, TEXT("eldritch_shield")));
	});

	Scenario.Step(TEXT("The swordsman hits the caster: the barrier absorbs it"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = Character(C, TEXT("Caster"));
		ACrossguardCharacter* Swordsman = Character(C, TEXT("Swordsman"));
		if (Caster == nullptr || Swordsman == nullptr)
		{
			C.Fail(TEXT("caster or swordsman gone"));
			return;
		}
		C.Face(Swordsman, 180.0f);
		const float Before = C.Health(Caster);
		const float Shield = C.Stat(Caster, UCrossguardAttributeSet::GetAttackShieldAttribute());
		// UCrossguardDamageExecution: the barrier takes the Attack part (True Damage passes through).
		const float Hit = C.ExpectedDamage(Swordsman, Caster, C.WeaponValue(Swordsman));
		const float TrueDamage = FMath::Max(0.0f, C.Stat(Swordsman, UCrossguardAttributeSet::GetTrueDamageAttribute()));
		const float Absorbed = FMath::Min(FMath::Max(Hit - TrueDamage, 0.0f), Shield);
		const float ExpectedLoss = FMath::Min(Hit - Absorbed, Before);
		if (!C.Require(C.BasicAttack(Swordsman), TEXT("the swordsman's attack activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Sword hit %.2f on a %.2f barrier: Health %.2f -> %.2f, barrier -> %.2f"), Hit, Shield, Before, C.Health(Caster),
			C.Stat(Caster, UCrossguardAttributeSet::GetAttackShieldAttribute())));
		C.Test.TestTrue(TEXT("The sword hit has damage to absorb"), Hit > 0.0f);
		C.Test.TestEqual(TEXT("Health loss is the hit minus what the barrier absorbed"), Before - C.Health(Caster), ExpectedLoss, 0.05f);
		C.Test.TestEqual(TEXT("The barrier is used up by what it absorbed"), C.Stat(Caster, UCrossguardAttributeSet::GetAttackShieldAttribute()),
			Shield - Absorbed, 0.05f);
		if (Hit - TrueDamage <= Shield)
		{
			C.Test.TestEqual(TEXT("A sword hit within 45 is absorbed entirely"), C.Health(Caster), Before - FMath::Min(TrueDamage, Before), 0.05f);
		}
		C.SetNumber(TEXT("ShieldLeft"), C.Stat(Caster, UCrossguardAttributeSet::GetAttackShieldAttribute()));
		C.SetTimeDilation(FastForward * 0.5f);
	});

	Scenario.WaitUntil(TEXT("The barrier is gone"), ExpectedEldritchShieldSeconds + 2.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Caster = FindCharacter(C, TEXT("Caster"));
		return Caster != nullptr && C.Stat(Caster, UCrossguardAttributeSet::GetAttackShieldAttribute()) <= 0.0f;
	});

	Scenario.Step(TEXT("What was left of the barrier lasted 12 s"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		if (C.Number(TEXT("ShieldLeft")) > 0.01)
		{
			const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("CastAt")));
			C.Test.AddInfo(FString::Printf(TEXT("Barrier ended after %.2f s"), Elapsed));
			C.Test.TestEqual(TEXT("The barrier lasts 12 s"), Elapsed, ExpectedEldritchShieldSeconds, 0.5f);
		}
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "chop_down with an axe, then hit the dummy -> 110% damage, the dummy slowed for 2 s. A
// flying target falls."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportChopDown, "Crossguard.Scenario.Skills.ChopDown", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportChopDown::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("An axeman with Chop Down and a dummy; a flier (wand, Flight) 6 m to the side"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Axeman = C.SpawnPlayer(TEXT("Axeman"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150.0f, Row, 0.0f), 180.0f);
		ACrossguardCharacter* Flier = C.SpawnPlayer(TEXT("Flier"), FVector(150.0f, Row + 600.0f, 0.0f), 180.0f);
		if (!C.Require(Axeman != nullptr && Dummy != nullptr && Flier != nullptr, TEXT("axeman, dummy and flier spawned")))
		{
			return;
		}
		if (!C.Equip(Axeman, TEXT("axe_one_handed")) || !C.Equip(Flier, TEXT("wand_one_handed")))
		{
			return;
		}
		if (!C.Require(GrantSkillsAtCost(C, Axeman, {TEXT("chop_down")}, CostChopDown), TEXT("Chop Down granted")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Flier, {TEXT("flight")}, CostFlight), TEXT("Flight granted to the flier"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Chop Down, then hit the dummy: 110% and a 2 s slow"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Axeman = Character(C, TEXT("Axeman"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Axeman == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("axeman or dummy gone"));
			return;
		}
		C.Face(Axeman, 0.0f);
		if (!C.Require(C.ActivateSkill(Axeman, TEXT("chop_down")), TEXT("Chop Down activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The next axe attack is readied (State.ChopDownReady)"), C.HasTag(Axeman, CrossguardTags::State_ChopDownReady));
		const float Before = C.Health(Dummy);
		const float Expected = FMath::Min(C.ExpectedDamage(Axeman, Dummy, C.WeaponValue(Axeman) * ChopDownMultiplier), Before);
		const float SpeedBefore = C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute());
		C.SetNumber(TEXT("DummySpeed"), SpeedBefore);
		if (!C.Require(C.BasicAttack(Axeman), TEXT("the axe attack activates")))
		{
			return;
		}
		C.Test.AddInfo(FString::Printf(TEXT("Chop Down hit: %.2f -> %.2f (expected -%.2f)"), Before, C.Health(Dummy), Expected));
		C.Test.TestEqual(TEXT("The Chop Down hit deals 110% of the axe's damage"), Before - C.Health(Dummy), Expected, 0.05f);
		C.Test.TestFalse(TEXT("The readied attack is used up"), C.HasTag(Axeman, CrossguardTags::State_ChopDownReady));
		C.Test.TestTrue(TEXT("The dummy is slowed (State.Slowed)"), C.HasTag(Dummy, CrossguardTags::State_Slowed));
		C.Test.TestEqual(TEXT("The dummy's Movement Speed is 50% lower"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			FMath::Max(0.0f, SpeedBefore - ExpectedChopDownSlow), 0.001f);
	});

	Scenario.Wait(ExpectedChopDownSlowSeconds + 0.5f);

	Scenario.Step(TEXT("The slow has worn off after 2 s"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			C.Fail(TEXT("dummy gone"));
			return;
		}
		C.Test.TestFalse(TEXT("No State.Slowed after 2.5 s"), C.HasTag(Dummy, CrossguardTags::State_Slowed));
		C.Test.TestEqual(TEXT("Movement Speed is back"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			static_cast<float>(C.Number(TEXT("DummySpeed"))), 0.001f);
		C.SetTimeDilation(FastForward);
	});

	// 25 s cooldown (less with Active / Melee CDR).
	Scenario.WaitUntil(TEXT("Chop Down is ready again"), 30.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Axeman = FindCharacter(C, TEXT("Axeman"));
		return Axeman != nullptr && !C.IsOnCooldown(Axeman, TEXT("chop_down"));
	});

	Scenario.Step(TEXT("The flier takes off; the axeman steps up to it"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		ACrossguardCharacter* Axeman = Character(C, TEXT("Axeman"));
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Axeman == nullptr || Flier == nullptr)
		{
			C.Fail(TEXT("axeman or flier gone"));
			return;
		}
		C.Place(Axeman, FVector(0.0f, Row + 600.0f, 0.0f), 0.0f);
		if (!C.Require(C.ActivateSkill(Flier, TEXT("flight")), TEXT("the flier's Flight activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The flier is flying (State.Flying)"), C.HasTag(Flier, CrossguardTags::State_Flying));
		C.Test.TestTrue(TEXT("The flier's movement mode is Flying"), IsFlyingMode(Flier));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Chop Down hit on the flier: it is Grounded and falls"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Axeman = Character(C, TEXT("Axeman"));
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Axeman == nullptr || Flier == nullptr)
		{
			C.Fail(TEXT("axeman or flier gone"));
			return;
		}
		C.Test.TestTrue(TEXT("The flier is still flying before the hit"), C.HasTag(Flier, CrossguardTags::State_Flying));
		C.Face(Axeman, 0.0f);
		if (!C.Require(C.ActivateSkill(Axeman, TEXT("chop_down")), TEXT("Chop Down activates the second time")))
		{
			return;
		}
		const float Before = C.Health(Flier);
		const float Expected = FMath::Min(C.ExpectedDamage(Axeman, Flier, C.WeaponValue(Axeman) * ChopDownMultiplier), Before);
		if (!C.Require(C.BasicAttack(Axeman), TEXT("the axe attack on the flier activates")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The flier takes 110% of the axe's damage"), Before - C.Health(Flier), Expected, 0.05f);
		C.Test.TestFalse(TEXT("The flier's Flight ended (no State.Flying)"), C.HasTag(Flier, CrossguardTags::State_Flying));
		C.Test.TestFalse(TEXT("The flier's movement mode is no longer Flying"), IsFlyingMode(Flier));
		C.Test.TestTrue(TEXT("The flier is slowed too"), C.HasTag(Flier, CrossguardTags::State_Slowed));
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "shooting_star with a sword, facing the second client -> You dash 10 m through them;
// they take 50 and get State.Blinded for 2 s."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportShootingStar, "Crossguard.Scenario.Skills.ShootingStar", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportShootingStar::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A swordsman with Shooting Star; a second player 5 m ahead looking at them"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dasher = C.SpawnPlayer(TEXT("Dasher"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Watcher = C.SpawnPlayer(TEXT("Watcher"), FVector(500.0f, Row, 0.0f), 180.0f);
		if (!C.Require(Dasher != nullptr && Watcher != nullptr, TEXT("dasher and watcher spawned")) || !C.Equip(Dasher, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Dasher, {TEXT("shooting_star")}, CostShootingStar), TEXT("Shooting Star granted"));
	});

	// Land, and let the watcher's view (camera manager) catch up with its facing.
	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Shooting Star: the flash blinds the watcher at once"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dasher = Character(C, TEXT("Dasher"));
		ACrossguardCharacter* Watcher = Character(C, TEXT("Watcher"));
		if (Dasher == nullptr || Watcher == nullptr)
		{
			C.Fail(TEXT("dasher or watcher gone"));
			return;
		}
		C.Face(Dasher, 0.0f);
		C.SetNumber(TEXT("StartX"), Dasher->GetActorLocation().X);
		C.SetNumber(TEXT("WatcherBefore"), C.Health(Watcher));
		C.SetNumber(TEXT("Expected"), C.ExpectedDamage(Dasher, Watcher, ExpectedShootingStarDamage));
		if (!C.Require(C.ActivateSkill(Dasher, TEXT("shooting_star")), TEXT("Shooting Star activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Shooting Star is on cooldown"), C.IsOnCooldown(Dasher, TEXT("shooting_star")));
		C.Test.TestTrue(TEXT("The watcher looking at the dasher is Blinded (State.Blinded)"), C.HasTag(Watcher, CrossguardTags::State_Blinded));
		C.Test.TestFalse(TEXT("The dasher does not blind itself"), C.HasTag(Dasher, CrossguardTags::State_Blinded));
	});

	// The dash takes 0.3 s.
	Scenario.Wait(0.6f);

	Scenario.Step(TEXT("The dasher went 10 m, through the watcher, who took 50 Attack Damage"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dasher = Character(C, TEXT("Dasher"));
		ACrossguardCharacter* Watcher = Character(C, TEXT("Watcher"));
		if (Dasher == nullptr || Watcher == nullptr)
		{
			C.Fail(TEXT("dasher or watcher gone"));
			return;
		}
		const float Dashed = static_cast<float>(Dasher->GetActorLocation().X - C.Number(TEXT("StartX")));
		const float Before = static_cast<float>(C.Number(TEXT("WatcherBefore")));
		const float Expected = FMath::Min(static_cast<float>(C.Number(TEXT("Expected"))), Before);
		C.Test.AddInfo(FString::Printf(TEXT("Dashed %.1f cm; watcher %.2f -> %.2f (expected -%.2f)"), Dashed, Before, C.Health(Watcher), Expected));
		C.Test.TestTrue(TEXT("The dash covers about 10 m"), Dashed > ExpectedShootingStarDistance * 0.85f && Dashed < ExpectedShootingStarDistance * 1.1f);
		C.Test.TestTrue(TEXT("The dasher passed through the watcher"), Dasher->GetActorLocation().X > Watcher->GetActorLocation().X);
		C.Test.TestEqual(TEXT("The watcher in the path takes 50 Attack Damage"), Before - C.Health(Watcher), Expected, 0.05f);
		C.Test.TestTrue(TEXT("The watcher is still Blinded 0.6 s later"), C.HasTag(Watcher, CrossguardTags::State_Blinded));
	});

	Scenario.Wait(ExpectedShootingStarBlindSeconds);

	Scenario.Step(TEXT("The Blind is over after 2 s"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Watcher = Character(C, TEXT("Watcher"));
		if (Watcher == nullptr)
		{
			C.Fail(TEXT("watcher gone"));
			return;
		}
		C.Test.TestFalse(TEXT("No State.Blinded after 2.6 s"), C.HasTag(Watcher, CrossguardTags::State_Blinded));
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "dragonification -> Max HP +50%, 1 / 2 and other skills refused; Flare Breath and Flight
// appear on Q; all gone after 45 s."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportDragonification, "Crossguard.Scenario.Skills.Dragonification", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportDragonification::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A swordsman with Dragonification and Stealth"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dragon = C.SpawnPlayer(TEXT("Dragon"), FVector(0.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Dragon != nullptr, TEXT("player spawned")) || !C.Equip(Dragon, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Dragon, {TEXT("dragonification"), TEXT("stealth")}, CostDragonification + CostStealth),
			TEXT("Dragonification and Stealth granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Turn into a dragon"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dragon = Character(C, TEXT("Dragon"));
		if (Dragon == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		const float Max = C.MaxHealth(Dragon);
		const float Health = C.Health(Dragon);
		const float Mhpb = C.Stat(Dragon, UCrossguardAttributeSet::GetMaxHealthBonusAttribute());
		const float Ar = C.Stat(Dragon, UCrossguardAttributeSet::GetAttackResistanceAttribute());
		const float Mr = C.Stat(Dragon, UCrossguardAttributeSet::GetMagicResistanceAttribute());
		C.SetNumber(TEXT("Max"), Max);
		C.SetNumber(TEXT("Ar"), Ar);
		C.SetNumber(TEXT("Mr"), Mr);
		C.Test.TestTrue(TEXT("Weapons are drawn before"), Dragon->GetEquipmentComponent()->AreWeaponsReady());
		C.Test.TestFalse(TEXT("No Flight before the form"), HasSkill(Dragon, TEXT("flight")));
		if (!C.Require(C.ActivateSkill(Dragon, TEXT("dragonification")), TEXT("Dragonification activates")))
		{
			return;
		}
		C.SetNumber(TEXT("FormAt"), C.Now());
		C.Test.TestTrue(TEXT("State.Dragonified"), C.HasTag(Dragon, CrossguardTags::State_Dragonified));
		// Max HP = derived x (1 + MHPB): +0.5 MHPB.
		const float ExpectedMax = Max * (1.0f + Mhpb + ExpectedDragonMaxHealthBonus) / FMath::Max(1.0f + Mhpb, KINDA_SMALL_NUMBER);
		C.Test.AddInfo(FString::Printf(TEXT("Dragon: Max HP %.2f -> %.2f (expected %.2f), Health %.2f -> %.2f"), Max, C.MaxHealth(Dragon), ExpectedMax,
			Health, C.Health(Dragon)));
		C.Test.TestEqual(TEXT("Max HP +50%"), C.MaxHealth(Dragon), ExpectedMax, 0.5f);
		C.Test.TestEqual(TEXT("Health keeps its percentage"), C.Health(Dragon) / C.MaxHealth(Dragon), Health / Max, 0.01f);
		C.Test.TestEqual(TEXT("Attack Resistance +0.25"), C.Stat(Dragon, UCrossguardAttributeSet::GetAttackResistanceAttribute()), Ar + ExpectedDragonResistance, 0.001f);
		C.Test.TestEqual(TEXT("Magic Resistance +0.25"), C.Stat(Dragon, UCrossguardAttributeSet::GetMagicResistanceAttribute()), Mr + ExpectedDragonResistance, 0.001f);
		C.Test.TestTrue(TEXT("Flare Breath is granted"), HasSkill(Dragon, TEXT("flare_breath")));
		C.Test.TestTrue(TEXT("Flight is granted"), HasSkill(Dragon, TEXT("flight")));
		C.Test.TestFalse(TEXT("Weapons are put away"), Dragon->GetEquipmentComponent()->AreWeaponsReady());
		C.Test.TestFalse(TEXT("No basic attack in dragon form"), C.BasicAttack(Dragon));
		C.Test.TestFalse(TEXT("Other skills are refused (Stealth)"), C.ActivateSkill(Dragon, TEXT("stealth")));
		// Keys 1 / 2.
		Dragon->GetEquipmentComponent()->RequestSwitchWeaponSet(0);
		Dragon->GetEquipmentComponent()->RequestSwitchWeaponSet(1);
		// Flight is a Spell, cast without a wand in dragon form.
		C.Test.TestTrue(TEXT("Flight can be cast in dragon form"), C.ActivateSkill(Dragon, TEXT("flight")));
		C.Test.TestTrue(TEXT("Flying (State.Flying)"), C.HasTag(Dragon, CrossguardTags::State_Flying));
		C.Test.TestTrue(TEXT("Flying movement mode"), IsFlyingMode(Dragon));
	});

	// Longer than a weapon draw (1 s).
	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("Weapon sets 1 / 2 do not draw anything"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dragon = Character(C, TEXT("Dragon"));
		if (Dragon == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		C.Test.TestFalse(TEXT("Weapons stay away after pressing 1 / 2"), Dragon->GetEquipmentComponent()->AreWeaponsReady());
		C.Test.TestFalse(TEXT("Still no basic attack"), C.BasicAttack(Dragon));
		C.Test.TestTrue(TEXT("Still a dragon"), C.HasTag(Dragon, CrossguardTags::State_Dragonified));
		C.SetTimeDilation(FastForward);
	});

	Scenario.WaitUntil(TEXT("The form ends"), ExpectedDragonSeconds + 5.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dragon = FindCharacter(C, TEXT("Dragon"));
		return Dragon != nullptr && !C.HasTag(Dragon, CrossguardTags::State_Dragonified);
	});

	Scenario.Step(TEXT("All gone after 45 s"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		ACrossguardCharacter* Dragon = Character(C, TEXT("Dragon"));
		if (Dragon == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("FormAt")));
		C.Test.AddInfo(FString::Printf(TEXT("Dragon form lasted %.2f s"), Elapsed));
		C.Test.TestEqual(TEXT("The form lasts 45 s"), Elapsed, ExpectedDragonSeconds, 1.0f);
		C.Test.TestEqual(TEXT("Max HP is back"), C.MaxHealth(Dragon), static_cast<float>(C.Number(TEXT("Max"))), 0.5f);
		C.Test.TestEqual(TEXT("Attack Resistance is back"), C.Stat(Dragon, UCrossguardAttributeSet::GetAttackResistanceAttribute()),
			static_cast<float>(C.Number(TEXT("Ar"))), 0.001f);
		C.Test.TestEqual(TEXT("Magic Resistance is back"), C.Stat(Dragon, UCrossguardAttributeSet::GetMagicResistanceAttribute()),
			static_cast<float>(C.Number(TEXT("Mr"))), 0.001f);
		C.Test.TestFalse(TEXT("Flare Breath is gone"), HasSkill(Dragon, TEXT("flare_breath")));
		C.Test.TestFalse(TEXT("Flight is gone"), HasSkill(Dragon, TEXT("flight")));
		C.Test.TestFalse(TEXT("Not flying"), C.HasTag(Dragon, CrossguardTags::State_Flying) || IsFlyingMode(Dragon));
		C.Test.TestTrue(TEXT("Other skills work again (Stealth)"), C.ActivateSkill(Dragon, TEXT("stealth")));
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "flight: Space / Left Ctrl -> You rise and descend; you fall after 20 s."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportFlight, "Crossguard.Scenario.Skills.Flight", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportFlight::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a wand and Flight"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = C.SpawnPlayer(TEXT("Flier"), FVector(0.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Flier != nullptr, TEXT("flier spawned")) || !C.Equip(Flier, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Flier, {TEXT("flight")}, CostFlight), TEXT("Flight granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Take off"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			C.Fail(TEXT("flier gone"));
			return;
		}
		C.SetNumber(TEXT("GroundZ"), Flier->GetActorLocation().Z);
		if (!C.Require(C.ActivateSkill(Flier, TEXT("flight")), TEXT("Flight activates")))
		{
			return;
		}
		C.SetNumber(TEXT("FlightAt"), C.Now());
		C.Test.TestTrue(TEXT("State.Flying"), C.HasTag(Flier, CrossguardTags::State_Flying));
		C.Test.TestTrue(TEXT("Flying movement mode"), IsFlyingMode(Flier));
		C.Test.TestTrue(TEXT("Flight is on cooldown"), C.IsOnCooldown(Flier, TEXT("flight")));
	});

	// What holding Space does (OnFlyUp): upward movement input, for 1 s.
	Scenario.WaitUntil(TEXT("Rise for 1 s"), 3.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = FindCharacter(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			return false;
		}
		Flier->AddMovementInput(FVector::UpVector, 1.0f);
		return C.Now() - C.Number(TEXT("FlightAt")) >= 1.0;
	});

	Scenario.Step(TEXT("Up in the air; then wait out the 20 s"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			C.Fail(TEXT("flier gone"));
			return;
		}
		const float Risen = static_cast<float>(Flier->GetActorLocation().Z - C.Number(TEXT("GroundZ")));
		C.Test.AddInfo(FString::Printf(TEXT("Rose %.1f cm in 1 s"), Risen));
		C.Test.TestTrue(TEXT("Upward input makes the flier rise (more than 50 cm in 1 s)"), Risen > 50.0f);
		C.Test.TestTrue(TEXT("Still flying"), IsFlyingMode(Flier));
		C.SetNumber(TEXT("HighZ"), Flier->GetActorLocation().Z);
		C.SetTimeDilation(FastForward);
	});

	Scenario.Wait(2.0f);

	Scenario.Step(TEXT("Hovering without input"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			C.Fail(TEXT("flier gone"));
			return;
		}
		C.Test.TestTrue(TEXT("Without input the flier does not fall while flying"),
			Flier->GetActorLocation().Z > C.Number(TEXT("HighZ")) - 20.0);
	});

	Scenario.WaitUntil(TEXT("Flight ends"), ExpectedFlightSeconds + 5.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Flier = FindCharacter(C, TEXT("Flier"));
		return Flier != nullptr && !C.HasTag(Flier, CrossguardTags::State_Flying);
	});

	Scenario.Step(TEXT("Flight lasted 20 s; the flier falls"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			C.Fail(TEXT("flier gone"));
			return;
		}
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("FlightAt")));
		C.Test.AddInfo(FString::Printf(TEXT("Flight lasted %.2f s"), Elapsed));
		C.Test.TestEqual(TEXT("Flight lasts 20 s"), Elapsed, ExpectedFlightSeconds, 1.0f);
		C.Test.TestFalse(TEXT("No longer in Flying movement mode"), IsFlyingMode(Flier));
	});

	Scenario.WaitUntil(TEXT("Back on the ground"), 10.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Flier = FindCharacter(C, TEXT("Flier"));
		return Flier != nullptr && Flier->GetCharacterMovement()->IsMovingOnGround();
	});

	Scenario.Step(TEXT("Landed where it started"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			C.Fail(TEXT("flier gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Fell back to the floor"), static_cast<float>(Flier->GetActorLocation().Z), static_cast<float>(C.Number(TEXT("GroundZ"))), 5.0f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "wall -> A box wall 3 m ahead for 15 s."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportWall, "Crossguard.Scenario.Skills.Wall", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportWall::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a wand and Wall"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = C.SpawnPlayer(TEXT("Caster"), FVector(0.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Caster != nullptr, TEXT("caster spawned")) || !C.Equip(Caster, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Caster, {TEXT("wall")}, CostWall), TEXT("Wall granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Cast Wall facing +X"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = Character(C, TEXT("Caster"));
		if (Caster == nullptr)
		{
			C.Fail(TEXT("caster gone"));
			return;
		}
		C.Face(Caster, 0.0f);
		const FVector Expected = Caster->GetActorLocation() + FVector(ExpectedWallDistance, 0.0f, 0.0f);
		if (!C.Require(C.ActivateSkill(Caster, TEXT("wall")), TEXT("Wall activates")))
		{
			return;
		}
		C.SetNumber(TEXT("CastAt"), C.Now());
		const TArray<ACrossguardWall*> Walls = C.FindAll<ACrossguardWall>();
		if (!C.Require(Walls.Num() == 1, FString::Printf(TEXT("one wall spawned (found %d)"), Walls.Num())))
		{
			return;
		}
		ACrossguardWall* Wall = Walls[0];
		C.Remember(TEXT("Wall"), Wall);
		C.Test.AddInfo(FString::Printf(TEXT("Wall at %s, yaw %.1f, life span %.2f s"), *Wall->GetActorLocation().ToString(),
			Wall->GetActorRotation().Yaw, Wall->GetLifeSpan()));
		C.Test.TestEqual(TEXT("The wall stands 3 m in front"), static_cast<float>(FVector::Dist2D(Wall->GetActorLocation(), Expected)), 0.0f, 5.0f);
		C.Test.TestEqual(TEXT("The wall stands on the floor"), static_cast<float>(Wall->GetActorLocation().Z), 0.0f, 2.0f);
		C.Test.TestEqual(TEXT("The wall runs across the view"), FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Wall->GetActorRotation().Yaw), 90.0f)), 0.0f, 1.0f);
		C.Test.TestEqual(TEXT("The wall lives 15 s"), Wall->GetLifeSpan(), WallSeconds, 0.2f);
		C.Test.TestTrue(TEXT("Wall is on cooldown"), C.IsOnCooldown(Caster, TEXT("wall")));
		C.SetTimeDilation(FastForward * 0.5f);
	});

	Scenario.WaitUntil(TEXT("The wall is gone"), WallSeconds + 3.0f, [](FScenarioContext& C)
	{
		return C.FindAll<ACrossguardWall>().Num() == 0;
	});

	Scenario.Step(TEXT("It lasted 15 s"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("CastAt")));
		C.Test.AddInfo(FString::Printf(TEXT("Wall lasted %.2f s"), Elapsed));
		C.Test.TestEqual(TEXT("The wall is gone after 15 s"), Elapsed, WallSeconds, 0.5f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "light -> a light over your head" (design-decisions: 120 s; recasting replaces it).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportLight, "Crossguard.Scenario.Skills.Light", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportLight::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a wand and Light"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = C.SpawnPlayer(TEXT("Caster"), FVector(0.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Caster != nullptr, TEXT("caster spawned")) || !C.Equip(Caster, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Caster, {TEXT("light")}, CostLight), TEXT("Light granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Cast Light: an orb over the caster's head"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = Character(C, TEXT("Caster"));
		if (Caster == nullptr)
		{
			C.Fail(TEXT("caster gone"));
			return;
		}
		if (!C.Require(C.ActivateSkill(Caster, TEXT("light")), TEXT("Light activates")))
		{
			return;
		}
		const TArray<ACrossguardLightOrb*> Orbs = C.FindAll<ACrossguardLightOrb>();
		if (!C.Require(Orbs.Num() == 1, FString::Printf(TEXT("one light orb spawned (found %d)"), Orbs.Num())))
		{
			return;
		}
		ACrossguardLightOrb* Orb = Orbs[0];
		C.Remember(TEXT("Orb1"), Orb);
		C.Test.TestTrue(TEXT("The orb is attached to the caster"), Orb->GetAttachParentActor() == Caster);
		C.Test.TestEqual(TEXT("The orb is 1.2 m over the caster"), static_cast<float>(Orb->GetActorLocation().Z - Caster->GetActorLocation().Z), ExpectedLightHeight, 1.0f);
		C.Test.TestEqual(TEXT("The orb lives 120 s"), Orb->GetLifeSpan(), LightSeconds, 0.2f);
		C.Test.TestTrue(TEXT("Light is on cooldown"), C.IsOnCooldown(Caster, TEXT("light")));
		C.SetTimeDilation(FastForward);
	});

	// 30 s cooldown (less with Spell CDR).
	Scenario.WaitUntil(TEXT("Light is ready again"), 35.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Caster = FindCharacter(C, TEXT("Caster"));
		return Caster != nullptr && !C.IsOnCooldown(Caster, TEXT("light"));
	});

	Scenario.Step(TEXT("Cast Light again: the new orb replaces the old one"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Caster = Character(C, TEXT("Caster"));
		if (Caster == nullptr)
		{
			C.Fail(TEXT("caster gone"));
			return;
		}
		C.Test.TestNotNull(TEXT("The first orb is still there before the recast"), C.FindActor(TEXT("Orb1")));
		if (!C.Require(C.ActivateSkill(Caster, TEXT("light")), TEXT("Light activates again")))
		{
			return;
		}
		C.SetNumber(TEXT("RecastAt"), C.Now());
		C.Test.TestNull(TEXT("The first orb is gone"), C.FindActor(TEXT("Orb1")));
		const TArray<ACrossguardLightOrb*> Orbs = C.FindAll<ACrossguardLightOrb>();
		int32 Live = 0;
		for (const ACrossguardLightOrb* Orb : Orbs)
		{
			Live += IsValid(Orb) && !Orb->IsActorBeingDestroyed() ? 1 : 0;
		}
		C.Test.TestEqual(TEXT("Exactly one orb after the recast"), Live, 1);
	});

	Scenario.WaitUntil(TEXT("The orb burns out"), LightSeconds + 5.0f, [](FScenarioContext& C)
	{
		return C.FindAll<ACrossguardLightOrb>().Num() == 0;
	});

	Scenario.Step(TEXT("It lasted 120 s"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("RecastAt")));
		C.Test.AddInfo(FString::Printf(TEXT("Light lasted %.2f s"), Elapsed));
		C.Test.TestEqual(TEXT("The orb is gone after 120 s"), Elapsed, LightSeconds, 1.0f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "stealth with a second client watching -> You vanish on their screen for 12 s or until
// you attack; then +10% speed."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportStealth, "Crossguard.Scenario.Skills.Stealth", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportStealth::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	// The hiding is decided on each machine for its local viewer (the PIE player here), and a player
	// character is never hidden from itself; every scenario player counts as locally controlled in
	// standalone PIE. So the hiding is checked on an AI-controlled stealther (a dummy), the
	// attack and the exit bonus on a player.
	Scenario.Step(TEXT("A swordsman with Stealth next to a dummy; a second dummy with Stealth"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Sneak = C.SpawnPlayer(TEXT("Sneak"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150.0f, Row, 0.0f), 180.0f);
		ACrossguardCharacter* Hider = C.SpawnDummy(TEXT("Hider"), FVector(0.0f, Row + 600.0f, 0.0f), 180.0f);
		if (!C.Require(Sneak != nullptr && Dummy != nullptr && Hider != nullptr, TEXT("sneak, dummy and hider spawned"))
			|| !C.Equip(Sneak, TEXT("sword_one_handed")))
		{
			return;
		}
		if (!C.Require(GrantSkillsAtCost(C, Sneak, {TEXT("stealth")}, CostStealth), TEXT("Stealth granted")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Hider, {TEXT("stealth")}, CostStealth), TEXT("Stealth granted to the hider dummy"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Both go into stealth"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Sneak = Character(C, TEXT("Sneak"));
		ACrossguardCharacter* Hider = Character(C, TEXT("Hider"));
		if (Sneak == nullptr || Hider == nullptr)
		{
			C.Fail(TEXT("sneak or hider gone"));
			return;
		}
		C.Test.TestTrue(TEXT("The hider is visible before"), Hider->GetCapsuleComponent()->GetVisibleFlag());
		if (!C.Require(C.ActivateSkill(Sneak, TEXT("stealth")), TEXT("Stealth activates")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("State.Stealthed"), C.HasTag(Sneak, CrossguardTags::State_Stealthed));
		C.Test.TestTrue(TEXT("Stealth is on cooldown"), C.IsOnCooldown(Sneak, TEXT("stealth")));
		if (!C.Require(C.ActivateSkill(Hider, TEXT("stealth")), TEXT("the hider's Stealth activates")))
		{
			return;
		}
		C.SetNumber(TEXT("HiddenAt"), C.Now());
		C.Test.TestTrue(TEXT("The hider has State.Stealthed"), C.HasTag(Hider, CrossguardTags::State_Stealthed));
		if (C.LocalPlayer() != nullptr)
		{
			// The PIE player is not on the dummy's side: UpdateStealthVisibility hides its capsule.
			C.Test.TestFalse(TEXT("The stealthed dummy is hidden from the PIE player (capsule not visible)"), Hider->GetCapsuleComponent()->GetVisibleFlag());
		}
		else
		{
			C.Test.AddWarning(TEXT("No PIE player character: the hiding from a viewer is not checked"));
		}
	});

	Scenario.Wait(1.0f);

	Scenario.Step(TEXT("Attacking breaks stealth; the exit bonus applies"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Sneak = Character(C, TEXT("Sneak"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Sneak == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("sneak or dummy gone"));
			return;
		}
		C.Test.TestTrue(TEXT("Still stealthed after 1 s"), C.HasTag(Sneak, CrossguardTags::State_Stealthed));
		const float Speed = C.Stat(Sneak, UCrossguardAttributeSet::GetMoveSpeedAttribute());
		const float Bonus = C.Stat(Sneak, UCrossguardAttributeSet::GetAttackDamageBonusAttribute());
		C.SetNumber(TEXT("Speed"), Speed);
		C.SetNumber(TEXT("Bonus"), Bonus);
		C.Aim(Sneak, Dummy);
		if (!C.Require(C.BasicAttack(Sneak), TEXT("the attack activates")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("The attack ends stealth"), C.HasTag(Sneak, CrossguardTags::State_Stealthed));
		C.Test.TestEqual(TEXT("Exit bonus: +10% Movement Speed"), C.Stat(Sneak, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			Speed + ExpectedStealthExitMoveSpeed, 0.001f);
		C.Test.TestEqual(TEXT("Exit bonus: +25% Attack Damage Bonus"), C.Stat(Sneak, UCrossguardAttributeSet::GetAttackDamageBonusAttribute()),
			Bonus + ExpectedStealthExitAttackBonus, 0.001f);
	});

	Scenario.Wait(ExpectedStealthExitSeconds + 0.5f);

	Scenario.Step(TEXT("The exit bonus lasts 3 s"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Sneak = Character(C, TEXT("Sneak"));
		if (Sneak == nullptr)
		{
			C.Fail(TEXT("sneak gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Movement Speed is back"), C.Stat(Sneak, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			static_cast<float>(C.Number(TEXT("Speed"))), 0.001f);
		C.Test.TestEqual(TEXT("Attack Damage Bonus is back"), C.Stat(Sneak, UCrossguardAttributeSet::GetAttackDamageBonusAttribute()),
			static_cast<float>(C.Number(TEXT("Bonus"))), 0.001f);
	});

	Scenario.WaitUntil(TEXT("The hider's stealth runs out"), ExpectedStealthSeconds, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Hider = FindCharacter(C, TEXT("Hider"));
		return Hider != nullptr && !C.HasTag(Hider, CrossguardTags::State_Stealthed);
	});

	Scenario.Step(TEXT("After 12 s the hider is visible again"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Hider = Character(C, TEXT("Hider"));
		if (Hider == nullptr)
		{
			C.Fail(TEXT("hider gone"));
			return;
		}
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("HiddenAt")));
		C.Test.AddInfo(FString::Printf(TEXT("The hider's stealth lasted %.2f s"), Elapsed));
		C.Test.TestEqual(TEXT("Stealth lasts 12 s"), Elapsed, ExpectedStealthSeconds, 0.5f);
		C.Test.TestTrue(TEXT("The hider's capsule is visible again"), Hider->GetCapsuleComponent()->GetVisibleFlag());
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "immutable equipped, then hit by chop_down -> No slow."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportImmutable, "Crossguard.Scenario.Skills.Immutable", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportImmutable::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with Immutable equipped; an axeman with Chop Down facing them"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Rock = C.SpawnPlayer(TEXT("Rock"), FVector(150.0f, Row, 0.0f), 180.0f);
		ACrossguardCharacter* Axeman = C.SpawnPlayer(TEXT("Axeman"), FVector(0.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Rock != nullptr && Axeman != nullptr, TEXT("both spawned")) || !C.Equip(Axeman, TEXT("axe_one_handed")))
		{
			return;
		}
		if (!C.Require(GrantSkillsAtCost(C, Rock, {TEXT("immutable")}, CostImmutable), TEXT("Immutable granted")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Axeman, {TEXT("chop_down")}, CostChopDown), TEXT("Chop Down granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The Chop Down hit lands, but does not slow"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Rock = Character(C, TEXT("Rock"));
		ACrossguardCharacter* Axeman = Character(C, TEXT("Axeman"));
		if (Rock == nullptr || Axeman == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		C.Test.TestTrue(TEXT("Equipped Immutable: Passive.Immutable"), C.HasTag(Rock, CrossguardTags::Passive_Immutable));
		C.Face(Axeman, 0.0f);
		if (!C.Require(C.ActivateSkill(Axeman, TEXT("chop_down")), TEXT("Chop Down activates")))
		{
			return;
		}
		const float Before = C.Health(Rock);
		const float Speed = C.Stat(Rock, UCrossguardAttributeSet::GetMoveSpeedAttribute());
		const float Expected = FMath::Min(C.ExpectedDamage(Axeman, Rock, C.WeaponValue(Axeman) * ChopDownMultiplier), Before);
		if (!C.Require(C.BasicAttack(Axeman), TEXT("the axe attack activates")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The hit itself still lands (110%)"), Before - C.Health(Rock), Expected, 0.05f);
		C.Test.TestFalse(TEXT("No State.Slowed on the Immutable player"), C.HasTag(Rock, CrossguardTags::State_Slowed));
		C.Test.TestEqual(TEXT("Movement Speed unchanged"), C.Stat(Rock, UCrossguardAttributeSet::GetMoveSpeedAttribute()), Speed, 0.001f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "unleash -> STR / AGI / INT double for 15 s, Max HP follows."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportUnleash, "Crossguard.Scenario.Skills.Unleash", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportUnleash::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with Unleash"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("player spawned")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Player, {TEXT("unleash")}, CostUnleash), TEXT("Unleash granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Unleash: attributes double, Max HP follows"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = Character(C, TEXT("Player"));
		if (Player == nullptr || Player->GetCrossguardAbilitySystemComponent() == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		const float Str = C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute());
		const float Agi = C.Stat(Player, UCrossguardAttributeSet::GetAgilityAttribute());
		const float Int = C.Stat(Player, UCrossguardAttributeSet::GetIntelligenceAttribute());
		const float Max = C.MaxHealth(Player);
		C.SetNumber(TEXT("Str"), Str);
		C.SetNumber(TEXT("Agi"), Agi);
		C.SetNumber(TEXT("Int"), Int);
		C.SetNumber(TEXT("Max"), Max);

		// Max HP as UCrossguardAbilitySystemComponent::RecomputeDerivedStats derives it, plus whatever
		// gear adds on top (none here).
		const Crossguard::FScalingConfig& Scaling = Player->GetCrossguardAbilitySystemComponent()->GetScalingConfig();
		const double Mhpb = C.Stat(Player, UCrossguardAttributeSet::GetMaxHealthBonusAttribute());
		const auto DerivedMax = [&Scaling, Mhpb](float S, float A, float I)
		{
			const Crossguard::FAttributes Attributes{FMath::RoundToInt(S), FMath::RoundToInt(A), FMath::RoundToInt(I)};
			return static_cast<float>(Crossguard::ApplyMaxHpBonus(Crossguard::DeriveStats(Attributes, Scaling).MaxHp, Mhpb));
		};
		const float GearMax = Max - DerivedMax(Str, Agi, Int);
		const float ExpectedMax = DerivedMax(Str * 2.0f, Agi * 2.0f, Int * 2.0f) + GearMax;

		if (!C.Require(C.ActivateSkill(Player, TEXT("unleash")), TEXT("Unleash activates")))
		{
			return;
		}
		C.SetNumber(TEXT("UnleashedAt"), C.Now());
		C.Test.TestEqual(TEXT("Strength doubles"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()), Str * 2.0f, 0.01f);
		C.Test.TestEqual(TEXT("Agility doubles"), C.Stat(Player, UCrossguardAttributeSet::GetAgilityAttribute()), Agi * 2.0f, 0.01f);
		C.Test.TestEqual(TEXT("Intelligence doubles"), C.Stat(Player, UCrossguardAttributeSet::GetIntelligenceAttribute()), Int * 2.0f, 0.01f);
		C.Test.AddInfo(FString::Printf(TEXT("Unleash: Max HP %.2f -> %.2f (expected %.2f)"), Max, C.MaxHealth(Player), ExpectedMax));
		C.Test.TestEqual(TEXT("Max HP follows the doubled Strength"), C.MaxHealth(Player), ExpectedMax, 0.5f);
		C.Test.TestTrue(TEXT("Unleash is on cooldown"), C.IsOnCooldown(Player, TEXT("unleash")));
		C.SetTimeDilation(FastForward * 0.5f);
	});

	Scenario.Wait(ExpectedUnleashSeconds - 2.0f);

	Scenario.Step(TEXT("Still doubled after 13 s"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = Character(C, TEXT("Player"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Strength still doubled at 13 s"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()),
			static_cast<float>(C.Number(TEXT("Str"))) * 2.0f, 0.01f);
	});

	Scenario.Wait(3.0f);

	Scenario.Step(TEXT("Back to normal after 15 s"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		ACrossguardCharacter* Player = Character(C, TEXT("Player"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Strength back"), C.Stat(Player, UCrossguardAttributeSet::GetStrengthAttribute()),
			static_cast<float>(C.Number(TEXT("Str"))), 0.01f);
		C.Test.TestEqual(TEXT("Agility back"), C.Stat(Player, UCrossguardAttributeSet::GetAgilityAttribute()),
			static_cast<float>(C.Number(TEXT("Agi"))), 0.01f);
		C.Test.TestEqual(TEXT("Intelligence back"), C.Stat(Player, UCrossguardAttributeSet::GetIntelligenceAttribute()),
			static_cast<float>(C.Number(TEXT("Int"))), 0.01f);
		C.Test.TestEqual(TEXT("Max HP back"), C.MaxHealth(Player), static_cast<float>(C.Number(TEXT("Max"))), 0.5f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "benediction aimed at a dead body (DebugReviveDelay 0) -> It stands up at full HP with
// its gear." Plus the ruling's fallback: nobody aimed at heals yourself to full.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportBenediction, "Crossguard.Scenario.Skills.Benediction", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportBenediction::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A priest (wand, Benediction) and a swordsman 4 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Priest = C.SpawnPlayer(TEXT("Priest"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Fallen = C.SpawnPlayer(TEXT("Fallen"), FVector(400.0f, Row, 0.0f), 180.0f);
		if (!C.Require(Priest != nullptr && Fallen != nullptr, TEXT("priest and swordsman spawned")))
		{
			return;
		}
		if (!C.Equip(Priest, TEXT("wand_one_handed")) || !C.Equip(Fallen, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Priest, {TEXT("benediction")}, CostBenediction), TEXT("Benediction granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The swordsman dies"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Fallen = Character(C, TEXT("Fallen"));
		if (Fallen == nullptr)
		{
			C.Fail(TEXT("swordsman gone"));
			return;
		}
		C.Kill(Fallen);
	});

	Scenario.Wait(1.0f);

	Scenario.Step(TEXT("The priest aims at the body"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Priest = Character(C, TEXT("Priest"));
		ACrossguardCharacter* Fallen = Character(C, TEXT("Fallen"));
		if (Priest == nullptr || Fallen == nullptr)
		{
			C.Fail(TEXT("priest or body gone"));
			return;
		}
		if (!C.Require(C.IsDead(Fallen), TEXT("the swordsman is dead")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Death took the sword off the hands (it is on the body)"),
			Fallen->GetEquipmentComponent()->GetItem(ECrossguardEquipmentSlot::MainHand).WeaponId.IsNone());
		// The body is the ragdolled mesh.
		C.AimAt(Priest, Fallen->GetMesh()->GetComponentLocation());
	});

	Scenario.Wait(ViewCatchUp);

	Scenario.Step(TEXT("Benediction aimed at the body revives it with its gear"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Priest = Character(C, TEXT("Priest"));
		ACrossguardCharacter* Fallen = Character(C, TEXT("Fallen"));
		if (Priest == nullptr || Fallen == nullptr)
		{
			C.Fail(TEXT("priest or body gone"));
			return;
		}
		if (!C.Require(C.ActivateSkill(Priest, TEXT("benediction")), TEXT("Benediction activates")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("The swordsman is alive again"), C.IsDead(Fallen));
		C.Test.TestEqual(TEXT("Revived at full HP"), C.Health(Fallen), C.MaxHealth(Fallen), 0.01f);
		C.Test.TestTrue(TEXT("The sword is back in hand"),
			Fallen->GetEquipmentComponent()->GetItem(ECrossguardEquipmentSlot::MainHand).WeaponId == FName(TEXT("sword_one_handed")));
		C.Test.TestTrue(TEXT("Log shows the revive"), C.LogContains(TEXT("revived")));
		C.Test.TestTrue(TEXT("Benediction is on cooldown"), C.IsOnCooldown(Priest, TEXT("benediction")));
		C.SetTimeDilation(20.0f);
	});

	// 120 s cooldown (less with Spell CDR).
	Scenario.WaitUntil(TEXT("Benediction is ready again"), 125.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Priest = FindCharacter(C, TEXT("Priest"));
		return Priest != nullptr && !C.IsOnCooldown(Priest, TEXT("benediction"));
	});

	Scenario.Step(TEXT("The priest turns to face nothing"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		// Nobody along +Y within 15 m.
		if (ACrossguardCharacter* Priest = Character(C, TEXT("Priest")))
		{
			C.Face(Priest, 90.0f);
		}
	});

	Scenario.Wait(ViewCatchUp);

	Scenario.Step(TEXT("Hurt, the priest casts Benediction at nothing: full HP"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Priest = Character(C, TEXT("Priest"));
		ACrossguardCharacter* Fallen = Character(C, TEXT("Fallen"));
		if (Priest == nullptr || Fallen == nullptr)
		{
			C.Fail(TEXT("priest or swordsman gone"));
			return;
		}
		SetHealth(Priest, C.MaxHealth(Priest) * 0.5f);
		SetHealth(Fallen, C.MaxHealth(Fallen) * 0.5f);
		const float FallenBefore = C.Health(Fallen);
		if (!C.Require(C.ActivateSkill(Priest, TEXT("benediction")), TEXT("Benediction activates the second time")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("With nobody aimed at, the priest heals itself to full"), C.Health(Priest), C.MaxHealth(Priest), 0.01f);
		C.Test.TestEqual(TEXT("The swordsman (not aimed at) is not healed"), C.Health(Fallen), FallenBefore, 0.001f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "multiscale with a party member (second client, same ?Party=) -> Both have +0.15 Attack
// and Magic Resistance; gone when unequipped."

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportMultiscale, "Crossguard.Scenario.Skills.Multiscale", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportMultiscale::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	static const TCHAR* const AllNames[] = {TEXT("Knight"), TEXT("Friend"), TEXT("Outsider")};
	static const TCHAR* const PartyNames[] = {TEXT("Knight"), TEXT("Friend")};

	Scenario.Step(TEXT("Two party members (one in Plate) and an outsider"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.SpawnPlayer(TEXT("Knight"), FVector(0.0f, Row, 0.0f), 0.0f, TEXT("Scales"));
		ACrossguardCharacter* Friend = C.SpawnPlayer(TEXT("Friend"), FVector(0.0f, Row + 2000.0f, 0.0f), 0.0f, TEXT("Scales"));
		ACrossguardCharacter* Outsider = C.SpawnPlayer(TEXT("Outsider"), FVector(300.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Knight != nullptr && Friend != nullptr && Outsider != nullptr, TEXT("knight, friend and outsider spawned")))
		{
			return;
		}
		// Multiscale needs Plate armor (Skill Forge).
		EquipArmorPiece(C, Knight, TEXT("plate_cuirass"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The knight equips Multiscale"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = Character(C, TEXT("Knight"));
		if (!C.Require(Knight != nullptr && C.HasTag(Knight, CrossguardTags::Equipment_Armor_Plate),
			TEXT("the knight counts as wearing Plate (Equipment.Armor.Plate)")))
		{
			return;
		}
		for (const TCHAR* Name : AllNames)
		{
			ACrossguardCharacter* Member = Character(C, Name);
			if (Member == nullptr)
			{
				C.Fail(TEXT("a player is gone"));
				return;
			}
			C.SetNumber(FName(*(FString(Name) + TEXT(".Ar"))), C.Stat(Member, UCrossguardAttributeSet::GetAttackResistanceAttribute()));
			C.SetNumber(FName(*(FString(Name) + TEXT(".Mr"))), C.Stat(Member, UCrossguardAttributeSet::GetMagicResistanceAttribute()));
		}
		C.Require(GrantSkillsAtCost(C, Knight, {TEXT("multiscale")}, CostMultiscale), TEXT("Multiscale granted"));
	});

	// The aura refreshes every second.
	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("The knight and the party member have +0.15; the outsider nothing"), [](FScenarioContext& C)
	{
		for (const TCHAR* Name : AllNames)
		{
			ACrossguardCharacter* Member = Character(C, Name);
			if (Member == nullptr)
			{
				C.Fail(TEXT("a player is gone"));
				return;
			}
			const bool bParty = FCString::Strcmp(Name, TEXT("Outsider")) != 0;
			const float Bonus = bParty ? ExpectedMultiscaleResistance : 0.0f;
			C.Test.TestEqual(*FString::Printf(TEXT("%s: Attack Resistance %s"), Name, bParty ? TEXT("+0.15") : TEXT("unchanged")),
				C.Stat(Member, UCrossguardAttributeSet::GetAttackResistanceAttribute()),
				static_cast<float>(C.Number(FName(*(FString(Name) + TEXT(".Ar"))))) + Bonus, 0.001f);
			C.Test.TestEqual(*FString::Printf(TEXT("%s: Magic Resistance %s"), Name, bParty ? TEXT("+0.15") : TEXT("unchanged")),
				C.Stat(Member, UCrossguardAttributeSet::GetMagicResistanceAttribute()),
				static_cast<float>(C.Number(FName(*(FString(Name) + TEXT(".Mr"))))) + Bonus, 0.001f);
			C.Test.TestTrue(*FString::Printf(TEXT("%s: State.Multiscale %s"), Name, bParty ? TEXT("on") : TEXT("off")),
				C.HasTag(Member, CrossguardTags::State_Multiscale) == bParty);
		}
		C.Require(UnequipSkills(C, Character(C, TEXT("Knight")), TEXT("multiscale")), TEXT("Multiscale unequipped"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Unequipped: the bonus is gone from everyone"), [](FScenarioContext& C)
	{
		for (const TCHAR* Name : PartyNames)
		{
			ACrossguardCharacter* Member = Character(C, Name);
			if (Member == nullptr)
			{
				C.Fail(TEXT("a player is gone"));
				return;
			}
			C.Test.TestEqual(*FString::Printf(TEXT("%s: Attack Resistance back"), Name), C.Stat(Member, UCrossguardAttributeSet::GetAttackResistanceAttribute()),
				static_cast<float>(C.Number(FName(*(FString(Name) + TEXT(".Ar"))))), 0.001f);
			C.Test.TestEqual(*FString::Printf(TEXT("%s: Magic Resistance back"), Name), C.Stat(Member, UCrossguardAttributeSet::GetMagicResistanceAttribute()),
				static_cast<float>(C.Number(FName(*(FString(Name) + TEXT(".Mr"))))), 0.001f);
			C.Test.TestFalse(*FString::Printf(TEXT("%s: no State.Multiscale"), Name), C.HasTag(Member, CrossguardTags::State_Multiscale));
		}
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "hunters_tracking, second client (enemy) walks around -> Small grey marks follow their
// path on your screen, fading after 30 s." Checked through the tracker's footprint instances
// (UCrossguardTrackingComponent, an instanced static mesh): no rendering needed.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportHuntersTracking, "Crossguard.Scenario.Skills.HuntersTracking", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportHuntersTracking::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A tracker with Hunter's Tracking and an enemy player"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tracker = C.SpawnPlayer(TEXT("Tracker"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Enemy = C.SpawnPlayer(TEXT("Enemy"), FVector(500.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Tracker != nullptr && Enemy != nullptr, TEXT("tracker and enemy spawned")))
		{
			return;
		}
		if (!C.Require(Tracker->FindComponentByClass<UCrossguardTrackingComponent>() != nullptr, TEXT("the character has a tracking component")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Tracker, {TEXT("hunters_tracking")}, CostHuntersTracking), TEXT("Hunter's Tracking granted"));
		C.SetNumber(TEXT("Steps"), 0.0);
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The passive is on"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tracker = Character(C, TEXT("Tracker"));
		if (Tracker == nullptr)
		{
			C.Fail(TEXT("tracker gone"));
			return;
		}
		C.Test.TestTrue(TEXT("Passive.HuntersTracking on the tracker"), C.HasTag(Tracker, CrossguardTags::Passive_HuntersTracking));
		C.Test.TestEqual(TEXT("No footprints yet"), Tracker->FindComponentByClass<UCrossguardTrackingComponent>()->GetInstanceCount(), 0);
	});

	// The enemy walks along +X, 1.2 m every 0.3 s (a print every 80 cm walked on the ground).
	Scenario.WaitUntil(TEXT("Footprints appear behind the walking enemy"), 10.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tracker = FindCharacter(C, TEXT("Tracker"));
		ACrossguardCharacter* Enemy = FindCharacter(C, TEXT("Enemy"));
		if (Tracker == nullptr || Enemy == nullptr)
		{
			return false;
		}
		const double Steps = C.Number(TEXT("Steps")) + 1.0;
		C.SetNumber(TEXT("Steps"), Steps);
		C.Place(Enemy, FVector(500.0f + 120.0f * static_cast<float>(FMath::Min(Steps, 20.0)), Row, 0.0f), 0.0f);
		return Tracker->FindComponentByClass<UCrossguardTrackingComponent>()->GetInstanceCount() >= 3;
	}, 0.3f);

	Scenario.Step(TEXT("The enemy stops; the prints stay for 30 s"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tracker = Character(C, TEXT("Tracker"));
		if (Tracker == nullptr)
		{
			C.Fail(TEXT("tracker gone"));
			return;
		}
		const int32 Prints = Tracker->FindComponentByClass<UCrossguardTrackingComponent>()->GetInstanceCount();
		C.Test.AddInfo(FString::Printf(TEXT("%d footprints after %d steps"), Prints, static_cast<int32>(C.Number(TEXT("Steps")))));
		C.SetNumber(TEXT("StoppedAt"), C.Now());
		C.SetTimeDilation(FastForward);
	});

	Scenario.Wait(TrackingTrailSeconds - 5.0f);

	Scenario.Step(TEXT("Prints are still there after 25 s"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tracker = Character(C, TEXT("Tracker"));
		if (Tracker == nullptr)
		{
			C.Fail(TEXT("tracker gone"));
			return;
		}
		C.Test.TestTrue(TEXT("Footprints remain within 30 s"), Tracker->FindComponentByClass<UCrossguardTrackingComponent>()->GetInstanceCount() > 0);
	});

	Scenario.WaitUntil(TEXT("The prints fade"), 15.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Tracker = FindCharacter(C, TEXT("Tracker"));
		return Tracker != nullptr && Tracker->FindComponentByClass<UCrossguardTrackingComponent>()->GetInstanceCount() == 0;
	});

	Scenario.Step(TEXT("Gone about 30 s after the last step"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("StoppedAt")));
		C.Test.AddInfo(FString::Printf(TEXT("The last prints faded %.2f s after the enemy stopped"), Elapsed));
		C.Test.TestEqual(TEXT("Footprints fade after 30 s"), Elapsed, TrackingTrailSeconds, 1.5f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "summon_skeletons -> 6 cylinders appear, attack the dummy, follow you otherwise, vanish
// after 60 s; casting again replaces them." (No NavMesh here: minions walk straight at their goal.)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportSummonSkeletons, "Crossguard.Scenario.Skills.SummonSkeletons", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportSummonSkeletons::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A necromancer (wand, Summon Skeletons) and a dummy ahead, off the Mage's line of fire"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Necromancer = C.SpawnPlayer(TEXT("Necromancer"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(600.0f, Row + 600.0f, 0.0f), 180.0f);
		if (!C.Require(Necromancer != nullptr && Dummy != nullptr, TEXT("necromancer and dummy spawned"))
			|| !C.Equip(Necromancer, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Necromancer, {TEXT("summon_skeletons")}, CostSummonSkeletons), TEXT("Summon Skeletons granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Summon: 5 Warriors (40 HP) and 1 Mage (30 HP) for 60 s"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Necromancer = Character(C, TEXT("Necromancer"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Necromancer == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("necromancer or dummy gone"));
			return;
		}
		C.SetNumber(TEXT("DummyBefore"), C.Health(Dummy));
		if (!C.Require(C.ActivateSkill(Necromancer, TEXT("summon_skeletons")), TEXT("Summon Skeletons activates")))
		{
			return;
		}
		const TArray<ACrossguardMinion*> Minions = MinionsOf(C, Necromancer);
		C.Test.TestEqual(TEXT("Six skeletons appear"), Minions.Num(), SkeletonCount);
		int32 Warriors = 0;
		int32 Mages = 0;
		for (int32 Index = 0; Index < Minions.Num(); ++Index)
		{
			ACrossguardMinion* Minion = Minions[Index];
			C.Remember(FName(*FString::Printf(TEXT("First%d"), Index)), Minion);
			if (Minion->CombatRole == ECrossguardCombatRole::Mage)
			{
				++Mages;
				C.Test.TestEqual(TEXT("The Mage has 30 HP"), C.MaxHealth(Minion), ExpectedSkeletonMageHealth, 0.01f);
			}
			else
			{
				++Warriors;
				C.Test.TestEqual(TEXT("A Warrior has 40 HP"), C.MaxHealth(Minion), ExpectedSkeletonWarriorHealth, 0.01f);
			}
			C.Test.TestEqual(TEXT("A skeleton lives 60 s"), Minion->GetLifeSpan(), ExpectedSkeletonSeconds, 0.2f);
		}
		C.Test.TestEqual(TEXT("Five Warriors"), Warriors, ExpectedSkeletonWarriors);
		C.Test.TestEqual(TEXT("One Mage"), Mages, SkeletonCount - ExpectedSkeletonWarriors);
	});

	Scenario.WaitUntil(TEXT("The skeletons attack the dummy"), 15.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = FindCharacter(C, TEXT("Dummy"));
		return Dummy == nullptr || C.Health(Dummy) < C.Number(TEXT("DummyBefore")) - 0.01;
	});

	Scenario.Step(TEXT("Remove the dummy; speed up for the 30 s cooldown"), [](FScenarioContext& C)
	{
		// Without a target they follow the necromancer (and the Mage stops firing past it).
		if (ACrossguardCharacter* Dummy = FindCharacter(C, TEXT("Dummy")))
		{
			Dummy->Destroy();
		}
		C.SetTimeDilation(FastForward);
	});

	Scenario.WaitUntil(TEXT("Summon Skeletons is ready again"), 35.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Necromancer = FindCharacter(C, TEXT("Necromancer"));
		return Necromancer != nullptr && !C.IsOnCooldown(Necromancer, TEXT("summon_skeletons"));
	});

	Scenario.Step(TEXT("Cast again: the old skeletons go, six new ones come"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Necromancer = Character(C, TEXT("Necromancer"));
		if (Necromancer == nullptr)
		{
			C.Fail(TEXT("necromancer gone"));
			return;
		}
		int32 OldStanding = 0;
		for (int32 Index = 0; Index < SkeletonCount; ++Index)
		{
			OldStanding += C.FindActor(FName(*FString::Printf(TEXT("First%d"), Index))) != nullptr ? 1 : 0;
		}
		C.Test.AddInfo(FString::Printf(TEXT("%d of the first skeletons still standing before the recast"), OldStanding));
		if (!C.Require(C.ActivateSkill(Necromancer, TEXT("summon_skeletons")), TEXT("Summon Skeletons activates again")))
		{
			return;
		}
		C.SetNumber(TEXT("RecastAt"), C.Now());
		int32 OldLeft = 0;
		for (int32 Index = 0; Index < SkeletonCount; ++Index)
		{
			OldLeft += C.FindActor(FName(*FString::Printf(TEXT("First%d"), Index))) != nullptr ? 1 : 0;
		}
		C.Test.TestEqual(TEXT("Recasting dismisses the previous skeletons"), OldLeft, 0);
		C.Test.TestEqual(TEXT("Six new skeletons"), MinionsOf(C, Necromancer).Num(), SkeletonCount);
	});

	Scenario.WaitUntil(TEXT("The skeletons vanish"), ExpectedSkeletonSeconds + 5.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Necromancer = FindCharacter(C, TEXT("Necromancer"));
		return Necromancer != nullptr && MinionsOf(C, Necromancer).Num() == 0;
	});

	Scenario.Step(TEXT("They lasted 60 s"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("RecastAt")));
		C.Test.AddInfo(FString::Printf(TEXT("Skeletons lasted %.2f s"), Elapsed));
		C.Test.TestEqual(TEXT("Skeletons vanish after 60 s"), Elapsed, ExpectedSkeletonSeconds, 1.0f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "summon_tiger without a pet_gem_tiger in the inventory -> Refused; with one it works and
// the gem stays." and "summon_tiger (wand in hand), dummy 8 m away -> One cylinder; it ... hits ...
// Vanishes after 180 s." Tiger: 100 HP, 20 Attack Damage, 1.3 attacks a second (design-decisions.md).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportSummonTiger, "Crossguard.Scenario.Skills.SummonTiger", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportSummonTiger::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A tamer (wand, Summon Tiger, no gem) and a dummy 8 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tamer = C.SpawnPlayer(TEXT("Tamer"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(800.0f, Row, 0.0f), 180.0f);
		if (!C.Require(Tamer != nullptr && Dummy != nullptr, TEXT("tamer and dummy spawned")) || !C.Equip(Tamer, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Tamer, {TEXT("summon_tiger")}, CostSummonTiger), TEXT("Summon Tiger granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Without a Pet Gem (Tiger): refused"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tamer = Character(C, TEXT("Tamer"));
		if (Tamer == nullptr)
		{
			C.Fail(TEXT("tamer gone"));
			return;
		}
		C.Test.TestEqual(TEXT("No pet gem in the inventory"), Tamer->GetInventoryComponent()->CountItem(TigerPetGem), 0);
		C.Test.TestFalse(TEXT("Summon Tiger is refused without the gem"), C.ActivateSkill(Tamer, TEXT("summon_tiger")));
		C.Test.TestEqual(TEXT("No tiger"), MinionsOf(C, Tamer).Num(), 0);
		C.Test.TestFalse(TEXT("No cooldown from the refused cast"), C.IsOnCooldown(Tamer, TEXT("summon_tiger")));
		C.Require(C.GiveItem(Tamer, TigerPetGem) == 0, TEXT("the pet gem fits in the inventory"));
	});

	Scenario.Step(TEXT("With the gem: a tiger with 100 HP and 20 Attack Damage; the gem stays"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tamer = Character(C, TEXT("Tamer"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Tamer == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("tamer or dummy gone"));
			return;
		}
		C.SetNumber(TEXT("DummyBefore"), C.Health(Dummy));
		if (!C.Require(C.ActivateSkill(Tamer, TEXT("summon_tiger")), TEXT("Summon Tiger activates with the gem")))
		{
			return;
		}
		C.SetNumber(TEXT("SummonedAt"), C.Now());
		C.Test.TestEqual(TEXT("The gem is not used up"), Tamer->GetInventoryComponent()->CountItem(TigerPetGem), 1);
		C.Test.TestTrue(TEXT("Summon Tiger is on cooldown"), C.IsOnCooldown(Tamer, TEXT("summon_tiger")));
		const TArray<ACrossguardMinion*> Minions = MinionsOf(C, Tamer);
		if (!C.Require(Minions.Num() == 1, FString::Printf(TEXT("one tiger (found %d)"), Minions.Num())))
		{
			return;
		}
		ACrossguardMinion* Tiger = Minions[0];
		C.Remember(TEXT("Tiger"), Tiger);
		C.Test.TestEqual(TEXT("The tiger has 100 HP"), C.MaxHealth(Tiger), TigerHealth, 0.01f);
		C.Test.TestEqual(TEXT("The tiger's claws deal 20 Attack Damage"), Tiger->AttackDamage, TigerAttackDamage, 0.001f);
		C.Test.TestEqual(TEXT("The tiger attacks 1.3 times a second"), Tiger->AttackInterval, TigerAttackInterval, 0.001f);
		C.Test.TestEqual(TEXT("The tiger lives 180 s"), Tiger->GetLifeSpan(), TigerSeconds, 0.2f);
		C.Test.TestEqual(TEXT("The tiger reaches 2 m"), Tiger->AttackRange, CompanionBaseReach, 0.001f);
		C.Test.TestTrue(TEXT("The tiger has Hunter's Leap"), HasSkill(Tiger, TEXT("hunters_leap")));
		C.Test.TestTrue(TEXT("The tiger has Neckbite"), HasSkill(Tiger, TEXT("neckbite")));
		C.Test.TestTrue(TEXT("The tiger has Slash"), HasSkill(Tiger, TEXT("slash")));
	});

	Scenario.WaitUntil(TEXT("The tiger hits the dummy"), 15.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = FindCharacter(C, TEXT("Dummy"));
		return Dummy == nullptr || C.Health(Dummy) < C.Number(TEXT("DummyBefore")) - 0.01;
	});

	Scenario.Step(TEXT("Speed up to the end of the 180 s"), [](FScenarioContext& C)
	{
		C.Test.TestTrue(TEXT("Log shows the tiger's hits"), C.LogContains(TEXT("damage from")));
		C.SetTimeDilation(20.0f);
	});

	Scenario.WaitUntil(TEXT("The tiger vanishes"), TigerSeconds + 5.0f, [](FScenarioContext& C)
	{
		return C.FindActor(TEXT("Tiger")) == nullptr;
	});

	Scenario.Step(TEXT("It lasted 180 s"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("SummonedAt")));
		C.Test.AddInfo(FString::Printf(TEXT("The tiger lasted %.2f s"), Elapsed));
		// A tiger killed by the dummy's side would vanish earlier; the dummy does not fight back.
		C.Test.TestEqual(TEXT("The tiger vanishes after 180 s"), Elapsed, TigerSeconds, 1.5f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "summon_greater_demon near a second client -> A bigger cylinder ... Gone after 60 s."
// Demon: 220 HP, 3x the player's size, 40 Attack Damage (design-decisions.md).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportSummonGreaterDemon, "Crossguard.Scenario.Skills.SummonGreaterDemon", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportSummonGreaterDemon::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A warlock with a wand and Summon Greater Demon"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Warlock = C.SpawnPlayer(TEXT("Warlock"), FVector(0.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Warlock != nullptr, TEXT("warlock spawned")) || !C.Equip(Warlock, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Warlock, {TEXT("summon_greater_demon")}, CostSummonGreaterDemon), TEXT("Summon Greater Demon granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Summon: a 3x demon with 220 HP"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Warlock = Character(C, TEXT("Warlock"));
		if (Warlock == nullptr)
		{
			C.Fail(TEXT("warlock gone"));
			return;
		}
		if (!C.Require(C.ActivateSkill(Warlock, TEXT("summon_greater_demon")), TEXT("Summon Greater Demon activates")))
		{
			return;
		}
		C.SetNumber(TEXT("SummonedAt"), C.Now());
		const TArray<ACrossguardMinion*> Minions = MinionsOf(C, Warlock);
		if (!C.Require(Minions.Num() == 1, FString::Printf(TEXT("one demon (found %d)"), Minions.Num())))
		{
			return;
		}
		ACrossguardMinion* Demon = Minions[0];
		C.Remember(TEXT("Demon"), Demon);
		C.Test.TestEqual(TEXT("The demon has 220 HP"), C.MaxHealth(Demon), DemonHealth, 0.01f);
		C.Test.TestEqual(TEXT("The demon is 3x size"), static_cast<float>(Demon->GetActorScale3D().X), DemonScale, 0.001f);
		C.Test.TestEqual(TEXT("The demon is 3x the warlock's height"), Demon->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
			DemonScale * Warlock->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 0.1f);
		// Standing on the floor, not sunk into it: the feet within 10 cm of the warlock's.
		const float DemonFeet = static_cast<float>(Demon->GetActorLocation().Z) - Demon->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float WarlockFeet = static_cast<float>(Warlock->GetActorLocation().Z) - Warlock->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		C.Test.TestEqual(TEXT("The demon stands on the warlock's floor"), DemonFeet, WarlockFeet, 10.0f);
		C.Test.TestEqual(TEXT("The demon's sword deals 40 Attack Damage"), Demon->AttackDamage, DemonAttackDamage, 0.001f);
		C.Test.TestEqual(TEXT("The demon's reach is 3x a player-size companion's (6 m)"), Demon->AttackRange,
			DemonScale * CompanionBaseReach, 0.001f);
		C.Test.TestTrue(TEXT("The demon wields a two-handed sword"), Demon->bWieldsWeapon && Demon->bTwoHandedWeapon
			&& Demon->WeaponType == ECrossguardWeaponType::Sword);
		C.Test.TestEqual(TEXT("The demon lives 60 s"), Demon->GetLifeSpan(), DemonSeconds, 0.2f);
		C.Test.TestTrue(TEXT("The demon has Mortal Strike"), HasSkill(Demon, TEXT("mortal_strike")));
		C.Test.TestTrue(TEXT("The demon has Eldritch Shield"), HasSkill(Demon, TEXT("eldritch_shield")));
		C.Test.TestTrue(TEXT("The demon has Shade Blow"), HasSkill(Demon, TEXT("shade_blow")));
		C.SetTimeDilation(FastForward);
	});

	Scenario.WaitUntil(TEXT("The demon vanishes"), DemonSeconds + 5.0f, [](FScenarioContext& C)
	{
		return C.FindActor(TEXT("Demon")) == nullptr;
	});

	Scenario.Step(TEXT("It lasted 60 s"), [](FScenarioContext& C)
	{
		C.SetTimeDilation(1.0f);
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("SummonedAt")));
		C.Test.AddInfo(FString::Printf(TEXT("The demon lasted %.2f s"), Elapsed));
		C.Test.TestEqual(TEXT("The demon is gone after 60 s"), Elapsed, DemonSeconds, 1.0f);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "Tiger's Neckbite lands on the second client -> For 2 s they cannot cast skills (basic
// attacks still work); a channel in progress stops." The victim flies (Flight: the running skill)
// and holds a wand (a hitscan basic attack, which no guard reacts to).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportNeckbiteSilence, "Crossguard.Scenario.Skills.NeckbiteSilence", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportNeckbiteSilence::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A tamer with the gem; a victim (wand, Flight, Heal) 4.5 m ahead"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tamer = C.SpawnPlayer(TEXT("Tamer"), FVector(0.0f, Row, 0.0f), 0.0f);
		ACrossguardCharacter* Victim = C.SpawnPlayer(TEXT("Victim"), FVector(450.0f, Row, 0.0f), 180.0f);
		if (!C.Require(Tamer != nullptr && Victim != nullptr, TEXT("tamer and victim spawned")))
		{
			return;
		}
		if (!C.Equip(Tamer, TEXT("wand_one_handed")) || !C.Equip(Victim, TEXT("wand_one_handed")))
		{
			return;
		}
		if (!C.Require(GrantSkillsAtCost(C, Tamer, {TEXT("summon_tiger")}, CostSummonTiger), TEXT("Summon Tiger granted")))
		{
			return;
		}
		if (!C.Require(GrantSkillsAtCost(C, Victim, {TEXT("flight"), TEXT("heal")}, CostFlight + CostHeal), TEXT("Flight and Heal granted to the victim")))
		{
			return;
		}
		C.Require(C.GiveItem(Tamer, TigerPetGem) == 0, TEXT("the pet gem fits in the inventory"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("The victim takes off; the tiger is summoned between them"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tamer = Character(C, TEXT("Tamer"));
		ACrossguardCharacter* Victim = Character(C, TEXT("Victim"));
		if (Tamer == nullptr || Victim == nullptr)
		{
			C.Fail(TEXT("tamer or victim gone"));
			return;
		}
		if (!C.Require(C.ActivateSkill(Victim, TEXT("flight")), TEXT("the victim's Flight activates")))
		{
			return;
		}
		C.Face(Tamer, 0.0f);
		if (!C.Require(C.ActivateSkill(Tamer, TEXT("summon_tiger")), TEXT("Summon Tiger activates")))
		{
			return;
		}
		const TArray<ACrossguardMinion*> Minions = MinionsOf(C, Tamer);
		if (C.Require(Minions.Num() == 1, TEXT("one tiger")))
		{
			C.Remember(TEXT("Tiger"), Minions[0]);
		}
	});

	// The tiger readies Neckbite within reach + 2 m and claws once a second.
	Scenario.WaitUntil(TEXT("Neckbite Silences the victim"), 10.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Victim = FindCharacter(C, TEXT("Victim"));
		return Victim != nullptr && C.HasTag(Victim, CrossguardTags::State_Silenced);
	});

	Scenario.Step(TEXT("Silenced: skills fail, the running skill stopped, basic attacks work"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Victim = Character(C, TEXT("Victim"));
		if (Victim == nullptr)
		{
			C.Fail(TEXT("victim gone"));
			return;
		}
		C.SetNumber(TEXT("SilencedAt"), C.Now());
		// The tiger is done: it would keep clawing the victim.
		if (AActor* Tiger = C.FindActor(TEXT("Tiger")))
		{
			Tiger->Destroy();
		}
		C.Test.TestFalse(TEXT("The Silence interrupted Flight (no State.Flying)"), C.HasTag(Victim, CrossguardTags::State_Flying));
		C.Test.TestFalse(TEXT("The victim is no longer in Flying movement mode"), IsFlyingMode(Victim));
		C.Test.TestFalse(TEXT("A Silenced character cannot cast Heal"), C.ActivateSkill(Victim, TEXT("heal")));
		C.Test.TestTrue(TEXT("A Silenced character's basic attack still works"), C.BasicAttack(Victim));
	});

	Scenario.WaitUntil(TEXT("The Silence wears off"), SilenceSeconds + 1.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Victim = FindCharacter(C, TEXT("Victim"));
		return Victim != nullptr && !C.HasTag(Victim, CrossguardTags::State_Silenced);
	});

	Scenario.Step(TEXT("After 2 s skills work again"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Victim = Character(C, TEXT("Victim"));
		if (Victim == nullptr)
		{
			C.Fail(TEXT("victim gone"));
			return;
		}
		const float Elapsed = C.Now() - static_cast<float>(C.Number(TEXT("SilencedAt")));
		C.Test.TestTrue(TEXT("The Silence lasted about 2 s"), Elapsed <= SilenceSeconds + 0.1f);
		C.Test.TestTrue(TEXT("Heal can be cast again"), C.ActivateSkill(Victim, TEXT("heal")));
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// The Silence ruling on its own, without the tiger's AI: State.Silenced applied directly for 2 s
// blocks skills, not basic attacks.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportSilence, "Crossguard.Scenario.Skills.Silence", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportSilence::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a wand and Heal"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, Row, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("player spawned")) || !C.Equip(Player, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Player, {TEXT("heal")}, CostHeal), TEXT("Heal granted"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Silenced for 2 s: Heal fails, the basic attack works"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = Character(C, TEXT("Player"));
		UAbilitySystemComponent* ASC = AbilitySystemOf(Player);
		if (Player == nullptr || ASC == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		UCrossguardSkillAbility::ApplyStatus(ASC, ASC, CrossguardTags::State_Silenced, SilenceSeconds);
		if (!C.Require(C.HasTag(Player, CrossguardTags::State_Silenced), TEXT("State.Silenced applied")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("A Silenced character cannot cast Heal"), C.ActivateSkill(Player, TEXT("heal")));
		C.Test.TestFalse(TEXT("No cooldown from the refused cast"), C.IsOnCooldown(Player, TEXT("heal")));
		C.Test.TestTrue(TEXT("A Silenced character's basic attack works"), C.BasicAttack(Player));
	});

	Scenario.Wait(SilenceSeconds + 0.5f);

	Scenario.Step(TEXT("The Silence is over: Heal works"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = Character(C, TEXT("Player"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("player gone"));
			return;
		}
		C.Test.TestFalse(TEXT("No State.Silenced after 2.5 s"), C.HasTag(Player, CrossguardTags::State_Silenced));
		C.Test.TestTrue(TEXT("Heal can be cast again"), C.ActivateSkill(Player, TEXT("heal")));
	});

	Scenario.Run();
	return true;
}

namespace CrossguardScenarioSupportSkills
{
	// Amounts of the "<Victim> took X damage from <Source>" lines in the captured log.
	TArray<float> DamageAmounts(const FScenarioContext& C, const AActor* Victim, const AActor* Source)
	{
		TArray<float> Amounts;
		if (Victim == nullptr || Source == nullptr)
		{
			return Amounts;
		}
		const FString VictimPart = FString::Printf(TEXT(": %s took "), *Victim->GetName());
		const FString SourcePart = FString::Printf(TEXT(" damage from %s ("), *Source->GetName());
		for (const FString& Line : C.LogLines())
		{
			const int32 Start = Line.Find(VictimPart);
			if (Start == INDEX_NONE || !Line.Contains(SourcePart))
			{
				continue;
			}
			Amounts.Add(FCString::Atof(*Line.Mid(Start + VictimPart.Len())));
		}
		return Amounts;
	}

	float MaxAmount(const TArray<float>& Amounts)
	{
		float Max = 0.0f;
		for (const float Amount : Amounts)
		{
			Max = FMath::Max(Max, Amount);
		}
		return Max;
	}
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "flight: Space / Left Ctrl". Left Ctrl (OnFlyDown) is downward movement input while
// flying: after rising for 1.5 s, 1 s of it brings the flier down, still flying.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportFlightDescend, "Crossguard.Scenario.Skills.FlightDescend", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportFlightDescend::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	static constexpr float SceneRow = 7000.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a wand and Flight"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = C.SpawnPlayer(TEXT("Flier"), FVector(0.0f, SceneRow, 0.0f), 0.0f);
		if (!C.Require(Flier != nullptr, TEXT("flier spawned")) || !C.Equip(Flier, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Flier, {TEXT("flight")}, CostFlight), TEXT("Flight granted"));
	});
	Scenario.Wait(0.5f);
	Scenario.Step(TEXT("Take off"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Flier == nullptr || !C.Require(C.ActivateSkill(Flier, TEXT("flight")), TEXT("Flight activates")))
		{
			return;
		}
		C.SetNumber(TEXT("GroundZ"), Flier->GetActorLocation().Z);
		C.SetNumber(TEXT("RiseAt"), C.Now());
	});
	Scenario.WaitUntil(TEXT("Space: rise for 1.5 s"), 4.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = FindCharacter(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			return false;
		}
		Flier->AddMovementInput(FVector::UpVector, 1.0f);
		return C.Now() - C.Number(TEXT("RiseAt")) >= 1.5;
	});
	Scenario.Step(TEXT("Up; now Left Ctrl"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			return;
		}
		C.SetNumber(TEXT("TopZ"), Flier->GetActorLocation().Z);
		C.SetNumber(TEXT("DescendAt"), C.Now());
		C.Test.TestTrue(TEXT("Rose more than 1 m"), Flier->GetActorLocation().Z - C.Number(TEXT("GroundZ")) > 100.0);
	});
	// What holding Left Ctrl does (OnFlyDown): downward movement input, for 1 s.
	Scenario.WaitUntil(TEXT("Left Ctrl: descend for 1 s"), 4.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Flier = FindCharacter(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			return false;
		}
		Flier->AddMovementInput(FVector::UpVector, -1.0f);
		return C.Now() - C.Number(TEXT("DescendAt")) >= 1.0;
	});
	Scenario.Step(TEXT("Lower, still flying"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Flier = Character(C, TEXT("Flier"));
		if (Flier == nullptr)
		{
			return;
		}
		const float Dropped = static_cast<float>(C.Number(TEXT("TopZ")) - Flier->GetActorLocation().Z);
		C.Test.AddInfo(FString::Printf(TEXT("Descended %.1f cm in 1 s"), Dropped));
		C.Test.TestTrue(TEXT("Downward input brings the flier down (more than 50 cm in 1 s)"), Dropped > 50.0f);
		C.Test.TestTrue(TEXT("Still flying"), C.HasTag(Flier, CrossguardTags::State_Flying));
		C.Test.TestTrue(TEXT("Still in the air"), Flier->GetActorLocation().Z - C.Number(TEXT("GroundZ")) > 20.0);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "summon_skeletons ... follow you otherwise". No enemy around: the six walk after the
// necromancer who moved 20 m away, to the controller's follow distance (3 m).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportSummonSkeletonsFollow, "Crossguard.Scenario.Skills.SummonSkeletonsFollow", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportSummonSkeletonsFollow::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	static constexpr float SceneRow = 7500.0f;
	static constexpr float FollowSlack = 450.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A necromancer with Summon Skeletons, nobody else around"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Necromancer = C.SpawnPlayer(TEXT("Necromancer"), FVector(0.0f, SceneRow, 0.0f), 0.0f);
		if (!C.Require(Necromancer != nullptr, TEXT("necromancer spawned")) || !C.Equip(Necromancer, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Necromancer, {TEXT("summon_skeletons")}, CostSummonSkeletons), TEXT("Summon Skeletons granted"));
	});
	Scenario.Wait(0.5f);
	Scenario.Step(TEXT("Summon, then walk 20 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Necromancer = Character(C, TEXT("Necromancer"));
		if (Necromancer == nullptr || !C.Require(C.ActivateSkill(Necromancer, TEXT("summon_skeletons")), TEXT("Summon Skeletons activates")))
		{
			return;
		}
		if (!C.Require(MinionsOf(C, Necromancer).Num() == SkeletonCount, TEXT("six skeletons")))
		{
			return;
		}
		C.Place(Necromancer, FVector(2000.0f, SceneRow, 0.0f), 0.0f);
	});
	Scenario.WaitUntil(TEXT("all six skeletons come to within 4.5 m of the necromancer"), 20.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Necromancer = FindCharacter(C, TEXT("Necromancer"));
		if (Necromancer == nullptr)
		{
			return false;
		}
		const TArray<ACrossguardMinion*> Minions = MinionsOf(C, Necromancer);
		if (Minions.Num() != SkeletonCount)
		{
			return false;
		}
		for (const ACrossguardMinion* Minion : Minions)
		{
			if (FVector::Dist2D(Minion->GetActorLocation(), Necromancer->GetActorLocation()) > FollowSlack)
			{
				return false;
			}
		}
		return true;
	});
	Scenario.Step(TEXT("They followed"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Necromancer = Character(C, TEXT("Necromancer"));
		if (Necromancer == nullptr)
		{
			return;
		}
		for (const ACrossguardMinion* Minion : MinionsOf(C, Necromancer))
		{
			C.Test.AddInfo(FString::Printf(TEXT("%s is %.0f cm from the necromancer"), *Minion->GetName(),
				FVector::Dist2D(Minion->GetActorLocation(), Necromancer->GetActorLocation())));
		}
		C.Test.TestEqual(TEXT("Still six"), MinionsOf(C, Necromancer).Num(), SkeletonCount);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "summon_tiger: leap, riders, hits for 20". The tiger uses Hunter's Leap on a dummy 8 m
// off, bites (Neckbite: +10 Magic, Silence) and slashes (+7 Attack): the skills go on cooldown, the
// dummy is Silenced, and a hit carries at least the +10 Magic over the 20 claw.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportSummonTigerLeapAndRiders, "Crossguard.Scenario.Skills.SummonTigerLeapAndRiders", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportSummonTigerLeapAndRiders::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	static constexpr float SceneRow = 8000.0f;
	static constexpr float NeckbiteMagic = 10.0f;
	static constexpr float SlashAttack = 7.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A tamer with the gem and Summon Tiger, a dummy 8 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tamer = C.SpawnPlayer(TEXT("Tamer"), FVector(0.0f, SceneRow, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(800.0f, SceneRow, 0.0f), 180.0f);
		if (!C.Require(Tamer != nullptr && Dummy != nullptr, TEXT("tamer and dummy spawned")) || !C.Equip(Tamer, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Tamer, {TEXT("summon_tiger")}, CostSummonTiger) && C.GiveItem(Tamer, TigerPetGem) == 0,
			TEXT("Summon Tiger granted and the gem in the inventory"));
	});
	Scenario.Wait(0.5f);
	Scenario.Step(TEXT("Summon the tiger"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Tamer = Character(C, TEXT("Tamer"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Tamer == nullptr || Dummy == nullptr || !C.Require(C.ActivateSkill(Tamer, TEXT("summon_tiger")), TEXT("Summon Tiger activates")))
		{
			return;
		}
		const TArray<ACrossguardMinion*> Minions = MinionsOf(C, Tamer);
		if (!C.Require(Minions.Num() == 1, TEXT("one tiger")))
		{
			return;
		}
		C.Remember(TEXT("Tiger"), Minions[0]);
		C.ClearLog();
		C.SetNumber(TEXT("Claw"), C.ExpectedDamage(Minions[0], Dummy, TigerAttackDamage));
		C.SetNumber(TEXT("Neckbite"), C.ExpectedDamage(Minions[0], Dummy, TigerAttackDamage, NeckbiteMagic));
		C.SetNumber(TEXT("Slash"), C.ExpectedDamage(Minions[0], Dummy, TigerAttackDamage + SlashAttack));
	});
	Scenario.WaitUntil(TEXT("the tiger leaps, bites (the dummy is Silenced) and slashes"), 25.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		const ACrossguardMinion* Tiger = Cast<ACrossguardMinion>(C.FindActor(TEXT("Tiger")));
		if (Dummy == nullptr || Tiger == nullptr)
		{
			return false;
		}
		// The dummy must outlive 25 s of a 20-damage tiger.
		C.SetHealth(Dummy, C.MaxHealth(Dummy));
		if (C.IsOnCooldown(Tiger, TEXT("hunters_leap")))
		{
			C.SetNumber(TEXT("Leapt"), 1.0);
		}
		if (C.IsOnCooldown(Tiger, TEXT("neckbite")))
		{
			C.SetNumber(TEXT("Bit"), 1.0);
		}
		if (C.HasTag(Dummy, CrossguardTags::State_Silenced))
		{
			C.SetNumber(TEXT("Silenced"), 1.0);
		}
		if (C.IsOnCooldown(Tiger, TEXT("slash")))
		{
			C.SetNumber(TEXT("Slashed"), 1.0);
		}
		return C.Number(TEXT("Leapt")) > 0.0 && C.Number(TEXT("Bit")) > 0.0 && C.Number(TEXT("Silenced")) > 0.0 && C.Number(TEXT("Slashed")) > 0.0
			&& MaxAmount(DamageAmounts(C, Dummy, Tiger)) >= C.Number(TEXT("Neckbite")) - 0.5;
	});
	// The riders have cooldowns (Slash 4 s, Neckbite 7 s): between them the claws hit plain.
	Scenario.WaitUntil(TEXT("a plain claw hit lands for the 20"), 12.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		const AActor* Tiger = C.FindActor(TEXT("Tiger"));
		if (Dummy == nullptr || Tiger == nullptr)
		{
			return false;
		}
		C.SetHealth(Dummy, C.MaxHealth(Dummy));
		for (const float Amount : DamageAmounts(C, Dummy, Tiger))
		{
			if (FMath::IsNearlyEqual(Amount, static_cast<float>(C.Number(TEXT("Claw"))), 0.5f))
			{
				return true;
			}
		}
		return false;
	});
	Scenario.Step(TEXT("Leap, Neckbite, Slash and the riders' numbers"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		const AActor* Tiger = C.FindActor(TEXT("Tiger"));
		C.Test.TestTrue(TEXT("The tiger used Hunter's Leap"), C.Number(TEXT("Leapt")) > 0.0);
		C.Test.TestTrue(TEXT("The tiger used Neckbite"), C.Number(TEXT("Bit")) > 0.0);
		C.Test.TestTrue(TEXT("Neckbite Silenced the dummy"), C.Number(TEXT("Silenced")) > 0.0);
		C.Test.TestTrue(TEXT("The tiger used Slash"), C.Number(TEXT("Slashed")) > 0.0);
		const TArray<float> Amounts = DamageAmounts(C, Dummy, Tiger);
		FString List;
		for (const float Amount : Amounts)
		{
			List += FString::Printf(TEXT("%.1f "), Amount);
		}
		C.Test.AddInfo(FString::Printf(TEXT("Tiger hits on the dummy: %s(claw %.1f, with Neckbite %.1f, with Slash %.1f)"), *List,
			C.Number(TEXT("Claw")), C.Number(TEXT("Neckbite")), C.Number(TEXT("Slash"))));
		C.Test.TestTrue(TEXT("A hit carried the +10 Magic rider (at least the Neckbite amount)"), MaxAmount(Amounts) >= C.Number(TEXT("Neckbite")) - 0.5);
		bool bPlainClaw = false;
		for (const float Amount : Amounts)
		{
			bPlainClaw |= FMath::IsNearlyEqual(Amount, static_cast<float>(C.Number(TEXT("Claw"))), 0.5f);
		}
		C.Test.TestTrue(TEXT("A plain claw hit did the 20 (after the dummy's Armor)"), bPlainClaw);
	});

	Scenario.Run();
	return true;
}

// ---------------------------------------------------------------------------------------------------
// Checklist: "summon_greater_demon ... Eldritch Shield, hits with riders". Against a dummy 4 m off,
// the demon raises Eldritch Shield, hits for its 40 (two-handed sword) and rides Shade Blow (heal
// block on the dummy) or Mortal Strike on a swing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSupportSummonGreaterDemonFights, "Crossguard.Scenario.Skills.SummonGreaterDemonFights", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSupportSummonGreaterDemonFights::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioSupportSkills;

	static constexpr float SceneRow = 8500.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A warlock with Summon Greater Demon, a dummy 4 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Warlock = C.SpawnPlayer(TEXT("Warlock"), FVector(0.0f, SceneRow, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(400.0f, SceneRow, 0.0f), 180.0f);
		if (!C.Require(Warlock != nullptr && Dummy != nullptr, TEXT("warlock and dummy spawned")) || !C.Equip(Warlock, TEXT("wand_one_handed")))
		{
			return;
		}
		C.Require(GrantSkillsAtCost(C, Warlock, {TEXT("summon_greater_demon")}, CostSummonGreaterDemon), TEXT("Summon Greater Demon granted"));
	});
	Scenario.Wait(0.5f);
	Scenario.Step(TEXT("Summon the demon"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Warlock = Character(C, TEXT("Warlock"));
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Warlock == nullptr || Dummy == nullptr || !C.Require(C.ActivateSkill(Warlock, TEXT("summon_greater_demon")), TEXT("Summon Greater Demon activates")))
		{
			return;
		}
		const TArray<ACrossguardMinion*> Minions = MinionsOf(C, Warlock);
		if (!C.Require(Minions.Num() == 1, TEXT("one demon")))
		{
			return;
		}
		C.Remember(TEXT("Demon"), Minions[0]);
		C.ClearLog();
		C.SetNumber(TEXT("Sword"), C.ExpectedDamage(Minions[0], Dummy, DemonAttackDamage));
	});
	Scenario.WaitUntil(TEXT("the demon raises Eldritch Shield, hits for 40 and rides a next-attack skill"), 25.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		const ACrossguardMinion* Demon = Cast<ACrossguardMinion>(C.FindActor(TEXT("Demon")));
		if (Dummy == nullptr || Demon == nullptr)
		{
			return false;
		}
		C.SetHealth(Dummy, C.MaxHealth(Dummy));
		if (C.IsOnCooldown(Demon, TEXT("eldritch_shield")))
		{
			C.SetNumber(TEXT("Shielded"), 1.0);
		}
		if (C.HasTag(Dummy, CrossguardTags::State_HealBlocked))
		{
			C.SetNumber(TEXT("HealBlocked"), 1.0);
		}
		if (C.IsOnCooldown(Demon, TEXT("shade_blow")))
		{
			C.SetNumber(TEXT("ShadeBlow"), 1.0);
		}
		if (C.IsOnCooldown(Demon, TEXT("mortal_strike")))
		{
			C.SetNumber(TEXT("MortalStrike"), 1.0);
		}
		if (MaxAmount(DamageAmounts(C, Dummy, Demon)) >= C.Number(TEXT("Sword")) - 0.5)
		{
			C.SetNumber(TEXT("Hit40"), 1.0);
		}
		return C.Number(TEXT("Shielded")) > 0.0 && C.Number(TEXT("Hit40")) > 0.0 && C.Number(TEXT("ShadeBlow")) > 0.0 && C.Number(TEXT("MortalStrike")) > 0.0;
	});
	// One rider per swing: Shade Blow's heal block lands on a later hit than Mortal Strike's.
	Scenario.WaitUntil(TEXT("Shade Blow's heal block lands on the dummy"), 12.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			return false;
		}
		C.SetHealth(Dummy, C.MaxHealth(Dummy));
		if (C.HasTag(Dummy, CrossguardTags::State_HealBlocked))
		{
			C.SetNumber(TEXT("HealBlocked"), 1.0);
		}
		return C.Number(TEXT("HealBlocked")) > 0.0;
	});
	Scenario.Step(TEXT("Eldritch Shield, the 40, Shade Blow and Mortal Strike"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = Character(C, TEXT("Dummy"));
		const AActor* Demon = C.FindActor(TEXT("Demon"));
		C.Test.TestTrue(TEXT("The demon raised Eldritch Shield"), C.Number(TEXT("Shielded")) > 0.0);
		C.Test.TestTrue(TEXT("The demon used Shade Blow"), C.Number(TEXT("ShadeBlow")) > 0.0);
		C.Test.TestTrue(TEXT("Shade Blow blocked the dummy's healing"), C.Number(TEXT("HealBlocked")) > 0.0);
		C.Test.TestTrue(TEXT("The demon used Mortal Strike"), C.Number(TEXT("MortalStrike")) > 0.0);
		const TArray<float> Amounts = DamageAmounts(C, Dummy, Demon);
		FString List;
		for (const float Amount : Amounts)
		{
			List += FString::Printf(TEXT("%.1f "), Amount);
		}
		C.Test.AddInfo(FString::Printf(TEXT("Demon hits on the dummy: %s(sword %.1f)"), *List, C.Number(TEXT("Sword"))));
		C.Test.TestTrue(TEXT("A hit did at least the sword's 40 (after the dummy's Armor)"), MaxAmount(Amounts) >= C.Number(TEXT("Sword")) - 0.5);
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
