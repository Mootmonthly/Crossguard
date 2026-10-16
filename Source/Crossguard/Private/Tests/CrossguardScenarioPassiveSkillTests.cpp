// Scenario tests: passive skills played from data (UCrossguardDataPassiveAbility) and the on-hit
// passive Splinter of Winter. Framework: CrossguardScenario.h, docs/editor-tests.md. Coverage of
// every skill: docs/skill-coverage.md.
//
// Expected numbers come from Data/Skills/Skills.json and docs/design-decisions.md. Requirement rows
// (Armor Mastery: Plate, The Mithril Knight: Plate in every slot, Spear of Liscor: a Spear) gate the
// skill's own bonuses and, for a bundle, the skills it includes. A thrown throwing knife is a dagger
// attack (Splinter of Winter, Gouge).

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardItemTypes.h"
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
	// Included skills, off with their bundle's requirement: Antimagic Garb +15% MR, Multiscale +15%
	// AR and MR.
	constexpr float PassiveGarbResistance = 0.15f;
	constexpr float PassiveMultiscaleResistance = 0.15f;
	// Cor Leonis: per empty armor slot +5% ADB, +5% MDB.
	constexpr float PassiveCorLeonisBonusPerSlot = 0.05f;
	// Splinter of Winter: dagger attacks +5 Magic Damage, -10% speed for 3 s.
	constexpr float PassiveSplinterMagic = 5.0f;
	constexpr float PassiveSplinterSlow = 0.1f;
	constexpr float PassiveSplinterSeconds = 3.0f;

	// Thrown knives: Data/Items (throwing_knife counts as a Dagger), a dummy 4 m away; Gouge 5 Attack
	// Damage per second and -5% speed for 5 s.
	const FName PassiveThrowingKnife(TEXT("throwing_knife"));
	constexpr float PassiveThrowSpeed = 2000.0f;
	constexpr float PassiveThrowGap = 400.0f;
	// A player's view is read through its camera manager, updated once a frame: wait after aiming.
	constexpr float PassiveViewSettle = 0.3f;
	constexpr float PassiveGougeDamagePerSecond = 5.0f;
	constexpr float PassiveGougeSlow = 0.05f;

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

	// Throwing knives in hand (key 3), as a player readies them. Fails the scenario if they do not fit.
	bool PassiveReadyKnives(FScenarioContext& C, ACrossguardCharacter* Player, int32 Count)
	{
		UCrossguardInventoryComponent* Inventory = Player != nullptr ? Player->GetInventoryComponent() : nullptr;
		if (!C.Require(Inventory != nullptr && C.GiveItem(Player, PassiveThrowingKnife, Count) == 0, TEXT("throwing knives fit in the inventory")))
		{
			return false;
		}
		int32 StackId = 0;
		for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
		{
			if (Item.Category == ECrossguardItemCategory::Throwable && Item.ItemId == PassiveThrowingKnife)
			{
				StackId = Item.Id;
				break;
			}
		}
		if (!C.Require(StackId != 0, TEXT("the knives are in the inventory")))
		{
			return false;
		}
		Inventory->RequestAssignHotbarSlot(0, StackId);
		Inventory->RequestUseHotbarSlot(0);
		return C.Require(Inventory->GetReadiedItemId() == StackId, TEXT("key 3 readies the knives"));
	}

	// Aims at Target, raised to make up for the throw's drop on the way (ACrossguardThrownProjectile:
	// 2000 cm/s, full gravity).
	void PassiveAimThrow(FScenarioContext& C, ACrossguardCharacter* Thrower, const AActor* Target)
	{
		if (Thrower == nullptr || Target == nullptr)
		{
			return;
		}
		const double Seconds = FVector::Dist2D(Thrower->GetPawnViewLocation(), Target->GetActorLocation()) / PassiveThrowSpeed;
		const UWorld* World = C.World();
		const double Gravity = World != nullptr ? FMath::Abs(World->GetGravityZ()) : 980.0;
		C.AimAt(Thrower, Target->GetActorLocation() + FVector(0.0, 0.0, 0.5 * Gravity * Seconds * Seconds));
	}

	// Left click with the knives in hand: one is thrown.
	void PassiveThrow(ACrossguardCharacter* Thrower)
	{
		if (UCrossguardInventoryComponent* Inventory = Thrower != nullptr ? Thrower->GetInventoryComponent() : nullptr)
		{
			Inventory->RequestStartUse();
			Inventory->RequestStopUse();
		}
	}

	// A common throwing knife's Attack Damage (item data); -1 with a failure without the Item Table.
	float PassiveKnifeDamage(FScenarioContext& C, const ACrossguardCharacter* Thrower)
	{
		const UCrossguardInventoryComponent* Inventory = Thrower != nullptr ? Thrower->GetInventoryComponent() : nullptr;
		const FCrossguardItemRow* Row = Inventory != nullptr && Inventory->ItemTable != nullptr
			? Inventory->ItemTable->FindRow<FCrossguardItemRow>(PassiveThrowingKnife, TEXT("Scenario"), false)
			: nullptr;
		if (Row == nullptr)
		{
			C.Fail(TEXT("No throwing_knife row in the Item Table (InventoryComponent Item Table: DT_Items)"));
			return -1.0f;
		}
		return Row->ThrowDamage + Row->ThrowDamagePerRarity * UCrossguardItemLibrary::GetRarityLevel(ECrossguardRarity::Common);
	}

	float PassiveMagicResistance(const FScenarioContext& C, const AActor* Actor)
	{
		return C.Stat(Actor, UCrossguardAttributeSet::GetMagicResistanceAttribute());
	}

	// The Mithril Knight's included skills against a squire in the same armor: Antimagic Garb and
	// Multiscale (+15% MR each, State.Multiscale) and Immutable's always-on part (Passive.Immutable).
	void PassiveCheckMithrilComponents(FScenarioContext& C, const AActor* Knight, const AActor* Squire, bool bOn, const TCHAR* What)
	{
		const float Expected = PassiveMagicResistance(C, Squire) + (bOn ? PassiveGarbResistance + PassiveMultiscaleResistance : 0.0f);
		C.Test.AddInfo(FString::Printf(TEXT("%s: MR knight %.4f, squire %.4f"), What, PassiveMagicResistance(C, Knight), PassiveMagicResistance(C, Squire)));
		C.Test.TestEqual(*FString::Printf(TEXT("%s: Antimagic Garb and Multiscale %s"), What, bOn ? TEXT("give +0.30 MR") : TEXT("give no MR")),
			PassiveMagicResistance(C, Knight), Expected, PassiveStatTolerance);
		C.Test.TestTrue(*FString::Printf(TEXT("%s: State.Multiscale %s"), What, bOn ? TEXT("on") : TEXT("off")),
			C.HasTag(Knight, CrossguardTags::State_Multiscale) == bOn);
		C.Test.TestTrue(*FString::Printf(TEXT("%s: Passive.Immutable %s"), What, bOn ? TEXT("on") : TEXT("off")),
			C.HasTag(Knight, CrossguardTags::Passive_Immutable) == bOn);
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
// chest, hands, legs and feet (the cape slot has no plate). The requirement also switches off what it
// includes (owner ruling): Antimagic Garb (+15% MR), Multiscale (+15% MR, State.Multiscale) and
// Immutable (Passive.Immutable, activation). A squire in the same plate, not in a party, is the
// baseline. Taking a piece off turns it all off again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPassiveMithrilKnight, "Crossguard.Scenario.Skills.MithrilKnightNeedsFullPlate", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPassiveMithrilKnight::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioPassiveSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A knight and a squire in four plate pieces; the knight learns The Mithril Knight"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.SpawnPlayer(TEXT("Knight"), FVector(-1500.0f, 4000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Squire = C.SpawnPlayer(TEXT("Squire"), FVector(-1500.0f, 4300.0f, 0.0f), 0.0f);
		if (!C.Require(Knight != nullptr && Squire != nullptr, TEXT("knight and squire spawned")) || !PassiveHasCatalog(C, Knight))
		{
			return;
		}
		PassiveAllowFullCapacity(Knight);
		for (int32 Index = 0; Index < PassivePlatePieces().Num() - 1; ++Index)
		{
			if (!PassiveWearArmor(C, Knight, PassivePlatePieces()[Index]) || !PassiveWearArmor(C, Squire, PassivePlatePieces()[Index]))
			{
				return;
			}
		}
		C.SetNumber(TEXT("Strength"), PassiveStrength(C, Knight));
		C.Require(C.GrantSkills(Knight, {TEXT("the_mithril_knight")}), TEXT("The Mithril Knight granted"));
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Four pieces: the bundle and what it includes are off; both put on the sabatons"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.Get<ACrossguardCharacter>(TEXT("Knight"));
		ACrossguardCharacter* Squire = C.Get<ACrossguardCharacter>(TEXT("Squire"));
		if (Knight == nullptr || Squire == nullptr)
		{
			C.Fail(TEXT("knight or squire gone"));
			return;
		}
		C.Test.TestEqual(TEXT("Not all plate: The Mithril Knight's Strength is off"), PassiveStrength(C, Knight),
			static_cast<float>(C.Number(TEXT("Strength"))), PassiveStatTolerance);
		PassiveCheckMithrilComponents(C, Knight, Squire, false, TEXT("Not all plate"));
		C.Test.TestFalse(TEXT("Not all plate: Immutable cannot be activated"), C.ActivateSkill(Knight, TEXT("immutable")));
		if (PassiveWearArmor(C, Knight, PassivePlatePieces().Last()))
		{
			PassiveWearArmor(C, Squire, PassivePlatePieces().Last());
		}
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("Plate in every slot: +5 Strength and the included skills on; both take the helm off"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Knight = C.Get<ACrossguardCharacter>(TEXT("Knight"));
		ACrossguardCharacter* Squire = C.Get<ACrossguardCharacter>(TEXT("Squire"));
		UCrossguardEquipmentComponent* Equipment = Knight != nullptr ? Knight->GetEquipmentComponent() : nullptr;
		UCrossguardEquipmentComponent* SquireEquipment = Squire != nullptr ? Squire->GetEquipmentComponent() : nullptr;
		if (Equipment == nullptr || SquireEquipment == nullptr)
		{
			C.Fail(TEXT("knight or squire gone"));
			return;
		}
		C.Test.TestTrue(TEXT("The knight wears plate in every slot"), Equipment->SatisfiesArmorRequirement(ECrossguardArmorRequirement::PlateAllSlots));
		C.Test.TestEqual(TEXT("Plate in every slot: Strength +5"), PassiveStrength(C, Knight),
			static_cast<float>(C.Number(TEXT("Strength"))) + PassiveMithrilStrength, PassiveStatTolerance);
		PassiveCheckMithrilComponents(C, Knight, Squire, true, TEXT("Plate in every slot"));
		C.Test.TestTrue(TEXT("Plate in every slot: Immutable activates"), C.ActivateSkill(Knight, TEXT("immutable")));
		C.Test.TestTrue(TEXT("The knight's helm comes off"),
			Equipment->EquipArmor(ECrossguardArmorSlot::Head, FCrossguardArmorItem()) == ECrossguardEquipError::None);
		C.Test.TestTrue(TEXT("The squire's helm comes off"),
			SquireEquipment->EquipArmor(ECrossguardArmorSlot::Head, FCrossguardArmorItem()) == ECrossguardEquipError::None);
	});

	Scenario.Wait(0.3f);

	Scenario.Step(TEXT("No helm: the Strength and the included skills are off again"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Knight = C.Get<ACrossguardCharacter>(TEXT("Knight"));
		const ACrossguardCharacter* Squire = C.Get<ACrossguardCharacter>(TEXT("Squire"));
		if (Knight == nullptr || Squire == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("Helm off: The Mithril Knight's Strength is off"), PassiveStrength(C, Knight),
			static_cast<float>(C.Number(TEXT("Strength"))), PassiveStatTolerance);
		PassiveCheckMithrilComponents(C, Knight, Squire, false, TEXT("Helm off"));
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// Spear of Liscor ("Requirement: Spear"): its own +7 STR and +3 AGI only with a spear in hand, and the
// included Antimagic Garb (+15% MR) too (owner ruling: a bundle's requirement switches off what it
// includes).
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
		C.SetNumber(TEXT("MR"), PassiveMagicResistance(C, Player));
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
		C.Test.TestEqual(TEXT("Sword: the included Antimagic Garb is off too"), PassiveMagicResistance(C, Player),
			static_cast<float>(C.Number(TEXT("MR"))), PassiveStatTolerance);
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
		C.Test.TestEqual(TEXT("Spear: the included Antimagic Garb gives +15% MR"), PassiveMagicResistance(C, Player),
			static_cast<float>(C.Number(TEXT("MR"))) + PassiveGarbResistance, PassiveStatTolerance);
		C.Equip(Player, TEXT("sword_one_handed"));
	});

	Scenario.Wait(PassiveDrawWait);

	Scenario.Step(TEXT("Sword again: the bonus is off"), [](FScenarioContext& C)
	{
		if (const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Lancer")))
		{
			C.Test.TestEqual(TEXT("Sword again: Strength back to its base"), PassiveStrength(C, Player), static_cast<float>(C.Number(TEXT("Strength"))),
				PassiveStatTolerance);
			C.Test.TestEqual(TEXT("Sword again: Antimagic Garb off"), PassiveMagicResistance(C, Player), static_cast<float>(C.Number(TEXT("MR"))),
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

// =====================================================================================================
// A thrown throwing knife is a dagger attack (owner ruling): Splinter of Winter's +5 Magic Damage and
// 10% slow ride on it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPassiveThrownKnifeSplinter, "Crossguard.Scenario.Skills.ThrownKnifeSplinterOfWinter",
	CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPassiveThrownKnifeSplinter::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioPassiveSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with Splinter of Winter readies throwing knives, a dummy 4 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Thrower"), FVector(-1500.0f, 6400.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(-1500.0f + PassiveThrowGap, 6400.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("thrower and dummy spawned"))
			|| !C.Require(C.GrantSkills(Player, {TEXT("splinter_of_winter")}), TEXT("Splinter of Winter granted")))
		{
			return;
		}
		if (PassiveReadyKnives(C, Player, 2))
		{
			PassiveAimThrow(C, Player, Dummy);
		}
	});

	Scenario.Wait(PassiveViewSettle);

	Scenario.Step(TEXT("Throw one knife"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Thrower"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		const float Base = PassiveKnifeDamage(C, Player);
		if (Player == nullptr || Dummy == nullptr || Base < 0.0f)
		{
			C.Fail(TEXT("thrower or dummy gone"));
			return;
		}
		PassiveAimThrow(C, Player, Dummy);
		const float Before = C.Health(Dummy);
		C.SetNumber(TEXT("Before"), Before);
		C.SetNumber(TEXT("Speed"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()));
		C.SetNumber(TEXT("Expected"), FMath::Min(C.ExpectedDamage(Player, Dummy, Base, PassiveSplinterMagic), Before));
		PassiveThrow(Player);
	});

	Scenario.WaitUntil(TEXT("the knife hits the dummy"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		return Dummy == nullptr || C.Health(Dummy) < static_cast<float>(C.Number(TEXT("Before")));
	});

	Scenario.Step(TEXT("The knife hit carries +5 Magic Damage and a 10% slow"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			C.Fail(TEXT("dummy gone"));
			return;
		}
		const float Taken = static_cast<float>(C.Number(TEXT("Before"))) - C.Health(Dummy);
		C.Test.AddInfo(FString::Printf(TEXT("Thrown knife with the splinter: took %.2f (expected %.2f)"), Taken, C.Number(TEXT("Expected"))));
		C.Test.TestEqual(TEXT("The thrown knife carries +5 Magic Damage"), Taken, static_cast<float>(C.Number(TEXT("Expected"))),
			PassiveDamageTolerance);
		C.Test.TestTrue(TEXT("The dummy is Slowed"), C.HasTag(Dummy, CrossguardTags::State_Slowed));
		C.Test.TestEqual(TEXT("The dummy's Movement Speed is 10% lower"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			static_cast<float>(C.Number(TEXT("Speed"))) - PassiveSplinterSlow, PassiveStatTolerance);
	});

	Scenario.Run();
	return true;
}

// =====================================================================================================
// A thrown throwing knife uses up a readied Gouge (owner ruling: a dagger attack): 5 Attack Damage
// per second and -5% speed for 5 s on the dummy it hits.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPassiveThrownKnifeGouge, "Crossguard.Scenario.Skills.ThrownKnifeGouge", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPassiveThrownKnifeGouge::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioPassiveSkills;

	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player with Gouge readies throwing knives, a dummy 4 m away"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Thrower"), FVector(-1500.0f, 7000.0f, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(-1500.0f + PassiveThrowGap, 7000.0f, 0.0f), 180.0f);
		if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("thrower and dummy spawned"))
			|| !C.Require(C.GrantSkills(Player, {TEXT("gouge")}), TEXT("Gouge granted")))
		{
			return;
		}
		if (PassiveReadyKnives(C, Player, 2))
		{
			PassiveAimThrow(C, Player, Dummy);
		}
	});

	Scenario.Wait(PassiveViewSettle);

	Scenario.Step(TEXT("Gouge (a knife in hand counts as a dagger), then throw"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Thrower"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		const float Base = PassiveKnifeDamage(C, Player);
		if (Player == nullptr || Dummy == nullptr || Base < 0.0f)
		{
			C.Fail(TEXT("thrower or dummy gone"));
			return;
		}
		if (!C.Require(C.ActivateSkill(Player, TEXT("gouge")), TEXT("Gouge activates with a throwing knife in hand")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("State.NextAttack.Gouge is on"), C.HasTag(Player, CrossguardTags::State_NextAttack_Gouge));
		PassiveAimThrow(C, Player, Dummy);
		const float Before = C.Health(Dummy);
		C.SetNumber(TEXT("Before"), Before);
		C.SetNumber(TEXT("Speed"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()));
		C.SetNumber(TEXT("Hit"), FMath::Min(C.ExpectedDamage(Player, Dummy, Base), Before));
		C.SetNumber(TEXT("Tick"), C.ExpectedDamage(Player, Dummy, PassiveGougeDamagePerSecond));
		PassiveThrow(Player);
		C.Test.TestFalse(TEXT("The throw uses Gouge up"), C.HasTag(Player, CrossguardTags::State_NextAttack_Gouge));
	});

	Scenario.WaitUntil(TEXT("the knife hits the dummy"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		return Dummy == nullptr || C.Health(Dummy) < static_cast<float>(C.Number(TEXT("Before")));
	});

	Scenario.Step(TEXT("A plain knife hit, the dummy slowed 5%"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			C.Fail(TEXT("dummy gone"));
			return;
		}
		const float Taken = static_cast<float>(C.Number(TEXT("Before"))) - C.Health(Dummy);
		C.Test.TestEqual(TEXT("The gouging knife itself hits for its own damage"), Taken, static_cast<float>(C.Number(TEXT("Hit"))),
			PassiveDamageTolerance);
		C.Test.TestTrue(TEXT("The dummy is Slowed"), C.HasTag(Dummy, CrossguardTags::State_Slowed));
		C.Test.TestEqual(TEXT("The dummy's Movement Speed is 5% lower"), C.Stat(Dummy, UCrossguardAttributeSet::GetMoveSpeedAttribute()),
			static_cast<float>(C.Number(TEXT("Speed"))) - PassiveGougeSlow, PassiveStatTolerance);
		C.SetNumber(TEXT("AfterHit"), C.Health(Dummy));
	});

	// Ticks every second, none on application.
	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("1.5 s later: one Gouge tick"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Dummy == nullptr)
		{
			C.Fail(TEXT("dummy gone"));
			return;
		}
		const float AfterHit = static_cast<float>(C.Number(TEXT("AfterHit")));
		const float Tick = FMath::Min(static_cast<float>(C.Number(TEXT("Tick"))), AfterHit);
		C.Test.AddInfo(FString::Printf(TEXT("Gouge after 1.5 s: %.2f taken (one tick %.2f)"), AfterHit - C.Health(Dummy), Tick));
		C.Test.TestEqual(TEXT("One Gouge tick after 1.5 s"), AfterHit - C.Health(Dummy), Tick, PassiveDamageTolerance);
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
