// Scenario tests: passive skills played from data (UCrossguardDataPassiveAbility) and the on-hit
// passive Splinter of Winter. Framework: CrossguardScenario.h, docs/editor-tests.md. Coverage of
// every skill: docs/skill-coverage.md.
//
// Expected numbers come from Data/Skills/Skills.json and docs/design-decisions.md. Requirement rows
// (Armor Mastery: Plate, The Mithril Knight: Plate in every slot, Spear of Liscor: a Spear) gate the
// skill's own bonuses.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Skills/CrossguardLoadoutComponent.h"

namespace CrossguardScenarioPassiveSkills
{
	using CrossguardScenario::FScenarioContext;

	// --- Data (Data/Skills/Skills.json) ---------------------------------------------------------
	// Prefixed "Passive": names in the game's anonymous namespaces must not be visible twice in a
	// unity build.

	// Armor Mastery: AR x1.10, +5% Movement Speed, needs Plate.
	constexpr float PassiveArmorMasteryResistance = 1.1f;
	constexpr float PassiveArmorMasteryMoveSpeed = 0.05f;
	// The Mithril Knight: +5 STR (and +30% AR, -5% speed), needs Plate in every slot.
	constexpr float PassiveMithrilStrength = 5.0f;
	// Spear of Liscor: +7 STR, +3 AGI, needs a Spear.
	constexpr float PassiveLiscorStrength = 7.0f;
	constexpr float PassiveLiscorAgility = 3.0f;
	// Cor Leonis: per empty armor slot +5% ADB, +5% MDB.
	constexpr float PassiveCorLeonisBonusPerSlot = 0.05f;
	// Splinter of Winter: dagger attacks +5 Magic Damage, -10% speed for 3 s.
	constexpr float PassiveSplinterMagic = 5.0f;
	constexpr float PassiveSplinterSlow = 0.1f;
	constexpr float PassiveSplinterSeconds = 3.0f;

	// Melee targets stand this far ahead (reach 220 cm + 60 cm radius).
	constexpr float PassiveMeleeGap = 150.0f;
	constexpr float PassiveStatTolerance = 0.001f;
	constexpr float PassiveDamageTolerance = 0.05f;
	// Weapons count once drawn (Data: 1 s at 0 Dexterity).
	constexpr float PassiveDrawWait = 1.3f;

	// The five plate pieces (Data/Items/Armor.json; there is no plate cape).
	const TArray<FName>& PassivePlatePieces()
	{
		static const TArray<FName> Pieces = {TEXT("plate_helm"), TEXT("plate_cuirass"), TEXT("plate_gauntlets"), TEXT("plate_greaves"),
			TEXT("plate_sabatons")};
		return Pieces;
	}

	// Data passives come from the Skill Catalog: without one there is nothing to test.
	bool PassiveHasCatalog(FScenarioContext& C, const ACrossguardCharacter* Character)
	{
		const UCrossguardLoadoutComponent* Loadout = Character != nullptr ? Character->GetLoadoutComponent() : nullptr;
		if (Loadout == nullptr || Loadout->SkillCatalog == nullptr)
		{
			C.Skip(TEXT("The character Blueprint's LoadoutComponent has no Skill Catalog: data passives cannot be granted"));
			return false;
		}
		return true;
	}

	// Bundles cost 15 Capacity: the debug progression at the max level (Capacity 20).
	void PassiveAllowFullCapacity(ACrossguardCharacter* Character)
	{
		if (Character != nullptr)
		{
			Character->bUseDebugProgression = true;
			Character->Level = 15;
		}
	}

	// A fresh armor piece in its slot, without the inventory. Skips without an Armor Table.
	bool PassiveWearArmor(FScenarioContext& C, ACrossguardCharacter* Target, FName ArmorId)
	{
		UCrossguardEquipmentComponent* Equipment = Target != nullptr ? Target->GetEquipmentComponent() : nullptr;
		if (Equipment == nullptr)
		{
			C.Fail(TEXT("PassiveWearArmor: no character or equipment component"));
			return false;
		}
		if (Equipment->ArmorTable == nullptr)
		{
			C.Skip(FString::Printf(TEXT("%s's EquipmentComponent has no Armor Table (DT_Armor)"), *Target->GetClass()->GetName()));
			return false;
		}
		ECrossguardArmorSlot Slot = ECrossguardArmorSlot::Head;
		if (!Equipment->GetArmorSlotFor(ArmorId, Slot))
		{
			C.Fail(FString::Printf(TEXT("PassiveWearArmor: %s is not in the Armor Table"), *ArmorId.ToString()));
			return false;
		}
		FCrossguardArmorItem Piece;
		Piece.ArmorId = ArmorId;
		const ECrossguardEquipError Error = Equipment->EquipArmor(Slot, Piece);
		if (Error != ECrossguardEquipError::None)
		{
			C.Fail(FString::Printf(TEXT("PassiveWearArmor %s: %s"), *ArmorId.ToString(),
				*StaticEnum<ECrossguardEquipError>()->GetNameStringByValue(static_cast<int64>(Error))));
			return false;
		}
		return true;
	}

	float PassiveStrength(const FScenarioContext& C, const AActor* Actor)
	{
		return C.Stat(Actor, UCrossguardAttributeSet::GetStrengthAttribute());
	}

	float PassiveAgility(const FScenarioContext& C, const AActor* Actor)
	{
		return C.Stat(Actor, UCrossguardAttributeSet::GetAgilityAttribute());
	}
}

// =====================================================================================================
// Armor Mastery ("Requirement: Plate Armor"): nothing without plate; with a plate piece AR x1.10 and
// +5% Movement Speed. A squire in the same plate without the skill is the baseline.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPassiveArmorMastery, "Crossguard.Scenario.Skills.ArmorMasteryNeedsPlate", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPassiveArmorMastery::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioPassiveSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A knight with Armor Mastery and a squire, no armor"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.SpawnPlayer(TEXT("Knight"), FVector(-1500.0f, 3000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Squire = C.SpawnPlayer(TEXT("Squire"), FVector(-1500.0f, 3400.0f, 0.0f), 0.0f);
		if (!C.Require(Knight != nullptr && Squire != nullptr, TEXT("knight and squire spawned")) || !PassiveHasCatalog(C, Knight))
		{
			return;
		}
		C.Require(C.GrantSkills(Knight, {TEXT("armor_mastery")}), TEXT("Armor Mastery granted"));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("No plate: no bonus; both put on a plate cuirass"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.Get<ACrossguardCharacter>(TEXT("Knight"));
		ACrossguardCharacter* Squire = C.Get<ACrossguardCharacter>(TEXT("Squire"));
		if (Knight == nullptr || Squire == nullptr)
		{
			C.Fail(TEXT("knight or squire gone"));
			return;
		}
		C.Test.TestEqual(TEXT("No plate: Movement Speed as the squire's"), C.Stat(Knight, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			C.Stat(Squire, UCrossguardAttributeSet::GetMoveSpeedAttribute()), PassiveStatTolerance);
		C.Test.TestEqual(TEXT("No plate: Attack Resistance as the squire's"), C.Stat(Knight, UCrossguardAttributeSet::GetAttackResistanceAttribute()),
			C.Stat(Squire, UCrossguardAttributeSet::GetAttackResistanceAttribute()), PassiveStatTolerance);
		if (PassiveWearArmor(C, Knight, TEXT("plate_cuirass")))
		{
			PassiveWearArmor(C, Squire, TEXT("plate_cuirass"));
		}
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Plate: AR x1.10 and +5% Movement Speed over the squire's"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Knight = C.Get<ACrossguardCharacter>(TEXT("Knight"));
		const ACrossguardCharacter* Squire = C.Get<ACrossguardCharacter>(TEXT("Squire"));
		if (Knight == nullptr || Squire == nullptr)
		{
			return;
		}
		const float SquireResistance = C.Stat(Squire, UCrossguardAttributeSet::GetAttackResistanceAttribute());
		C.Test.AddInfo(FString::Printf(TEXT("AR: knight %.4f, squire %.4f"), C.Stat(Knight, UCrossguardAttributeSet::GetAttackResistanceAttribute()),
			SquireResistance));
		C.Test.TestTrue(TEXT("The plate gives the squire some Attack Resistance"), SquireResistance > 0.0f);
		C.Test.TestEqual(TEXT("Plate: the knight's AR is the squire's x1.10"), C.Stat(Knight, UCrossguardAttributeSet::GetAttackResistanceAttribute()),
			SquireResistance * PassiveArmorMasteryResistance, PassiveStatTolerance);
		C.Test.TestEqual(TEXT("Plate: the knight is 5% faster"), C.Stat(Knight, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			C.Stat(Squire, UCrossguardAttributeSet::GetMoveSpeedAttribute()) + PassiveArmorMasteryMoveSpeed, PassiveStatTolerance);
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// The Mithril Knight ("Requires: Plate Armor in every slot"): its own +5 STR only with plate on head,
// chest, hands, legs and feet (the cape slot has no plate). Taking a piece off turns it off again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPassiveMithrilKnight, "Crossguard.Scenario.Skills.MithrilKnightNeedsFullPlate", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPassiveMithrilKnight::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioPassiveSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A knight in four plate pieces learns The Mithril Knight"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.SpawnPlayer(TEXT("Knight"), FVector(-1500.0f, 4000.0f, 0.0f), 0.0f);
		if (!C.Require(Knight != nullptr, TEXT("knight spawned")) || !PassiveHasCatalog(C, Knight))
		{
			return;
		}
		PassiveAllowFullCapacity(Knight);
		for (int32 Index = 0; Index < PassivePlatePieces().Num() - 1; ++Index)
		{
			if (!PassiveWearArmor(C, Knight, PassivePlatePieces()[Index]))
			{
				return;
			}
		}
		C.SetNumber(TEXT("Strength"), PassiveStrength(C, Knight));
		C.Require(C.GrantSkills(Knight, {TEXT("the_mithril_knight")}), TEXT("The Mithril Knight granted"));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Four pieces: no Strength; put on the sabatons"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.Get<ACrossguardCharacter>(TEXT("Knight"));
		if (Knight == nullptr)
		{
			C.Fail(TEXT("knight gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Not all plate: The Mithril Knight's Strength is off"), PassiveStrength(C, Knight),
			static_cast<float>(C.Number(TEXT("Strength"))), PassiveStatTolerance);
		PassiveWearArmor(C, Knight, PassivePlatePieces().Last());
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Plate in every slot: +5 Strength; take the helm off"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.Get<ACrossguardCharacter>(TEXT("Knight"));
		UCrossguardEquipmentComponent* Equipment = Knight != nullptr ? Knight->GetEquipmentComponent() : nullptr;
		if (Equipment == nullptr)
		{
			C.Fail(TEXT("knight gone"));
			return;
		}
		C.Test.TestTrue(TEXT("The knight wears plate in every slot"), Equipment->SatisfiesArmorRequirement(ECrossguardArmorRequirement::PlateAllSlots));
		C.Test.TestEqual(TEXT("Plate in every slot: Strength +5"), PassiveStrength(C, Knight),
			static_cast<float>(C.Number(TEXT("Strength"))) + PassiveMithrilStrength, PassiveStatTolerance);
		C.Test.TestTrue(TEXT("The helm comes off"),
			Equipment->EquipArmor(ECrossguardArmorSlot::Head, FCrossguardArmorItem()) == ECrossguardEquipError::None);
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("No helm: the Strength is gone again"), [](FScenarioContext& C)
	{
		if (const ACrossguardCharacter* Knight = C.Get<ACrossguardCharacter>(TEXT("Knight")))
		{
			C.Test.TestEqual(TEXT("Helm off: The Mithril Knight's Strength is off"), PassiveStrength(C, Knight),
				static_cast<float>(C.Number(TEXT("Strength"))), PassiveStatTolerance);
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Spear of Liscor ("Requirement: Spear"): its own +7 STR and +3 AGI only with a spear in hand.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPassiveSpearOfLiscor, "Crossguard.Scenario.Skills.SpearOfLiscorNeedsSpear", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPassiveSpearOfLiscor::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioPassiveSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a sword learns Spear of Liscor"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Lancer"), FVector(-1500.0f, 4600.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr, TEXT("lancer spawned")) || !PassiveHasCatalog(C, Player) || !C.Equip(Player, TEXT("sword_one_handed")))
		{
			return;
		}
		PassiveAllowFullCapacity(Player);
		C.SetNumber(TEXT("Strength"), PassiveStrength(C, Player));
		C.SetNumber(TEXT("Agility"), PassiveAgility(C, Player));
		C.Require(C.GrantSkills(Player, {TEXT("spear_of_liscor")}), TEXT("Spear of Liscor granted"));
	});

	Scenario.Wait(PassiveDrawWait);

	Scenario.Step(TEXT("Sword in hand: no bonus; take a spear"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lancer"));
		if (Player == nullptr)
		{
			C.Fail(TEXT("lancer gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Sword: Spear of Liscor's Strength is off"), PassiveStrength(C, Player), static_cast<float>(C.Number(TEXT("Strength"))),
			PassiveStatTolerance);
		C.Test.TestEqual(TEXT("Sword: Spear of Liscor's Agility is off"), PassiveAgility(C, Player), static_cast<float>(C.Number(TEXT("Agility"))),
			PassiveStatTolerance);
		C.Equip(Player, TEXT("spear_two_handed"));
	});

	Scenario.WaitUntil(TEXT("Spear in hand: +7 Strength"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lancer"));
		return Player == nullptr
			|| FMath::IsNearlyEqual(PassiveStrength(C, Player), static_cast<float>(C.Number(TEXT("Strength"))) + PassiveLiscorStrength, PassiveStatTolerance);
	});

	Scenario.Step(TEXT("Spear: +3 Agility too; back to the sword"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lancer"));
		if (Player == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("Spear: Agility +3"), PassiveAgility(C, Player), static_cast<float>(C.Number(TEXT("Agility"))) + PassiveLiscorAgility,
			PassiveStatTolerance);
		C.Equip(Player, TEXT("sword_one_handed"));
	});

	Scenario.Wait(PassiveDrawWait);

	Scenario.Step(TEXT("Sword again: the bonus is off"), [](FScenarioContext& C)
	{
		if (const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lancer")))
		{
			C.Test.TestEqual(TEXT("Sword again: Strength back to its base"), PassiveStrength(C, Player), static_cast<float>(C.Number(TEXT("Strength"))),
				PassiveStatTolerance);
		}
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Cor Leonis: +5% Attack Bonus and +5% Magic Bonus per empty armor slot; a piece put on takes one off.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPassiveCorLeonis, "Crossguard.Scenario.Skills.CorLeonisEmptySlots", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPassiveCorLeonis::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioPassiveSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player without armor learns Cor Leonis"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Lion"), FVector(-1500.0f, 5200.0f, 0.0f), 0.0f);
		if (!C.Require(Player != nullptr && Player->GetEquipmentComponent() != nullptr, TEXT("lion spawned")) || !PassiveHasCatalog(C, Player))
		{
			return;
		}
		C.SetNumber(TEXT("ADB"), C.Stat(Player, UCrossguardAttributeSet::GetAttackDamageBonusAttribute()));
		C.SetNumber(TEXT("MDB"), C.Stat(Player, UCrossguardAttributeSet::GetMagicDamageBonusAttribute()));
		C.Require(C.GrantSkills(Player, {TEXT("cor_leonis")}), TEXT("Cor Leonis granted"));
	});

	Scenario.Wait(0.3f);

	const auto Check = [](FScenarioContext& C, const TCHAR* What)
	{
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lion"));
		const UCrossguardEquipmentComponent* Equipment = Player != nullptr ? Player->GetEquipmentComponent() : nullptr;
		if (Equipment == nullptr)
		{
			C.Fail(TEXT("lion gone"));
			return;
		}
		const float Empty = static_cast<float>(Equipment->GetEmptyArmorSlotCount());
		C.Test.AddInfo(FString::Printf(TEXT("%s: %d empty armor slots"), What, Equipment->GetEmptyArmorSlotCount()));
		C.Test.TestEqual(*FString::Printf(TEXT("%s: Attack Bonus +5%% per empty slot"), What),
			C.Stat(Player, UCrossguardAttributeSet::GetAttackDamageBonusAttribute()),
			static_cast<float>(C.Number(TEXT("ADB"))) + PassiveCorLeonisBonusPerSlot * Empty, PassiveStatTolerance);
		C.Test.TestEqual(*FString::Printf(TEXT("%s: Magic Bonus +5%% per empty slot"), What),
			C.Stat(Player, UCrossguardAttributeSet::GetMagicDamageBonusAttribute()),
			static_cast<float>(C.Number(TEXT("MDB"))) + PassiveCorLeonisBonusPerSlot * Empty, PassiveStatTolerance);
	};

	Scenario.Step(TEXT("No armor: every slot counts; put on a helm"), [Check](FScenarioContext& C)
	{
		Check(C, TEXT("No armor"));
		PassiveWearArmor(C, C.Get<ACrossguardCharacter>(TEXT("Lion")), TEXT("plate_helm"));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("A helm on: one slot fewer"), [Check](FScenarioContext& C)
	{
		Check(C, TEXT("With a helm"));
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Splinter of Winter: a dagger attack deals +5 Magic Damage and slows the target 10% for 3 s.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPassiveSplinterOfWinter, "Crossguard.Scenario.Skills.SplinterOfWinter", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPassiveSplinterOfWinter::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioPassiveSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with a dagger and Splinter of Winter, a dummy in reach"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Rogue"), FVector(-1500.0f, 5800.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(-1500.0f + PassiveMeleeGap, 5800.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("rogue and dummy spawned")) || !C.Equip(Player, TEXT("dagger_one_handed")))
		{
			return;
		}
		// Native (Passive.SplinterOfWinter); Dagger Mastery comes with it from data.
		C.Require(C.GrantSkills(Player, {TEXT("splinter_of_winter")}), TEXT("Splinter of Winter granted"));
	});

	Scenario.Wait(PassiveDrawWait);

	Scenario.WaitUntil(TEXT("A dagger hit with the splinter"), 3.0f, [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Rogue"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			C.Fail(TEXT("rogue or dummy gone"));
			return true;
		}
		C.Aim(Player, Dummy);
		const float Before = C.Health(Dummy);
		const float Speed = C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute());
		const float Expected = FMath::Min(C.ExpectedDamage(Player, Dummy, C.WeaponValue(Player), PassiveSplinterMagic), Before);
		if (!C.BasicAttack(Player))
		{
			return false;
		}
		C.SetNumber(TEXT("Taken"), Before - C.Health(Dummy));
		C.SetNumber(TEXT("Expected"), Expected);
		C.SetNumber(TEXT("Speed"), Speed);
		return true;
	});

	Scenario.Step(TEXT("+5 Magic Damage on the hit and a 10% slow"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Rogue"));
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("The rogue has Passive.SplinterOfWinter"), C.HasTag(Player, CrossguardTags::Passive_SplinterOfWinter));
		C.Test.AddInfo(FString::Printf(TEXT("Splinter hit: took %.2f (expected %.2f)"), C.Number(TEXT("Taken")), C.Number(TEXT("Expected"))));
		C.Test.TestEqual(TEXT("The dagger hit carries +5 Magic Damage"), static_cast<float>(C.Number(TEXT("Taken"))),
			static_cast<float>(C.Number(TEXT("Expected"))), PassiveDamageTolerance);
		C.Test.TestTrue(TEXT("The dummy is Slowed"), C.HasTag(Dummy, CrossguardTags::State_Slowed));
		C.Test.TestEqual(TEXT("The dummy's Movement Speed is 10% lower"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			static_cast<float>(C.Number(TEXT("Speed"))) - PassiveSplinterSlow, PassiveStatTolerance);
	});

	Scenario.Wait(PassiveSplinterSeconds + 0.5f);

	Scenario.Step(TEXT("3 s later the slow is gone"), [](FScenarioContext& C)
	{
		if (const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy")))
		{
			C.Test.TestFalse(TEXT("The slow ran out"), C.HasTag(Dummy, CrossguardTags::State_Slowed));
			C.Test.TestEqual(TEXT("Movement Speed is back"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
				static_cast<float>(C.Number(TEXT("Speed"))), PassiveStatTolerance);
		}
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
