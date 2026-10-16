// Unit tests for CrossguardRules. Minimal harness: no exceptions, no dependencies.

#include "CrossguardRules/Combat.h"
#include "CrossguardRules/Economy.h"
#include "CrossguardRules/Experience.h"
#include "CrossguardRules/Gathering.h"
#include "CrossguardRules/Inventory.h"
#include "CrossguardRules/Portal.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/Json.h"
#include "CrossguardRules/Loot.h"
#include "CrossguardRules/Loadout.h"
#include "CrossguardRules/Party.h"
#include "CrossguardRules/Progression.h"
#include "CrossguardRules/Save.h"
#include "CrossguardRules/Stats.h"
#include "CrossguardRules/Teams.h"
#include "CrossguardRules/Guard.h"
#include "CrossguardRules/Trade.h"

#include <climits>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Crossguard;

namespace
{
	int GFailures = 0;
	int GChecks = 0;

	void Check(bool bCondition, const char* Expression, const char* File, int Line)
	{
		++GChecks;
		if (!bCondition)
		{
			++GFailures;
			std::printf("FAIL %s:%d: %s\n", File, Line, Expression);
		}
	}

	void CheckNear(double Actual, double Expected, const char* Expression, const char* File, int Line)
	{
		++GChecks;
		if (std::fabs(Actual - Expected) > 1e-9)
		{
			++GFailures;
			std::printf("FAIL %s:%d: %s = %.12g, expected %.12g\n", File, Line, Expression, Actual, Expected);
		}
	}
}

#define CHECK(Condition) Check((Condition), #Condition, __FILE__, __LINE__)
#define CHECK_NEAR(Actual, Expected) CheckNear((Actual), (Expected), #Actual, __FILE__, __LINE__)

static void TestProgression()
{
	const FProgressionConfig Config;

	CHECK(AttributePointsAtLevel(Config, 1) == 30);
	CHECK(AttributePointsAtLevel(Config, 2) == 33);
	CHECK(AttributePointsAtLevel(Config, 15) == 72);
	CHECK(SkillCapacityAtLevel(Config, 1) == 6);
	CHECK(SkillCapacityAtLevel(Config, 15) == 20);

	// Out-of-range levels clamp.
	CHECK(AttributePointsAtLevel(Config, 0) == 30);
	CHECK(SkillCapacityAtLevel(Config, 99) == 20);

	// Max level rises during a season.
	FProgressionConfig LateSeason;
	LateSeason.MaxLevel = 20;
	CHECK(SkillCapacityAtLevel(LateSeason, 20) == 25);

	CHECK(ValidateAllocation(Config, 1, {10, 10, 10}) == EAllocationError::None);
	CHECK(ValidateAllocation(Config, 1, {30, 0, 0}) == EAllocationError::None);
	CHECK(ValidateAllocation(Config, 1, {5, 5, 5}) == EAllocationError::None);
	CHECK(ValidateAllocation(Config, 1, {31, 0, 0}) == EAllocationError::TooManyPoints);
	CHECK(ValidateAllocation(Config, 2, {31, 1, 1}) == EAllocationError::None);
	CHECK(ValidateAllocation(Config, 1, {-1, 20, 11}) == EAllocationError::NegativeAttribute);
	// Anti-cheat: a total that would overflow int is still too many points.
	CHECK(ValidateAllocation(Config, 1, {INT_MAX, INT_MAX, 2}) == EAllocationError::TooManyPoints);

	CHECK(!RequiresRespec({10, 10, 10}, {10, 10, 10}));
	CHECK(!RequiresRespec({10, 10, 10}, {13, 10, 10}));
	CHECK(RequiresRespec({10, 10, 10}, {12, 9, 12}));
}

static void TestDerivedStats()
{
	const FScalingConfig Config;

	// Every baseline holds at 10/10/10.
	const FDerivedStats Base = DeriveStats({10, 10, 10}, Config);
	CHECK_NEAR(Base.MaxHp, 100.0);
	CHECK_NEAR(Base.Adb, 0.0);
	CHECK_NEAR(Base.Ar, 0.0);
	CHECK_NEAR(Base.MoveSpeed, 1.0);
	CHECK_NEAR(Base.ActionSpeed, 0.0);
	CHECK_NEAR(Base.Mdb, 0.0);
	CHECK_NEAR(Base.Mr, 0.0);

	// Below the reference, bonuses go negative.
	const FDerivedStats Low = DeriveStats({0, 30, 0}, Config);
	CHECK_NEAR(Low.MaxHp, 50.0);
	CHECK_NEAR(Low.Adb, -0.20);
	CHECK_NEAR(Low.Mdb, -0.20);
	CHECK_NEAR(Low.ArtCdr, 0.20);
	CHECK_NEAR(Low.MoveSpeed, 1.10);

	CHECK_NEAR(CategoryCdr(Low, ESkillCategory::Art), 0.20);
	CHECK_NEAR(CategoryCdr(Low, ESkillCategory::Active), -0.10);
	CHECK_NEAR(CategoryCdr(Low, ESkillCategory::Spell), -0.10);
	CHECK_NEAR(CategoryCdr(Low, ESkillCategory::Passive), 0.0);

	// Dragonification: +50% Max HP.
	CHECK_NEAR(ApplyMaxHpBonus(100.0, 0.5), 150.0);
	CHECK_NEAR(ApplyMaxHpBonus(100.0, -2.0), 1.0);
}

static void TestDamage()
{
	// Plain weapon hit, no modifiers.
	CHECK_NEAR(CalculateDamage({30.0, 0.0, 0.0, 1.0, 0.0, 0.0}), 30.0);

	// Fireball 50 (0.5) with 30% MDB: only 15% applies.
	CHECK_NEAR(CalculateDamage({50.0, 0.0, 0.30, 0.5, 0.0, 0.0}), 57.5);

	// Flat bonus adds before the multiplier.
	CHECK_NEAR(CalculateDamage({20.0, 5.0, 0.20, 1.0, 0.0, 0.0}), 30.0);

	// 40% AR with 50% AP leaves 20% AR.
	CHECK_NEAR(CalculateDamage({100.0, 0.0, 0.0, 1.0, 0.40, 0.50}), 80.0);
	CHECK_NEAR(CalculateDamage({100.0, 0.0, 0.0, 1.0, 0.40, 1.50}), 100.0);

	// Negative resistance amplifies and ignores penetration.
	CHECK_NEAR(CalculateDamage({100.0, 0.0, 0.0, 1.0, -0.10, 0.50}), 110.0);

	// Never negative.
	CHECK_NEAR(CalculateDamage({100.0, 0.0, -2.0, 1.0, 0.0, 0.0}), 0.0);
	CHECK_NEAR(CalculateDamage({100.0, 0.0, 0.0, 1.0, 1.5, 0.0}), 0.0);

	// True Damage: flat, after resistance, no bonus.
	{
		FDamageInput Input{100.0, 0.0, 0.50, 1.0, 0.40, 0.0};
		Input.TrueDamage = 3.0;
		CHECK_NEAR(CalculateDamage(Input), 100.0 * 1.5 * 0.6 + 3.0);
		Input.Resistance = 2.0;
		CHECK_NEAR(CalculateDamage(Input), 3.0);
		Input.TrueDamage = -5.0;
		CHECK_NEAR(CalculateDamage(Input), 0.0);
	}

	CHECK_NEAR(CalculateHealing(20.0, 0.0, 0.10), 22.0);
	CHECK_NEAR(CalculateHealing(20.0, 5.0, 0.20, 0.5), 27.5);
}

static void TestReductions()
{
	// Additive: 15% Art CDR + 10% Melee mastery = 25%.
	CHECK_NEAR(CombineReductions({0.15, 0.10}, 0.5), 0.25);
	// Capped at 50%.
	CHECK_NEAR(CombineReductions({0.30, 0.10, 0.30}, 0.5), 0.50);
	// Low attributes can lengthen cooldowns.
	CHECK_NEAR(CombineReductions({-0.10}, 0.5), -0.10);
	// Cast time reduction is uncapped.
	CHECK_NEAR(SumReductions({0.30, 0.10, 0.30}), 0.70);
	CHECK_NEAR(SumReductions({}), 0.0);

	CHECK_NEAR(ApplyReduction(12.0, 0.25), 9.0);
	CHECK_NEAR(ApplyReduction(12.0, -0.10), 13.2);
}

static FSkillCatalog MakeCatalog()
{
	FSkillCatalog Catalog;
	CHECK(Catalog.Add({"dagger_mastery", 2, {}, {}}));
	CHECK(Catalog.Add({"splinter_of_winter", 2, {"dagger_mastery"}, {}}));
	CHECK(Catalog.Add({"antimagic_garb", 5, {}, {}}));
	CHECK(Catalog.Add({"spear_mastery", 2, {}, {}}));
	CHECK(Catalog.Add({"triple_thrust", 8, {}, {}}));
	CHECK(Catalog.Add({"spear_of_liscor", 15, {"triple_thrust", "spear_mastery", "antimagic_garb"}, {}}));
	CHECK(Catalog.Add({"multiscale", 9, {}, {}}));
	CHECK(Catalog.Add({"immutable", 9, {}, {}}));
	CHECK(Catalog.Add({"the_mithril_knight", 15, {"multiscale", "immutable", "antimagic_garb"}, {}}));
	CHECK(Catalog.Add({"flight", 8, {}, {}}));
	CHECK(Catalog.Add({"flare_breath", 9, {}, {}}));
	CHECK(Catalog.Add({"dragonification", 13, {}, {"flare_breath", "flight"}}));
	CHECK(Catalog.Add({"fireball", 3, {}, {}}));
	CHECK(Catalog.Add({"heal", 2, {}, {}}));
	CHECK(!Catalog.Add({"heal", 2, {}, {}}));
	CHECK(!Catalog.Add({"", 1, {}, {}}));
	return Catalog;
}

static void TestLoadout()
{
	const FSkillCatalog Catalog = MakeCatalog();

	const std::unordered_set<std::string> Liscor = Catalog.EffectiveSkills("spear_of_liscor");
	CHECK(Liscor.size() == 4);
	CHECK(Liscor.count("antimagic_garb") == 1);

	FLoadoutResult Result = ValidateLoadout(Catalog, {"fireball", "heal"}, 6);
	CHECK(Result.IsValid());
	CHECK(Result.TotalCost == 5);

	CHECK(ValidateLoadout(Catalog, {}, 6).IsValid());

	// Exactly at capacity is fine; one over is not.
	CHECK(ValidateLoadout(Catalog, {"fireball", "heal", "dagger_mastery"}, 7).IsValid());
	Result = ValidateLoadout(Catalog, {"fireball", "heal", "dagger_mastery"}, 6);
	CHECK(Result.Error == ELoadoutError::OverCapacity);
	CHECK(Result.TotalCost == 7);

	Result = ValidateLoadout(Catalog, {"fireball", "meteor"}, 20);
	CHECK(Result.Error == ELoadoutError::UnknownSkill);
	CHECK(Result.SkillId == "meteor");

	Result = ValidateLoadout(Catalog, {"heal", "heal"}, 20);
	CHECK(Result.Error == ELoadoutError::DuplicateSkill);
	CHECK(Result.SkillId == "heal");

	// A bundle plus its component is blocked, in either order.
	Result = ValidateLoadout(Catalog, {"splinter_of_winter", "dagger_mastery"}, 20);
	CHECK(Result.Error == ELoadoutError::SupersededSkill);
	CHECK(Result.SkillId == "dagger_mastery");
	CHECK(Result.OtherSkillId == "splinter_of_winter");
	Result = ValidateLoadout(Catalog, {"dagger_mastery", "splinter_of_winter"}, 20);
	CHECK(Result.Error == ELoadoutError::SupersededSkill);
	CHECK(Result.SkillId == "dagger_mastery");

	// Skills granted while active are blocked too.
	Result = ValidateLoadout(Catalog, {"flight", "dragonification"}, 30);
	CHECK(Result.Error == ELoadoutError::SupersededSkill);
	CHECK(Result.SkillId == "flight");
	CHECK(Result.OtherSkillId == "dragonification");
	CHECK(ValidateLoadout(Catalog, {"dragonification", "flare_breath"}, 30).Error == ELoadoutError::SupersededSkill);
	// ...but they are not permanent effects.
	CHECK(Catalog.EffectiveSkills("dragonification").size() == 1);
	CHECK(Catalog.ProvidedSkills("dragonification").size() == 2);

	// Two bundles sharing Antimagic Garb are allowed; the garb applies once.
	const std::vector<std::string> Knight{"spear_of_liscor", "the_mithril_knight"};
	CHECK(ValidateLoadout(Catalog, Knight, 30).IsValid());
	const std::unordered_set<std::string> Effects = ResolveLoadoutEffects(Catalog, Knight);
	CHECK(Effects.size() == 7);
	CHECK(Effects.count("antimagic_garb") == 1);
	CHECK(Effects.count("immutable") == 1);

	// A shared component cannot be equipped on its own next to either bundle.
	CHECK(ValidateLoadout(Catalog, {"spear_of_liscor", "the_mithril_knight", "antimagic_garb"}, 40).Error == ELoadoutError::SupersededSkill);

	// Superseded is reported before capacity.
	CHECK(ValidateLoadout(Catalog, {"spear_of_liscor", "antimagic_garb"}, 1).Error == ELoadoutError::SupersededSkill);

	// Sources: an equipped skill comes by itself (empty chain); a component through its bundle, whose
	// requirement switches it off; a shared component through either bundle.
	const FLoadoutSources Sources = ResolveLoadoutSources(Catalog, Knight);
	CHECK(Sources.size() == Effects.size());
	CHECK(Sources.at("the_mithril_knight").size() == 1);
	CHECK(Sources.at("the_mithril_knight")[0].empty());
	CHECK(Sources.at("immutable").size() == 1);
	CHECK(Sources.at("immutable")[0] == std::vector<std::string>{"the_mithril_knight"});
	CHECK(Sources.at("triple_thrust")[0] == std::vector<std::string>{"spear_of_liscor"});
	CHECK(Sources.at("antimagic_garb").size() == 2);
	const FLoadoutSources Alone = ResolveLoadoutSources(Catalog, {"antimagic_garb", "fireball"});
	CHECK(Alone.size() == 2);
	CHECK(Alone.at("antimagic_garb").size() == 1 && Alone.at("antimagic_garb")[0].empty());
}

static void TestRarityAndWeapons()
{
	const FItemConfig Config;

	CHECK(RarityLevel(ERarity::Common) == 0);
	CHECK(RarityLevel(ERarity::Mystic) == 5);

	// One-Handed Sword: 30 AD, +2 per rarity level.
	CHECK_NEAR(ValueAtRarity(30.0, 2.0, ERarity::Common), 30.0);
	CHECK_NEAR(ValueAtRarity(30.0, 2.0, ERarity::Epic), 36.0);
	CHECK_NEAR(ValueAtRarity(30.0, 2.0, ERarity::Mystic), 40.0);
	// One-Handed Shield: 8% AR, +2% per rarity level.
	CHECK_NEAR(ValueAtRarity(0.08, 0.02, ERarity::Legendary), 0.16);

	CHECK(AffixSlots(Config, ERarity::Common) == 0);
	CHECK(AffixSlots(Config, ERarity::Mystic) == 5);

	// Dagger: 3 attacks per second. Two-Handed Sword: once every 1.4 s. Axe: 1.2 per second.
	CHECK_NEAR(AttackInterval(Config, 3.0, 1.0, 0.0), 1.0 / 3.0);
	CHECK_NEAR(AttackInterval(Config, 1.0, 1.4, 0.0), 1.4);
	CHECK_NEAR(AttackInterval(Config, 1.2, 1.0, 0.0), 1.0 / 1.2);
	// +25% Action Speed (Quickshot) speeds attacks up.
	CHECK_NEAR(AttackInterval(Config, 1.0, 1.0, 0.25), 0.8);
	// Negative Action Speed slows them, floored.
	CHECK_NEAR(AttackInterval(Config, 1.0, 1.0, -0.2), 1.25);
	CHECK_NEAR(AttackInterval(Config, 1.0, 1.0, -5.0), 10.0);
	// Shields do not attack.
	CHECK_NEAR(AttackInterval(Config, 0.0, 0.0, 0.0), 0.0);
}

static void TestEquipment()
{
	const FEquippedWeapon Sword{EWeaponType::Sword, EHandedness::OneHanded, true};
	const FEquippedWeapon GreatSword{EWeaponType::Sword, EHandedness::TwoHanded, true};
	const FEquippedWeapon GreatAxe{EWeaponType::Axe, EHandedness::TwoHanded, true};
	const FEquippedWeapon Shield{EWeaponType::Shield, EHandedness::OneHanded, true};
	const FEquippedWeapon Dagger{EWeaponType::Dagger, EHandedness::OneHanded, true};
	const FEquippedWeapon Bow{EWeaponType::Bow, EHandedness::TwoHanded, false};
	const FEquippedWeapon Staff{EWeaponType::MagicStaff, EHandedness::TwoHanded, false};
	const FEquippedWeapon Wildcard{EWeaponType::AnyMelee, EHandedness::OneHanded, true};

	CHECK(ValidateEquipment({}) == EEquipError::None);
	CHECK(ValidateEquipment({Sword, Shield}) == EEquipError::None);
	CHECK(ValidateEquipment({GreatSword, std::nullopt}) == EEquipError::None);
	CHECK(ValidateEquipment({std::nullopt, Shield}) == EEquipError::None);
	CHECK(ValidateEquipment({GreatSword, Shield}) == EEquipError::TwoHandedWithOffHand);
	// Dual wielding: any one-handed weapon fits the off hand.
	CHECK(ValidateEquipment({Sword, Dagger}) == EEquipError::None);
	CHECK(ValidateEquipment({Dagger, Dagger}) == EEquipError::None);
	CHECK(ValidateEquipment({Sword, GreatSword}) == EEquipError::TwoHandedInOffHand);
	CHECK(ValidateEquipment({std::nullopt, GreatSword}) == EEquipError::TwoHandedInOffHand);
	CHECK(ValidateEquipment({Shield, std::nullopt}) == EEquipError::ShieldInMainHand);
	CHECK(ValidateEquipment({Wildcard, std::nullopt}) == EEquipError::WildcardType);

	const FEquipment SwordAndBoard{Sword, Shield};
	CHECK(SatisfiesWeaponRequirement({}, FEquipment{}));
	CHECK(SatisfiesWeaponRequirement({EWeaponType::Sword}, SwordAndBoard));
	CHECK(SatisfiesWeaponRequirement({EWeaponType::Shield}, SwordAndBoard));
	CHECK(!SatisfiesWeaponRequirement({EWeaponType::Axe}, SwordAndBoard));
	// Shade Blow: Swords or Axes.
	CHECK(SatisfiesWeaponRequirement({EWeaponType::Sword, EWeaponType::Axe}, {GreatAxe, std::nullopt}));
	// A two-handed sword is still a Sword (Sword Mastery, Shooting Star).
	CHECK(SatisfiesWeaponRequirement({EWeaponType::Sword}, {GreatSword, std::nullopt}));

	// Cleave: any melee weapon. The shield counts.
	CHECK(SatisfiesWeaponRequirement({EWeaponType::AnyMelee}, SwordAndBoard));
	CHECK(SatisfiesWeaponRequirement({EWeaponType::AnyMelee}, {std::nullopt, Shield}));
	CHECK(!SatisfiesWeaponRequirement({EWeaponType::AnyMelee}, {Bow, std::nullopt}));
	CHECK(!SatisfiesWeaponRequirement({EWeaponType::AnyMelee}, FEquipment{}));

	// Parting The Land: two-handed melee only.
	CHECK(SatisfiesWeaponRequirement({EWeaponType::TwoHandedMelee}, {GreatAxe, std::nullopt}));
	CHECK(!SatisfiesWeaponRequirement({EWeaponType::TwoHandedMelee}, SwordAndBoard));
	CHECK(!SatisfiesWeaponRequirement({EWeaponType::TwoHandedMelee}, {Bow, std::nullopt}));
	CHECK(!SatisfiesWeaponRequirement({EWeaponType::TwoHandedMelee}, {Staff, std::nullopt}));
}

static void TestSecondaryAction()
{
	const FEquippedWeapon Sword{EWeaponType::Sword, EHandedness::OneHanded, true, true, false};
	const FEquippedWeapon Dagger{EWeaponType::Dagger, EHandedness::OneHanded, true, true, false};
	const FEquippedWeapon ParryingSword{EWeaponType::Sword, EHandedness::OneHanded, true, true, true};
	const FEquippedWeapon GreatSword{EWeaponType::Sword, EHandedness::TwoHanded, true, true, true};
	const FEquippedWeapon Bow{EWeaponType::Bow, EHandedness::TwoHanded, false, true, false};
	const FEquippedWeapon Shield{EWeaponType::Shield, EHandedness::OneHanded, true, false, false};

	CHECK(SecondaryAction({Sword, Dagger}) == ESecondaryAction::OffHandAttack);
	CHECK(SecondaryAction({Sword, Shield}) == ESecondaryAction::Block);
	CHECK(SecondaryAction({std::nullopt, Shield}) == ESecondaryAction::Block);
	CHECK(SecondaryAction({GreatSword, std::nullopt}) == ESecondaryAction::Parry);
	CHECK(SecondaryAction({ParryingSword, std::nullopt}) == ESecondaryAction::Parry);
	CHECK(SecondaryAction({Sword, std::nullopt}) == ESecondaryAction::None);
	CHECK(SecondaryAction({Bow, std::nullopt}) == ESecondaryAction::None);
	CHECK(SecondaryAction(FEquipment{}) == ESecondaryAction::None);
	// Parry needs the off hand empty.
	CHECK(SecondaryAction({ParryingSword, Dagger}) == ESecondaryAction::OffHandAttack);
}

static void TestArmor()
{
	FArmorLoadout Armor{};
	CHECK(CountEmptyArmorSlots(Armor) == 6);
	CHECK(SatisfiesArmorRequirement(EArmorRequirement::None, Armor));
	CHECK(!SatisfiesArmorRequirement(EArmorRequirement::Plate, Armor));
	CHECK(!SatisfiesArmorRequirement(EArmorRequirement::PlateAllSlots, Armor));

	// One plate piece satisfies [Armor Mastery].
	Armor[static_cast<size_t>(EArmorSlot::Hands)] = EArmorType::Plate;
	CHECK(SatisfiesArmorRequirement(EArmorRequirement::Plate, Armor));
	CHECK(CountEmptyArmorSlots(Armor) == 5);

	// [The Mithril Knight]: plate in the five body slots; the Back slot does not matter.
	for (const EArmorSlot Slot : {EArmorSlot::Head, EArmorSlot::Chest, EArmorSlot::Legs})
	{
		Armor[static_cast<size_t>(Slot)] = EArmorType::Plate;
	}
	CHECK(!SatisfiesArmorRequirement(EArmorRequirement::PlateAllSlots, Armor));
	Armor[static_cast<size_t>(EArmorSlot::Feet)] = EArmorType::Leather;
	CHECK(!SatisfiesArmorRequirement(EArmorRequirement::PlateAllSlots, Armor));
	Armor[static_cast<size_t>(EArmorSlot::Feet)] = EArmorType::Plate;
	CHECK(SatisfiesArmorRequirement(EArmorRequirement::PlateAllSlots, Armor));
	Armor[static_cast<size_t>(EArmorSlot::Back)] = EArmorType::Cloth;
	CHECK(SatisfiesArmorRequirement(EArmorRequirement::PlateAllSlots, Armor));

	// [Cor Leonis] counts all six slots.
	CHECK(CountEmptyArmorSlots(Armor) == 0);
	Armor[static_cast<size_t>(EArmorSlot::Back)].reset();
	CHECK(CountEmptyArmorSlots(Armor) == 1);
}

static void TestAffixRolls()
{
	const std::vector<FAffixDefinition> Pool{
		{"strength", 1.0, 3.0, 1.0},
		{"adb", 0.01, 0.05, 0.01},
		{"move_speed", 0.02, 0.04, 0.0},
		{"ar", 0.05, 0.05, 0.0},
	};

	// Same seed, same result.
	const std::vector<FRolledAffix> A = RollAffixes(Pool, 3, 42);
	const std::vector<FRolledAffix> B = RollAffixes(Pool, 3, 42);
	CHECK(A.size() == 3);
	bool bSame = A.size() == B.size();
	for (size_t I = 0; bSame && I < A.size(); ++I)
	{
		bSame = A[I].Id == B[I].Id && A[I].Value == B[I].Value;
	}
	CHECK(bSame);

	// Never more than the pool; never duplicates.
	CHECK(RollAffixes(Pool, 10, 7).size() == 4);
	CHECK(RollAffixes(Pool, 0, 7).empty());
	CHECK(RollAffixes({}, 3, 7).empty());

	// Values stay in range and on the step grid; both ends of a stepped range are reachable.
	bool bInRange = true;
	bool bOnGrid = true;
	bool bSawMin = false;
	bool bSawMax = false;
	bool bDistinct = true;
	for (uint64_t Seed = 0; Seed < 2000; ++Seed)
	{
		const std::vector<FRolledAffix> Rolled = RollAffixes(Pool, 4, Seed);
		for (size_t I = 0; I < Rolled.size(); ++I)
		{
			for (size_t J = 0; J < I; ++J)
			{
				bDistinct = bDistinct && Rolled[I].Id != Rolled[J].Id;
			}
			for (const FAffixDefinition& Def : Pool)
			{
				if (Def.Id != Rolled[I].Id)
				{
					continue;
				}
				bInRange = bInRange && Rolled[I].Value >= Def.Min - 1e-12 && Rolled[I].Value <= Def.Max + 1e-12;
				if (Def.Step > 0.0)
				{
					const double Steps = (Rolled[I].Value - Def.Min) / Def.Step;
					bOnGrid = bOnGrid && std::fabs(Steps - std::round(Steps)) < 1e-9;
				}
				if (Def.Id == "strength")
				{
					bSawMin = bSawMin || Rolled[I].Value == 1.0;
					bSawMax = bSawMax || Rolled[I].Value == 3.0;
				}
			}
		}
	}
	// Ranges that went through float (Unreal data tables) still reach Max.
	{
		const std::vector<FAffixDefinition> FloatPool{{"pct", static_cast<float>(0.02), static_cast<float>(0.05), static_cast<float>(0.01)}};
		bool bReachedMax = false;
		for (uint64_t Seed = 0; Seed < 500 && !bReachedMax; ++Seed)
		{
			bReachedMax = std::fabs(RollAffixes(FloatPool, 1, Seed)[0].Value - static_cast<float>(0.05)) < 1e-6;
		}
		CHECK(bReachedMax);
	}

	CHECK(bInRange);
	CHECK(bOnGrid);
	CHECK(bDistinct);
	CHECK(bSawMin);
	CHECK(bSawMax);
}

static void TestTeams()
{
	FTeamRegistry Teams;

	// A party of 8 shares one team.
	int PartyTeam = -1;
	for (int I = 0; I < 8; ++I)
	{
		const FTeamJoinResult Joined = Teams.Join("p" + std::to_string(I), "party-a");
		CHECK(Joined.IsValid());
		if (I == 0)
		{
			PartyTeam = Joined.TeamId;
		}
		CHECK(Joined.TeamId == PartyTeam);
	}
	CHECK(Teams.TeamSize(PartyTeam) == 8);

	// A ninth is refused, and CanJoin agrees without changing anything.
	CHECK(Teams.CanJoin("p8", "party-a") == ETeamJoinError::TeamFull);
	CHECK(Teams.Join("p8", "party-a").Error == ETeamJoinError::TeamFull);
	CHECK(!Teams.TeamOf("p8"));
	CHECK(Teams.TeamSize(PartyTeam) == 8);

	// Solo players get their own teams.
	const FTeamJoinResult SoloA = Teams.Join("solo-a", "");
	const FTeamJoinResult SoloB = Teams.Join("solo-b", "");
	CHECK(SoloA.IsValid() && SoloB.IsValid());
	CHECK(SoloA.TeamId != SoloB.TeamId);
	CHECK(SoloA.TeamId != PartyTeam);
	CHECK(Teams.TeamSize(SoloA.TeamId) == 1);

	// Another party gets another team.
	const FTeamJoinResult OtherParty = Teams.Join("q0", "party-b");
	CHECK(OtherParty.IsValid() && OtherParty.TeamId != PartyTeam);

	CHECK(Teams.AreAllies("p0", "p7"));
	CHECK(!Teams.AreAllies("p0", "q0"));
	CHECK(!Teams.AreAllies("solo-a", "solo-b"));
	CHECK(!Teams.AreAllies("p0", "nobody"));
	CHECK(Teams.AreAllies("solo-a", "solo-a"));

	CHECK(Teams.Join("p0", "party-a").Error == ETeamJoinError::AlreadyInMatch);
	CHECK(Teams.Join("", "party-a").Error == ETeamJoinError::InvalidPlayer);

	// Leaving frees a slot; rejoining with the party key returns to the same team.
	CHECK(Teams.Leave("p3"));
	CHECK(!Teams.Leave("p3"));
	CHECK(Teams.TeamSize(PartyTeam) == 7);
	CHECK(!Teams.TeamOf("p3"));
	const FTeamJoinResult Rejoined = Teams.Join("p3", "party-a");
	CHECK(Rejoined.IsValid() && Rejoined.TeamId == PartyTeam);

	// The cap is configurable.
	FMatchConfig Duos;
	Duos.MaxTeamSize = 2;
	FTeamRegistry DuoTeams(Duos);
	CHECK(DuoTeams.Join("a", "x").IsValid());
	CHECK(DuoTeams.Join("b", "x").IsValid());
	CHECK(DuoTeams.Join("c", "x").Error == ETeamJoinError::TeamFull);
}

static void TestIncludeCycle()
{
	FSkillCatalog Catalog;
	Catalog.Add({"a", 1, {"b"}, {}});
	Catalog.Add({"b", 1, {"a"}, {}});
	CHECK(Catalog.EffectiveSkills("a").size() == 2);
	CHECK(Catalog.ProvidedSkills("a").size() == 1);

	// Chains stop at a cycle: a by itself, b through a.
	const FLoadoutSources Sources = ResolveLoadoutSources(Catalog, {"a"});
	CHECK(Sources.size() == 2);
	CHECK(Sources.at("a").size() == 1 && Sources.at("a")[0].empty());
	CHECK(Sources.at("b").size() == 1 && Sources.at("b")[0] == std::vector<std::string>{"a"});

	// Nested bundles: c comes through both, outermost first.
	FSkillCatalog Nested;
	Nested.Add({"outer", 1, {"inner"}, {}});
	Nested.Add({"inner", 1, {"c"}, {}});
	Nested.Add({"c", 1, {}, {}});
	CHECK(ResolveLoadoutSources(Nested, {"outer"}).at("c")[0] == (std::vector<std::string>{"outer", "inner"}));
}

static void TestExperience()
{
	const FXpConfig Xp;
	CHECK(XpForSource(Xp, EXpSource::BasicMonster) == 5);
	CHECK(XpForSource(Xp, EXpSource::BossMonster) == 50);
	CHECK(XpForSource(Xp, EXpSource::PlayerKill) == 25);
	CHECK(XpForSource(Xp, EXpSource::GatherNode) == 10);
	CHECK(XpForSource(Xp, EXpSource::Quest, 40) == 40);
	CHECK(XpForSource(Xp, EXpSource::Quest, 5) == 10);
	CHECK(XpForSource(Xp, EXpSource::Quest, 500) == 100);
	CHECK(XpForSource(Xp, EXpSource::Craft, 0, ERarity::Common) == 10);
	CHECK(XpForSource(Xp, EXpSource::Craft, 0, ERarity::Epic) == 40);
	CHECK(XpForSource(Xp, EXpSource::Enchant, 0, ERarity::Mystic) == 60);
	CHECK(XpForSource(Xp, EXpSource::Extract) == 35);

	const FProgressionConfig Config;
	CHECK(XpToNextLevel(Config, 1) == 50);
	CHECK(XpToNextLevel(Config, 2) == 55);
	// 60.5 rounds up.
	CHECK(XpToNextLevel(Config, 3) == 61);
	CHECK(XpToNextLevel(Config, 14) == 173);
	CHECK(XpToNextLevel(Config, 0) == 50);

	CHECK(TotalXpForLevel(Config, 1) == 0);
	CHECK(TotalXpForLevel(Config, 2) == 50);
	CHECK(TotalXpForLevel(Config, 3) == 105);
	CHECK(TotalXpForLevel(Config, 15) == 1401);

	CHECK(LevelForTotalXp(Config, 0) == 1);
	CHECK(LevelForTotalXp(Config, 49) == 1);
	CHECK(LevelForTotalXp(Config, 50) == 2);
	CHECK(LevelForTotalXp(Config, 104) == 2);
	CHECK(LevelForTotalXp(Config, 105) == 3);
	CHECK(LevelForTotalXp(Config, 1400) == 14);
	CHECK(LevelForTotalXp(Config, 1401) == 15);
	// Capped at MaxLevel; XP past the cap counts once the cap rises.
	CHECK(LevelForTotalXp(Config, 100000) == 15);
	FProgressionConfig LateSeason;
	LateSeason.MaxLevel = 16;
	CHECK(LevelForTotalXp(LateSeason, 1401 + 190) == 16);
	CHECK(LevelForTotalXp(LateSeason, 1401 + 189) == 15);

	// Raid XP: held until the raid ends.
	FRaidXp Raid;
	Raid.Add(5);
	Raid.Add(25);
	Raid.Add(-10);
	Raid.Add(0);
	CHECK(Raid.Pending() == 30);
	CHECK(Raid.Extract(Xp) == 65);
	CHECK(Raid.Pending() == 0);
	Raid.Add(50);
	CHECK(Raid.FailExtract() == 50);
	CHECK(Raid.Pending() == 0);
	FXpConfig ExtractReward;
	ExtractReward.Extract = 20;
	Raid.Add(5);
	CHECK(Raid.Extract(ExtractReward) == 25);

	CHECK(CanRespec(false, 1) == ERespecError::None);
	CHECK(CanRespec(false, 0) == ERespecError::NoRespecToken);
	CHECK(CanRespec(true, 5) == ERespecError::InRaid);

	// Ruled price: 1000 banked gold or one respec token, the player's choice.
	const FEconomyConfig Economy;
	CHECK(Economy.RespecGoldPrice == 1000);
	int Tokens = 1;
	int Gold = 2500;
	CHECK(PayRespec(Economy, false, ERespecPayment::Gold, Tokens, Gold) == ERespecError::None);
	CHECK(Tokens == 1 && Gold == 1500);
	CHECK(PayRespec(Economy, false, ERespecPayment::Token, Tokens, Gold) == ERespecError::None);
	CHECK(Tokens == 0 && Gold == 1500);
	CHECK(PayRespec(Economy, false, ERespecPayment::Token, Tokens, Gold) == ERespecError::NoRespecToken);
	CHECK(Tokens == 0 && Gold == 1500);
	CHECK(PayRespec(Economy, true, ERespecPayment::Gold, Tokens, Gold) == ERespecError::InRaid);
	CHECK(Gold == 1500);
	CHECK(PayRespec(Economy, false, ERespecPayment::Gold, Tokens, Gold) == ERespecError::None);
	CHECK(Gold == 500);
	// Short of the price: refused, nothing taken; exactly the price is enough.
	CHECK(CanPayRespec(Economy, false, ERespecPayment::Gold, 0, 999) == ERespecError::NotEnoughGold);
	CHECK(PayRespec(Economy, false, ERespecPayment::Gold, Tokens, Gold) == ERespecError::NotEnoughGold);
	CHECK(Gold == 500);
	Gold = 1000;
	CHECK(PayRespec(Economy, false, ERespecPayment::Gold, Tokens, Gold) == ERespecError::None);
	CHECK(Gold == 0);
	// Gold never pays for a token payment, nor a token for gold.
	Tokens = 3;
	CHECK(CanPayRespec(Economy, false, ERespecPayment::Gold, Tokens, 0) == ERespecError::NotEnoughGold);
	CHECK(CanPayRespec(Economy, false, ERespecPayment::Token, 0, 5000) == ERespecError::NoRespecToken);
	CHECK(CanPayRespec(Economy, true, ERespecPayment::Token, Tokens, 5000) == ERespecError::InRaid);
}

static void TestInventoryGrid()
{
	const FInventoryConfig Config;
	CHECK(ContainerSize(Config, EContainerKind::Main).Squares() == 50);
	CHECK(ContainerSize(Config, EContainerKind::Backpack, ERarity::Common).Squares() == 5);
	CHECK(ContainerSize(Config, EContainerKind::Backpack, ERarity::Rare).Squares() == 15);
	CHECK(ContainerSize(Config, EContainerKind::Quiver, ERarity::Mystic).Squares() == 30);
	CHECK(ContainerAccepts(EContainerKind::Quiver, EItemCategory::Ammo));
	CHECK(!ContainerAccepts(EContainerKind::Quiver, EItemCategory::Weapon));
	CHECK(ContainerAccepts(EContainerKind::Backpack, EItemCategory::Weapon));
	CHECK(ContainerAccepts(EContainerKind::Main, EItemCategory::SkillGem));

	FInventoryGrid Grid(ContainerSize(Config, EContainerKind::Main));
	CHECK(Grid.FreeSquares() == 50);
	// Anti-cheat: client-sent positions near INT_MAX do not wrap around into the grid.
	CHECK(!Grid.CanPlace({2, 1}, {INT_MAX, 0, false}));
	CHECK(!Grid.CanPlace({1, 2}, {0, INT_MAX - 1, false}));

	// A 1×3 sword at the left edge.
	CHECK(Grid.Place(1, {1, 3}, {0, 0, false}));
	CHECK(Grid.ItemAt(0, 2) == 1);
	CHECK(Grid.ItemAt(1, 0) == 0);
	CHECK(Grid.FreeSquares() == 47);

	// Ids must be positive and unique; overlaps and out-of-bounds fail.
	CHECK(!Grid.Place(0, {1, 1}, {5, 0, false}));
	CHECK(!Grid.Place(1, {1, 1}, {5, 0, false}));
	CHECK(!Grid.Place(2, {2, 2}, {0, 1, false}));
	CHECK(!Grid.Place(2, {2, 2}, {9, 0, false}));
	CHECK(!Grid.Place(2, {2, 2}, {-1, 0, false}));
	CHECK(!Grid.Place(2, {0, 2}, {4, 0, false}));
	CHECK(Grid.Find(2) == nullptr);

	// Rotation swaps the footprint: 1×3 lying down fits along the bottom row.
	CHECK(!Grid.Place(2, {1, 3}, {3, 4, false}));
	CHECK(Grid.Place(2, {1, 3}, {3, 4, true}));
	CHECK(Grid.ItemAt(5, 4) == 2);
	CHECK(Grid.ItemAt(6, 4) == 0);

	// Moving may overlap the item's own squares.
	CHECK(Grid.Move(1, {0, 1, false}));
	CHECK(Grid.ItemAt(0, 0) == 0);
	CHECK(Grid.ItemAt(0, 3) == 1);
	CHECK(!Grid.Move(1, {3, 2, false}));
	CHECK(Grid.Find(1)->Placement.Y == 1);
	CHECK(!Grid.Move(99, {0, 0, false}));

	// First fit: top-left, unrotated before rotated.
	FGridPlacement Spot;
	CHECK(Grid.FindFreeSpot({2, 2}, true, Spot));
	CHECK(Spot.X == 1 && Spot.Y == 0 && !Spot.bRotated);

	// A 1×10 item fits a 10×5 grid only lying down.
	FInventoryGrid Small({10, 5});
	CHECK(!Small.FindFreeSpot({1, 10}, false, Spot));
	CHECK(Small.FindFreeSpot({1, 10}, true, Spot));
	CHECK(Spot.X == 0 && Spot.Y == 0 && Spot.bRotated);

	CHECK(Grid.Remove(1));
	CHECK(!Grid.Remove(1));
	CHECK(Grid.ItemAt(0, 1) == 0);
	CHECK(Grid.FreeSquares() == 47);
	CHECK(Grid.Remove(2));
	CHECK(Grid.IsEmpty());

	// Fill a Common backpack (5×1) and confirm it is full.
	FInventoryGrid Backpack(ContainerSize(Config, EContainerKind::Backpack, ERarity::Common));
	for (int I = 1; I <= 5; ++I)
	{
		CHECK(Backpack.FindFreeSpot({1, 1}, true, Spot) && Backpack.Place(I, {1, 1}, Spot));
	}
	CHECK(!Backpack.FindFreeSpot({1, 1}, true, Spot));
	CHECK(Backpack.FreeSquares() == 0);

	// Stacks: arrows stack to 20 in the same squares.
	int Target = 15;
	int Source = 12;
	CHECK(MergeStacks(Target, Source, 20) == 5);
	CHECK(Target == 20 && Source == 7);
	CHECK(MergeStacks(Target, Source, 20) == 0);
	Target = 3;
	CHECK(MergeStacks(Target, Source, 20) == 7);
	CHECK(Target == 10 && Source == 0);
	// Gemstones stack to 3 (Items.json; same item and rarity, checked by the game's inventory): four
	// rubies fill a stack of 3, the fourth starts another.
	{
		int GemStack = 0;
		int Rubies = 4;
		CHECK(MergeStacks(GemStack, Rubies, 3) == 3);
		CHECK(GemStack == 3 && Rubies == 1);
		CHECK(MergeStacks(GemStack, Rubies, 3) == 0);
		int SecondStack = 0;
		CHECK(MergeStacks(SecondStack, Rubies, 3) == 1);
		CHECK(SecondStack == 1 && Rubies == 0);
	}
	CHECK(!ContainerAccepts(EContainerKind::Quiver, EItemCategory::Throwable));

	// Stash tabs: 3000 gold each, at most 3 bought.
	CHECK(CanBuyStashTab(Config, 0, 3000, false) == EStashTabPurchaseError::None);
	CHECK(CanBuyStashTab(Config, 0, 2999, false) == EStashTabPurchaseError::NotEnoughGold);
	CHECK(CanBuyStashTab(Config, 2, 99999, false) == EStashTabPurchaseError::None);
	CHECK(CanBuyStashTab(Config, 3, 99999, false) == EStashTabPurchaseError::AtCap);
	CHECK(CanBuyStashTab(Config, 0, 99999, true) == EStashTabPurchaseError::InRaid);

	// A default grid has no squares.
	const FInventoryGrid Empty;
	CHECK(!Empty.FindFreeSpot({1, 1}, true, Spot));
	CHECK(Empty.ItemAt(0, 0) == 0);
}

static void TestSkillLibrary()
{
	const FSkillCatalog Catalog = MakeCatalog();
	FSkillLibrary Library;

	CHECK(LearnSkillGem(Catalog, Library, "fireball", true) == ELearnError::InRaid);
	CHECK(!Library.Contains("fireball"));
	CHECK(LearnSkillGem(Catalog, Library, "fireball", false) == ELearnError::None);
	CHECK(Library.Contains("fireball"));
	CHECK(LearnSkillGem(Catalog, Library, "fireball", false) == ELearnError::AlreadyLearned);
	CHECK(LearnSkillGem(Catalog, Library, "meteor", false) == ELearnError::UnknownSkill);
	CHECK(LearnSkillGem(Catalog, Library, "splinter_of_winter", false) == ELearnError::None);

	// Only learned skills can be equipped. A learned bundle does not need its components learned.
	CHECK(ValidateLoadout(Catalog, {"fireball", "splinter_of_winter"}, 20, &Library).IsValid());
	FLoadoutResult Result = ValidateLoadout(Catalog, {"fireball", "heal"}, 20, &Library);
	CHECK(Result.Error == ELoadoutError::NotLearned);
	CHECK(Result.SkillId == "heal");
	CHECK(Result.TotalCost == 5);
	// Unknown is still reported first.
	CHECK(ValidateLoadout(Catalog, {"heal", "meteor"}, 20, &Library).Error == ELoadoutError::UnknownSkill);
	// No library: not checked.
	CHECK(ValidateLoadout(Catalog, {"fireball", "heal"}, 20).IsValid());
}

static void TestEconomy()
{
	// Gemstones: 50 gold at Common, +50 per rarity level, 300 at Mystic.
	CHECK(SellPrice(50, 50, ERarity::Common) == 50);
	CHECK(SellPrice(50, 50, ERarity::Rare) == 150);
	CHECK(SellPrice(50, 50, ERarity::Mystic) == 300);
	CHECK(SellPrice(0, 50, ERarity::Mystic) == 0);
	CHECK(SellPrice(10, -5, ERarity::Epic) == 10);

	const FEconomyConfig Config;
	// Gear: 5 gold per square per rarity step.
	CHECK(GearSellPrice(Config, 8, ERarity::Mystic) == 240);
	CHECK(GearSellPrice(Config, 1, ERarity::Common) == 5);
	CHECK(GearSellPrice(Config, 6, ERarity::Rare) == 90);
	CHECK(GearSellPrice(Config, 0, ERarity::Mystic) == 0);

	const FDeathConfig Death;
	CHECK(CanRevive(Death, 0.0));
	CHECK(CanRevive(Death, 119.9));
	CHECK(!CanRevive(Death, 120.0));

	CHECK(CanAssignToHotbar(EItemCategory::Consumable));
	CHECK(CanAssignToHotbar(EItemCategory::Throwable));
	CHECK(!CanAssignToHotbar(EItemCategory::Tool));
	CHECK(!CanAssignToHotbar(EItemCategory::Weapon));

	CHECK_NEAR(LockpickSeconds(Config, 0.0), 5.0);

	// Drawing a weapon set: 1 s, faster with Dexterity.
	const FItemConfig Items;
	CHECK_NEAR(WeaponDrawSeconds(Items, 0.0), 1.0);
	CHECK_NEAR(WeaponDrawSeconds(Items, 0.25), 0.8);
	CHECK_NEAR(WeaponDrawSeconds(Items, -5.0), 10.0);
	CHECK_NEAR(LockpickSeconds(Config, 0.25), 4.0);
	CHECK_NEAR(LockpickSeconds(Config, -0.2), 6.25);
	// Floor: never slower than LockpickSeconds / 0.1.
	CHECK_NEAR(LockpickSeconds(Config, -5.0), 50.0);
}

static void TestPortal()
{
	const FPortalConfig Config;
	CHECK(CanSummonPortal(Config, true, 600.0, true, false) == EPortalSummonError::None);
	CHECK(CanSummonPortal(Config, true, 599.0, true, false) == EPortalSummonError::TooEarly);
	CHECK(CanSummonPortal(Config, true, 900.0, false, false) == EPortalSummonError::OutsidePointOfInterest);
	CHECK(CanSummonPortal(Config, true, 900.0, true, true) == EPortalSummonError::AlreadySummoned);
	CHECK(CanSummonPortal(Config, false, 900.0, true, false) == EPortalSummonError::NotInRaid);

	// Channels 60 s while kept, then stays open 30 s.
	FExtractPortal Portal(Config);
	CHECK(Portal.Advance(59.0, true) == EPortalState::Channeling);
	CHECK_NEAR(Portal.GetSecondsLeft(), 1.0);
	CHECK(!Portal.CanEnter());
	CHECK(Portal.Advance(2.0, true) == EPortalState::Open);
	CHECK(Portal.CanEnter());
	// The extra second counted toward the open time.
	CHECK_NEAR(Portal.GetSecondsLeft(), 29.0);
	// Keepers do not matter once open.
	CHECK(Portal.Advance(28.0, false) == EPortalState::Open);
	CHECK(Portal.Advance(1.0, false) == EPortalState::Closed);
	CHECK(!Portal.CanEnter());
	CHECK_NEAR(Portal.GetSecondsLeft(), 0.0);
	CHECK(Portal.Advance(5.0, true) == EPortalState::Closed);

	// Losing every keeper collapses the channel for good.
	FExtractPortal Abandoned(Config);
	CHECK(Abandoned.Advance(30.0, true) == EPortalState::Channeling);
	CHECK(Abandoned.Advance(0.25, false) == EPortalState::Collapsed);
	CHECK(Abandoned.Advance(60.0, true) == EPortalState::Collapsed);
}

static void TestLoot()
{
	using namespace Crossguard;
	FSkillGemRarities Gems{{"fireball", ERarity::Common}, {"heal", ERarity::Common}, {"unleash", ERarity::Epic}, {"cor_leonis", ERarity::Mystic}};

	// Same seed, same loot; drops always come from the entries.
	FLootTable Table;
	Table.RollsMin = 2;
	Table.RollsMax = 4;
	Table.NothingWeight = 1;
	Table.RarityWeights = {50, 25, 13, 7, 4, 1};
	Table.Entries = {{ELootKind::Weapon, "sword_one_handed", 3, 1, 1, ""}, {ELootKind::Gold, "gold_coin", 3, 5, 20, ""},
		{ELootKind::SkillGem, "", 2, 1, 1, ""}, {ELootKind::Item, "healing_potion", 2, 1, 3, ""}};
	const std::vector<FLootDrop> First = RollLoot(Table, Gems, 42);
	const std::vector<FLootDrop> Again = RollLoot(Table, Gems, 42);
	CHECK(First.size() == Again.size());
	bool bSame = First.size() == Again.size();
	for (size_t I = 0; bSame && I < First.size(); ++I)
	{
		bSame = First[I].Id == Again[I].Id && First[I].Count == Again[I].Count && First[I].Rarity == Again[I].Rarity && First[I].Seed == Again[I].Seed;
	}
	CHECK(bSame);

	// "Any armor" of one type: the drop carries the type for the game's pick.
	{
		FLootTable ArmorTable;
		ArmorTable.Entries = {{ELootKind::Armor, "", 1, 1, 1, "Plate"}};
		const std::vector<FLootDrop> ArmorDrops = RollLoot(ArmorTable, Gems, 7);
		CHECK(ArmorDrops.size() == 1 && ArmorDrops[0].Kind == ELootKind::Armor && ArmorDrops[0].Id.empty() && ArmorDrops[0].ArmorType == "Plate");
	}

	int Total = 0;
	int GemDrops = 0;
	bool bCountsInRange = true;
	bool bGemRarityMatches = true;
	bool bGoldCommon = true;
	for (uint64_t Seed = 1; Seed <= 500; ++Seed)
	{
		const std::vector<FLootDrop> Drops = RollLoot(Table, Gems, Seed);
		CHECK(Drops.size() <= 4);
		for (const FLootDrop& Drop : Drops)
		{
			++Total;
			if (Drop.Kind == ELootKind::Gold)
			{
				bCountsInRange &= Drop.Count >= 5 && Drop.Count <= 20;
				bGoldCommon &= Drop.Rarity == ERarity::Common;
			}
			if (Drop.Kind == ELootKind::Item)
			{
				bCountsInRange &= Drop.Count >= 1 && Drop.Count <= 3;
			}
			if (Drop.Kind == ELootKind::SkillGem)
			{
				++GemDrops;
				// An "any gem" drop is a real gem, at that gem's own rarity.
				const auto Found = Gems.find(Drop.Id);
				bGemRarityMatches &= Found != Gems.end() && Found->second == Drop.Rarity && Drop.Count == 1;
			}
		}
	}
	CHECK(Total > 500);
	CHECK(GemDrops > 0);
	CHECK(bCountsInRange);
	CHECK(bGemRarityMatches);
	CHECK(bGoldCommon);

	// A wanted rarity with no gems falls back to the nearest one below; above only if none below.
	FLootTable OnlyRare;
	OnlyRare.RarityWeights = {0, 0, 1, 0, 0, 0};
	OnlyRare.Entries = {{ELootKind::SkillGem, "", 1, 1, 1, ""}};
	const std::vector<FLootDrop> Rare = RollLoot(OnlyRare, Gems, 7);
	CHECK(Rare.size() == 1 && Rare[0].Rarity == ERarity::Common);
	FLootTable OnlyLegendary = OnlyRare;
	OnlyLegendary.RarityWeights = {0, 0, 0, 0, 1, 0};
	const std::vector<FLootDrop> Legendary = RollLoot(OnlyLegendary, Gems, 7);
	CHECK(Legendary.size() == 1 && Legendary[0].Rarity == ERarity::Epic && Legendary[0].Id == "unleash");
	const FSkillGemRarities OnlyMysticGem{{"cor_leonis", ERarity::Mystic}};
	const std::vector<FLootDrop> Up = RollLoot(OnlyRare, OnlyMysticGem, 7);
	CHECK(Up.size() == 1 && Up[0].Rarity == ERarity::Mystic);

	// A named gem drops at its skill's rarity; no gems at all: no "any gem" drop.
	FLootTable Named;
	Named.Entries = {{ELootKind::SkillGem, "cor_leonis", 1, 1, 1, ""}};
	const std::vector<FLootDrop> Mystic = RollLoot(Named, Gems, 3);
	CHECK(Mystic.size() == 1 && Mystic[0].Rarity == ERarity::Mystic);
	CHECK(RollLoot(OnlyRare, {}, 3).empty());

	// Only "nothing", or no rolls: empty.
	FLootTable Nothing;
	Nothing.NothingWeight = 1;
	Nothing.Entries = {{ELootKind::Gold, "gold_coin", 0, 1, 1, ""}};
	CHECK(RollLoot(Nothing, Gems, 9).empty());
	FLootTable NoRolls = Table;
	NoRolls.RollsMin = 0;
	NoRolls.RollsMax = 0;
	CHECK(RollLoot(NoRolls, Gems, 9).empty());

	// Rarity weights steer weapon rarity.
	FLootTable AllEpic;
	AllEpic.RarityWeights = {0, 0, 0, 1, 0, 0};
	AllEpic.Entries = {{ELootKind::Weapon, "bow", 1, 1, 1, ""}};
	CHECK(RollLoot(AllEpic, Gems, 11).at(0).Rarity == ERarity::Epic);
	// Gear always drops one piece per drop.
	FLootTable ManySwords;
	ManySwords.Entries = {{ELootKind::Weapon, "sword_one_handed", 1, 3, 3, ""}};
	CHECK(RollLoot(ManySwords, Gems, 5).at(0).Count == 1);
}

#include "CrossguardRules/Guild.h"

#include <limits>

static void TestGuild()
{
	const FGuildConfig Config;
	CHECK(Config.MaxMembers == 50);
	CHECK(Config.BankWidth == FInventoryConfig().StashTabWidth);
	CHECK(Config.BankHeight == FInventoryConfig().StashTabHeight);
	CHECK(Config.BankLogSize == 100);

	// Permissions matrix.
	const EGuildRank Ranks[] = {EGuildRank::Member, EGuildRank::Officer, EGuildRank::Leader};
	for (const EGuildRank Rank : Ranks)
	{
		const bool bMember = Rank == EGuildRank::Member;
		CHECK(GuildRankCan(Rank, EGuildPermission::Deposit));
		CHECK(GuildRankCan(Rank, EGuildPermission::Invite) == !bMember);
		CHECK(GuildRankCan(Rank, EGuildPermission::Withdraw) == !bMember);
		CHECK(GuildRankCan(Rank, EGuildPermission::Redistribute) == !bMember);
		CHECK(GuildRankCan(Rank, EGuildPermission::ManageBank) == !bMember);
		CHECK(GuildRankCan(Rank, EGuildPermission::ManageRanks) == (Rank == EGuildRank::Leader));
	}
	CHECK(CanKick(EGuildRank::Leader, EGuildRank::Officer));
	CHECK(CanKick(EGuildRank::Leader, EGuildRank::Member));
	CHECK(!CanKick(EGuildRank::Leader, EGuildRank::Leader));
	CHECK(CanKick(EGuildRank::Officer, EGuildRank::Member));
	CHECK(!CanKick(EGuildRank::Officer, EGuildRank::Officer));
	CHECK(!CanKick(EGuildRank::Officer, EGuildRank::Leader));
	CHECK(!CanKick(EGuildRank::Member, EGuildRank::Member));
	CHECK(!CanKick(EGuildRank::Member, EGuildRank::Officer));
	CHECK(!CanKick(EGuildRank::Member, EGuildRank::Leader));

	// Names: 3-24 characters after trimming; letters, digits, spaces and '-'.
	CHECK(TrimGuildName("  Iron Wolves  ") == "Iron Wolves");
	CHECK(TrimGuildName("   ").empty());
	CHECK(ValidateGuildName(Config, "Abc") == EGuildError::None);
	CHECK(ValidateGuildName(Config, "  Abc  ") == EGuildError::None);
	CHECK(ValidateGuildName(Config, "Ab") == EGuildError::InvalidName);
	CHECK(ValidateGuildName(Config, "  Ab   ") == EGuildError::InvalidName);
	CHECK(ValidateGuildName(Config, std::string(24, 'x')) == EGuildError::None);
	CHECK(ValidateGuildName(Config, std::string(25, 'x')) == EGuildError::InvalidName);
	CHECK(ValidateGuildName(Config, "Red-Hand 7") == EGuildError::None);
	CHECK(ValidateGuildName(Config, "Red_Hand") == EGuildError::InvalidName);
	CHECK(ValidateGuildName(Config, "Red\tHand") == EGuildError::InvalidName);
	CHECK(ValidateGuildName(Config, "Caf\xC3\xA9s") == EGuildError::InvalidName);
	CHECK(ValidateGuildName(Config, "") == EGuildError::InvalidName);

	// Create.
	FGuildRegistry Guilds;
	CHECK(Guilds.Create("g1", "  Iron Wolves ", "leader") == EGuildError::None);
	const FGuild* Wolves = Guilds.Find("g1");
	CHECK(Wolves != nullptr && Wolves->Name == "Iron Wolves");
	CHECK(Wolves != nullptr && Wolves->LeaderId() == "leader");
	CHECK(Wolves != nullptr && Wolves->Bank.Grid.Size().Width == 10 && Wolves->Bank.Grid.Size().Height == 30);
	CHECK(Guilds.RankOf("leader") == EGuildRank::Leader);
	CHECK(Guilds.GuildOf("leader") == Wolves);
	CHECK(Guilds.FindByName("IRON WOLVES") == Wolves);
	CHECK(Guilds.FindByName(" iron wolves ") == Wolves);
	CHECK(Guilds.FindByName("Iron Wolf") == nullptr);
	CHECK(Guilds.Create("g2", "iron WOLVES", "other") == EGuildError::NameTaken);
	CHECK(Guilds.Create("g1", "Other Name", "other") == EGuildError::GuildIdTaken);
	CHECK(Guilds.Create("", "Other Name", "other") == EGuildError::InvalidGuildId);
	CHECK(Guilds.Create("g2", "Other Name", "") == EGuildError::InvalidPlayer);
	CHECK(Guilds.Create("g2", "No!", "other") == EGuildError::InvalidName);
	// Double guild: one guild per player.
	CHECK(Guilds.Create("g2", "Other Name", "leader") == EGuildError::AlreadyInGuild);
	CHECK(Guilds.Find("g2") == nullptr && Guilds.FindByName("Other Name") == nullptr);
	CHECK(Guilds.Create("g2", "Ravens", "raven") == EGuildError::None);

	// Invites: Leader and Officer invite, Members do not.
	double Now = 1000.0;
	CHECK(Guilds.Invite("leader", "officer", Now) == EGuildError::None);
	CHECK(Guilds.Accept("g1", "officer", Now + 1.0) == EGuildError::None);
	CHECK(Guilds.RankOf("officer") == EGuildRank::Member);
	CHECK(Guilds.Promote("leader", "officer") == EGuildError::None);
	CHECK(Guilds.RankOf("officer") == EGuildRank::Officer);
	CHECK(Guilds.Invite("officer", "member", Now) == EGuildError::None);
	CHECK(Guilds.Accept("g1", "member", Now) == EGuildError::None);
	CHECK(Guilds.Invite("member", "someone", Now) == EGuildError::NotPermitted);
	CHECK(Guilds.Invite("nobody", "someone", Now) == EGuildError::NotInGuild);
	CHECK(Guilds.Invite("leader", "", Now) == EGuildError::InvalidPlayer);
	// Double guild: invitee already in this or another guild.
	CHECK(Guilds.Invite("leader", "raven", Now) == EGuildError::AlreadyInGuild);
	CHECK(Guilds.Invite("leader", "member", Now) == EGuildError::AlreadyInGuild);
	CHECK(Wolves->Members.size() == 3 && Wolves->Invites.empty());

	// Decline, re-invite, duplicate invite.
	CHECK(Guilds.Invite("leader", "a", Now) == EGuildError::None);
	CHECK(Guilds.Invite("officer", "a", Now + 10.0) == EGuildError::AlreadyInvited);
	CHECK(Guilds.Decline("g1", "a") == EGuildError::None);
	CHECK(Guilds.Decline("g1", "a") == EGuildError::NoInvite);
	CHECK(Guilds.Accept("g1", "a", Now) == EGuildError::NoInvite);
	CHECK(Guilds.Decline("nope", "a") == EGuildError::UnknownGuild);
	CHECK(Guilds.Accept("nope", "a", Now) == EGuildError::UnknownGuild);
	CHECK(Guilds.Accept("g1", "", Now) == EGuildError::InvalidPlayer);

	// Expired invite: refused, kept until pruned or replaced.
	CHECK(Guilds.Invite("leader", "late", Now) == EGuildError::None);
	CHECK(Guilds.Accept("g1", "late", Now + Config.InviteExpirySeconds) == EGuildError::InviteExpired);
	CHECK(Guilds.GuildOf("late") == nullptr && Wolves->Invites.size() == 1);
	CHECK(Guilds.Accept("g1", "late", Now + Config.InviteExpirySeconds - 0.5) == EGuildError::None);
	CHECK(Guilds.Invite("leader", "late2", Now) == EGuildError::None);
	CHECK(Guilds.Invite("leader", "late2", Now + Config.InviteExpirySeconds) == EGuildError::None);
	CHECK(Wolves->Invites.size() == 1 && Wolves->Invites[0].ExpiresAt == Now + 2.0 * Config.InviteExpirySeconds);
	CHECK(Guilds.PruneExpiredInvites(Now + 2.0 * Config.InviteExpirySeconds - 1.0) == 0);
	CHECK(Guilds.PruneExpiredInvites(Now + 2.0 * Config.InviteExpirySeconds) == 1);
	CHECK(Wolves->Invites.empty());

	// Invites from two guilds: accepting one makes the other fail.
	CHECK(Guilds.Invite("leader", "b", Now) == EGuildError::None);
	CHECK(Guilds.Invite("raven", "b", Now) == EGuildError::None);
	CHECK(Guilds.Accept("g2", "b", Now) == EGuildError::None);
	CHECK(Guilds.Accept("g1", "b", Now) == EGuildError::AlreadyInGuild);
	CHECK(Guilds.Decline("g1", "b") == EGuildError::None);
	// Members now: leader (L), officer (O), member (M), late (M).

	// Kick matrix.
	CHECK(Guilds.Kick("member", "late") == EGuildError::NotPermitted);
	CHECK(Guilds.Kick("member", "officer") == EGuildError::NotPermitted);
	CHECK(Guilds.Kick("member", "leader") == EGuildError::NotPermitted);
	CHECK(Guilds.Kick("officer", "leader") == EGuildError::NotPermitted);
	CHECK(Guilds.Kick("leader", "leader") == EGuildError::CannotTargetSelf);
	CHECK(Guilds.Kick("officer", "officer") == EGuildError::CannotTargetSelf);
	CHECK(Guilds.Kick("leader", "raven") == EGuildError::TargetNotInGuild);
	CHECK(Guilds.Kick("nobody", "late") == EGuildError::NotInGuild);
	CHECK(Wolves->Members.size() == 4);
	CHECK(Guilds.Kick("officer", "late") == EGuildError::None);
	CHECK(Guilds.GuildOf("late") == nullptr && Wolves->Members.size() == 3);
	CHECK(Guilds.Invite("leader", "o2", Now) == EGuildError::None);
	CHECK(Guilds.Accept("g1", "o2", Now) == EGuildError::None);
	CHECK(Guilds.Promote("leader", "o2") == EGuildError::None);
	CHECK(Guilds.Kick("officer", "o2") == EGuildError::NotPermitted);
	CHECK(Guilds.Kick("leader", "o2") == EGuildError::None);
	CHECK(Guilds.RankOf("o2") == std::nullopt);

	// Promote / demote: Leader only.
	CHECK(Guilds.Promote("officer", "member") == EGuildError::NotPermitted);
	CHECK(Guilds.Promote("member", "member") == EGuildError::CannotTargetSelf);
	CHECK(Guilds.Demote("officer", "member") == EGuildError::NotPermitted);
	CHECK(Guilds.Promote("leader", "officer") == EGuildError::AlreadyRank);
	CHECK(Guilds.Demote("leader", "member") == EGuildError::AlreadyRank);
	CHECK(Guilds.Promote("leader", "leader") == EGuildError::CannotTargetSelf);
	CHECK(Guilds.Promote("leader", "raven") == EGuildError::TargetNotInGuild);
	CHECK(Guilds.Demote("leader", "officer") == EGuildError::None);
	CHECK(Guilds.RankOf("officer") == EGuildRank::Member);
	CHECK(Guilds.Promote("leader", "officer") == EGuildError::None);

	// Leave: the Leader must transfer first.
	CHECK(Guilds.Leave("leader").Error == EGuildError::LeaderMustTransfer);
	CHECK(Guilds.RankOf("leader") == EGuildRank::Leader && Wolves->Members.size() == 3);
	CHECK(Guilds.Leave("nobody").Error == EGuildError::NotInGuild);
	CHECK(Guilds.TransferLeadership("officer", "member") == EGuildError::NotPermitted);
	CHECK(Guilds.TransferLeadership("leader", "leader") == EGuildError::CannotTargetSelf);
	CHECK(Guilds.TransferLeadership("leader", "raven") == EGuildError::TargetNotInGuild);
	CHECK(Guilds.TransferLeadership("leader", "member") == EGuildError::None);
	CHECK(Guilds.RankOf("member") == EGuildRank::Leader);
	CHECK(Guilds.RankOf("leader") == EGuildRank::Officer);
	CHECK(Wolves->LeaderId() == "member");
	// Ex-leader is an officer now and can leave.
	const FGuildLeaveResult Left = Guilds.Leave("leader");
	CHECK(Left.IsValid() && !Left.bDisbanded);
	CHECK(Guilds.GuildOf("leader") == nullptr && Wolves->Members.size() == 2);
	// Free to found or join another guild.
	CHECK(Guilds.Invite("raven", "leader", Now) == EGuildError::None);
	CHECK(Guilds.Accept("g2", "leader", Now) == EGuildError::None);

	// Bank, gold: anyone deposits; officers and the leader withdraw and redistribute.
	// g1 now: member (Leader), officer (Officer). Add a plain member.
	CHECK(Guilds.Invite("member", "m2", Now) == EGuildError::None);
	CHECK(Guilds.Accept("g1", "m2", Now) == EGuildError::None);
	CHECK(Guilds.DepositGold("m2", 500, 1.0) == EGuildError::None);
	CHECK(Guilds.DepositGold("officer", 250, 2.0) == EGuildError::None);
	CHECK(Wolves->Bank.Gold == 750);
	CHECK(Guilds.DepositGold("m2", 0, 3.0) == EGuildError::InvalidAmount);
	CHECK(Guilds.DepositGold("m2", -5, 3.0) == EGuildError::InvalidAmount);
	CHECK(Guilds.DepositGold("nobody", 5, 3.0) == EGuildError::NotInGuild);
	CHECK(Guilds.WithdrawGold("m2", 10, 3.0) == EGuildError::NotPermitted);
	CHECK(Guilds.RedistributeGold("m2", "officer", 10, 3.0) == EGuildError::NotPermitted);
	// Bank underflow.
	CHECK(Guilds.WithdrawGold("officer", 751, 3.0) == EGuildError::NotEnoughGold);
	CHECK(Guilds.RedistributeGold("member", "m2", 751, 3.0) == EGuildError::NotEnoughGold);
	CHECK(Guilds.WithdrawGold("officer", 0, 3.0) == EGuildError::InvalidAmount);
	CHECK(Wolves->Bank.Gold == 750);
	CHECK(Guilds.WithdrawGold("officer", 100, 4.0) == EGuildError::None);
	CHECK(Guilds.WithdrawGold("member", 50, 5.0) == EGuildError::None);
	CHECK(Guilds.RedistributeGold("officer", "m2", 100, 6.0) == EGuildError::None);
	CHECK(Guilds.RedistributeGold("officer", "raven", 100, 6.0) == EGuildError::TargetNotInGuild);
	CHECK(Guilds.RedistributeGold("officer", "", 100, 6.0) == EGuildError::TargetNotInGuild);
	CHECK(Wolves->Bank.Gold == 500);
	CHECK(Guilds.WithdrawGold("member", 500, 7.0) == EGuildError::None);
	CHECK(Wolves->Bank.Gold == 0);
	CHECK(Guilds.WithdrawGold("member", 1, 7.0) == EGuildError::NotEnoughGold);
	// Overflow is refused.
	CHECK(Guilds.DepositGold("m2", std::numeric_limits<std::int64_t>::max(), 8.0) == EGuildError::None);
	CHECK(Guilds.DepositGold("m2", 1, 8.0) == EGuildError::GoldOverflow);
	CHECK(Wolves->Bank.Gold == std::numeric_limits<std::int64_t>::max());
	CHECK(Guilds.WithdrawGold("member", std::numeric_limits<std::int64_t>::max(), 9.0) == EGuildError::None);

	// Log so far: 2 deposits, 3 withdrawals, 1 redistribution, 1 deposit, 1 withdrawal.
	CHECK(Wolves->BankLog.size() == 8);
	CHECK(Wolves->BankLog[0].PlayerId == "m2" && Wolves->BankLog[0].Action == EGuildBankAction::DepositGold);
	CHECK(Wolves->BankLog[0].Gold == 500 && Wolves->BankLog[0].Time == 1.0 && Wolves->BankLog[0].ItemId.empty());
	CHECK(Wolves->BankLog[4].Action == EGuildBankAction::RedistributeGold);
	CHECK(Wolves->BankLog[4].PlayerId == "officer" && Wolves->BankLog[4].RecipientId == "m2");
	CHECK(Wolves->BankLog[2].Action == EGuildBankAction::WithdrawGold && Wolves->BankLog[2].RecipientId.empty());
	// Redistributing to oneself is a withdrawal.
	CHECK(Guilds.DepositGold("m2", 10, 10.0) == EGuildError::None);
	CHECK(Guilds.RedistributeGold("officer", "officer", 10, 10.0) == EGuildError::None);
	CHECK(Wolves->BankLog.back().Action == EGuildBankAction::WithdrawGold);

	// Bank, items: placed on the stash-tab grid.
	FGuildBankItem Sword;
	Sword.ItemId = "sword_one_handed";
	Sword.Rarity = ERarity::Rare;
	Sword.Size = {1, 3};
	Sword.Payload = "affixes:...";
	const FGuildItemResult Deposited = Guilds.DepositItem("m2", Sword, nullptr, 11.0);
	CHECK(Deposited.IsValid() && Deposited.Item.SlotId > 0);
	CHECK(Wolves->Bank.Grid.ItemAt(0, 0) == Deposited.Item.SlotId);
	CHECK(Wolves->Bank.Grid.ItemAt(0, 2) == Deposited.Item.SlotId);
	CHECK(Wolves->Bank.Find(Deposited.Item.SlotId) != nullptr);
	CHECK(Wolves->Bank.Find(Deposited.Item.SlotId)->Payload == "affixes:...");
	CHECK(Wolves->BankLog.back().Action == EGuildBankAction::DepositItem);
	CHECK(Wolves->BankLog.back().ItemId == "sword_one_handed" && Wolves->BankLog.back().Count == 1);
	CHECK(Wolves->BankLog.back().Rarity == ERarity::Rare && Wolves->BankLog.back().Gold == 0);

	FGuildBankItem Potions;
	Potions.ItemId = "healing_potion";
	Potions.Count = 5;
	const FGridPlacement Corner{9, 29, false};
	const FGuildItemResult PotionsIn = Guilds.DepositItem("m2", Potions, &Corner, 12.0);
	CHECK(PotionsIn.IsValid() && Wolves->Bank.Grid.ItemAt(9, 29) == PotionsIn.Item.SlotId);
	CHECK(PotionsIn.Item.SlotId != Deposited.Item.SlotId);
	// Occupied or out-of-grid spots are refused.
	const FGridPlacement OnSword{0, 1, false};
	const FGridPlacement Outside{10, 0, false};
	CHECK(Guilds.DepositItem("m2", Potions, &OnSword, 12.0).Error == EGuildError::NoRoom);
	CHECK(Guilds.DepositItem("m2", Potions, &Outside, 12.0).Error == EGuildError::NoRoom);
	FGuildBankItem Bad = Potions;
	Bad.Count = 0;
	CHECK(Guilds.DepositItem("m2", Bad, nullptr, 12.0).Error == EGuildError::InvalidItem);
	Bad = Potions;
	Bad.ItemId.clear();
	CHECK(Guilds.DepositItem("m2", Bad, nullptr, 12.0).Error == EGuildError::InvalidItem);
	Bad = Potions;
	Bad.Size = {0, 1};
	CHECK(Guilds.DepositItem("m2", Bad, nullptr, 12.0).Error == EGuildError::InvalidItem);
	CHECK(Guilds.DepositItem("nobody", Potions, nullptr, 12.0).Error == EGuildError::NotInGuild);
	CHECK(Wolves->Bank.Items.size() == 2);

	// Members cannot withdraw, redistribute or rearrange items.
	CHECK(Guilds.WithdrawItem("m2", Deposited.Item.SlotId, 13.0).Error == EGuildError::NotPermitted);
	CHECK(Guilds.RedistributeItem("m2", "officer", Deposited.Item.SlotId, 13.0).Error == EGuildError::NotPermitted);
	CHECK(Guilds.MoveBankItem("m2", Deposited.Item.SlotId, {5, 5, true}) == EGuildError::NotPermitted);
	// Officers manage: move and rotate.
	CHECK(Guilds.MoveBankItem("officer", Deposited.Item.SlotId, {5, 5, true}) == EGuildError::None);
	CHECK(Wolves->Bank.Grid.ItemAt(7, 5) == Deposited.Item.SlotId && Wolves->Bank.Grid.ItemAt(0, 0) == 0);
	CHECK(Guilds.MoveBankItem("officer", Deposited.Item.SlotId, {8, 29, true}) == EGuildError::NoRoom);
	CHECK(Guilds.MoveBankItem("officer", 999, {0, 0, false}) == EGuildError::UnknownItem);

	const FGuildItemResult Given = Guilds.RedistributeItem("officer", "m2", Deposited.Item.SlotId, 14.0);
	CHECK(Given.IsValid() && Given.Item.ItemId == "sword_one_handed" && Given.Item.Payload == "affixes:...");
	CHECK(Wolves->Bank.Find(Deposited.Item.SlotId) == nullptr && Wolves->Bank.Grid.ItemAt(7, 5) == 0);
	CHECK(Wolves->BankLog.back().Action == EGuildBankAction::RedistributeItem);
	CHECK(Wolves->BankLog.back().RecipientId == "m2" && Wolves->BankLog.back().Time == 14.0);
	CHECK(Guilds.RedistributeItem("officer", "m2", Deposited.Item.SlotId, 14.0).Error == EGuildError::UnknownItem);
	CHECK(Guilds.RedistributeItem("officer", "raven", PotionsIn.Item.SlotId, 14.0).Error == EGuildError::TargetNotInGuild);
	const FGuildItemResult Taken = Guilds.WithdrawItem("member", PotionsIn.Item.SlotId, 15.0);
	CHECK(Taken.IsValid() && Taken.Item.Count == 5 && Wolves->Bank.Items.empty() && Wolves->Bank.Grid.IsEmpty());
	CHECK(Wolves->BankLog.back().Action == EGuildBankAction::WithdrawItem && Wolves->BankLog.back().Count == 5);

	// A full bank refuses deposits.
	FGuildConfig Tiny;
	Tiny.BankWidth = 2;
	Tiny.BankHeight = 2;
	Tiny.BankLogSize = 3;
	Tiny.MaxMembers = 2;
	FGuildRegistry Small(Tiny);
	CHECK(Small.Create("s", "Small", "boss") == EGuildError::None);
	FGuildBankItem Shield;
	Shield.ItemId = "shield";
	Shield.Size = {2, 2};
	CHECK(Small.DepositItem("boss", Shield, nullptr, 1.0).IsValid());
	CHECK(Small.DepositItem("boss", Potions, nullptr, 2.0).Error == EGuildError::NoRoom);
	// Rotation is used to fit.
	FGuildRegistry Narrow(Tiny);
	CHECK(Narrow.Create("n", "Narrow", "boss") == EGuildError::None);
	FGuildBankItem Bar;
	Bar.ItemId = "bar";
	Bar.Size = {1, 2};
	FGuildBankItem Wide;
	Wide.ItemId = "wide";
	Wide.Size = {2, 1};
	CHECK(Narrow.DepositItem("boss", Wide, nullptr, 1.0).IsValid());
	const FGuildItemResult Rotated = Narrow.DepositItem("boss", Bar, nullptr, 1.0);
	CHECK(Rotated.IsValid() && Narrow.Find("n")->Bank.Grid.Find(Rotated.Item.SlotId)->Placement.bRotated);

	// Log trimming: only the newest BankLogSize entries are kept.
	for (int I = 0; I < 5; ++I)
	{
		CHECK(Small.DepositGold("boss", I + 1, 10.0 + I) == EGuildError::None);
	}
	const FGuild* SmallGuild = Small.Find("s");
	CHECK(SmallGuild->BankLog.size() == 3);
	CHECK(SmallGuild->BankLog.front().Gold == 3 && SmallGuild->BankLog.back().Gold == 5);
	CHECK(SmallGuild->BankLog.back().Time == 14.0);
	CHECK(Wolves->BankLog.size() <= 100);
	for (int I = 0; I < 150; ++I)
	{
		CHECK(Guilds.DepositGold("m2", 1, 100.0 + I) == EGuildError::None);
	}
	CHECK(Wolves->BankLog.size() == 100 && Wolves->BankLog.front().Time == 150.0);

	// Full guild: invites and accepts are refused at MaxMembers.
	CHECK(Small.Invite("boss", "p1", 0.0) == EGuildError::None);
	CHECK(Small.Invite("boss", "p2", 0.0) == EGuildError::None);
	CHECK(Small.Accept("s", "p1", 1.0) == EGuildError::None);
	CHECK(Small.Accept("s", "p2", 1.0) == EGuildError::GuildFull);
	CHECK(Small.Invite("boss", "p3", 1.0) == EGuildError::GuildFull);
	CHECK(Small.GuildOf("p2") == nullptr && SmallGuild->Members.size() == 2);

	// Default cap of 50.
	FGuildRegistry Big;
	CHECK(Big.Create("b", "Big", "p0") == EGuildError::None);
	for (int I = 1; I < 50; ++I)
	{
		const std::string Player = "p" + std::to_string(I);
		CHECK(Big.Invite("p0", Player, 0.0) == EGuildError::None);
		CHECK(Big.Accept("b", Player, 0.0) == EGuildError::None);
	}
	CHECK(Big.Find("b")->Members.size() == 50);
	CHECK(Big.Invite("p0", "p50", 0.0) == EGuildError::GuildFull);

	// Last member leaving disbands the guild and hands over the bank; the name frees up.
	CHECK(Small.Kick("boss", "p1") == EGuildError::None);
	const FGuildLeaveResult Disbanded = Small.Leave("boss");
	CHECK(Disbanded.IsValid() && Disbanded.bDisbanded);
	CHECK(Disbanded.Gold == 15 && Disbanded.Items.size() == 1 && Disbanded.Items[0].ItemId == "shield");
	CHECK(Small.Find("s") == nullptr && Small.FindByName("Small") == nullptr && Small.GuildOf("boss") == nullptr);
	CHECK(Small.Accept("s", "p2", 1.0) == EGuildError::UnknownGuild);
	CHECK(Small.Create("s", "SMALL", "p2") == EGuildError::None);

	// Restore round-trips saved guilds and rejects bad data.
	FGuildRegistry Loaded;
	for (const auto& Entry : Guilds.All())
	{
		CHECK(Loaded.Restore(Entry.second) == EGuildError::None);
	}
	CHECK(Loaded.FindByName("iron wolves") != nullptr);
	CHECK(Loaded.RankOf("member") == EGuildRank::Leader && Loaded.RankOf("leader") == EGuildRank::Member);
	CHECK(Loaded.Restore(*Guilds.Find("g1")) == EGuildError::GuildIdTaken);
	FGuild Copy = *Guilds.Find("g1");
	Copy.Id = "g9";
	CHECK(Loaded.Restore(Copy) == EGuildError::NameTaken);
	Copy.Name = "Copy";
	CHECK(Loaded.Restore(Copy) == EGuildError::AlreadyInGuild);
	FGuild NoLeader;
	NoLeader.Id = "x";
	NoLeader.Name = "No Leader";
	NoLeader.Members = {{"x1", EGuildRank::Officer}};
	NoLeader.Bank.Grid = FInventoryGrid(FGridSize{2, 2});
	CHECK(Loaded.Restore(NoLeader) == EGuildError::InvalidData);
	NoLeader.Members = {{"x1", EGuildRank::Leader}, {"x2", EGuildRank::Leader}};
	CHECK(Loaded.Restore(NoLeader) == EGuildError::InvalidData);
	NoLeader.Members = {{"x1", EGuildRank::Leader}, {"x1", EGuildRank::Member}};
	CHECK(Loaded.Restore(NoLeader) == EGuildError::InvalidData);
	NoLeader.Members = {{"x1", EGuildRank::Leader}};
	NoLeader.Bank.Items.push_back(Shield);
	CHECK(Loaded.Restore(NoLeader) == EGuildError::InvalidData);
	NoLeader.Bank.Items.clear();
	NoLeader.Bank.Gold = -1;
	CHECK(Loaded.Restore(NoLeader) == EGuildError::InvalidData);
	NoLeader.Bank.Gold = 0;
	CHECK(Loaded.Restore(NoLeader) == EGuildError::None);
	CHECK(Loaded.RankOf("x1") == EGuildRank::Leader);
}

static void TestTrade()
{
	const FTradeConfig Config;
	CHECK(CanStartTrade(Config, false, 300.0, true, false) == ETradeError::None);
	CHECK(CanStartTrade(Config, true, 0.0, true, false) == ETradeError::SamePlayer);
	CHECK(CanStartTrade(Config, false, 600.0, true, false) == ETradeError::TooFar);
	CHECK(CanStartTrade(Config, false, 100.0, false, false) == ETradeError::NotAlive);
	CHECK(CanStartTrade(Config, false, 100.0, true, true) == ETradeError::Busy);

	FTradeSession Session;
	// Accepting needs both offers locked.
	CHECK(Session.Accept(0) == ETradeError::NotLocked);
	CHECK(Session.SetOffer(Config, 0, {{1, 2}, 50}, 100, false) == ETradeError::None);
	CHECK(Session.SetOffer(Config, 1, {{9}, 0}, 0, false) == ETradeError::None);
	// Gold: not more than banked, none in raids, never negative.
	CHECK(Session.SetOffer(Config, 0, {{1}, 150}, 100, false) == ETradeError::NotEnoughGold);
	CHECK(Session.SetOffer(Config, 0, {{1}, 10}, 100, true) == ETradeError::NotEnoughGold);
	CHECK(Session.SetOffer(Config, 0, {{1}, -1}, 100, false) == ETradeError::NotEnoughGold);
	// The same stack twice, or too many stacks.
	CHECK(Session.SetOffer(Config, 0, {{4, 4}, 0}, 0, false) == ETradeError::BadOffer);
	FTradeOffer Big;
	for (int Id = 1; Id <= Config.MaxOfferedStacks + 1; ++Id)
	{
		Big.ItemIds.push_back(Id);
	}
	CHECK(Session.SetOffer(Config, 0, Big, 0, false) == ETradeError::BadOffer);
	// A refused offer changed nothing.
	CHECK(Session.GetOffer(0).ItemIds.size() == 2 && Session.GetOffer(0).Gold == 50);

	CHECK(Session.Lock(0) == ETradeError::None);
	CHECK(Session.SetOffer(Config, 0, {{1}, 0}, 100, false) == ETradeError::Locked);
	CHECK(Session.Lock(1) == ETradeError::None);
	CHECK(Session.Accept(0) == ETradeError::None);
	CHECK(!Session.IsComplete());
	// Unlocking takes back the acceptance.
	CHECK(Session.Unlock(1) == ETradeError::None);
	CHECK(!Session.HasAccepted(0));
	// Changing an offer unlocks both sides.
	CHECK(Session.SetOffer(Config, 1, {{9, 10}, 0}, 0, false) == ETradeError::None);
	CHECK(!Session.IsLocked(0) && !Session.IsLocked(1));
	CHECK(Session.Lock(0) == ETradeError::None && Session.Lock(1) == ETradeError::None);
	CHECK(Session.Accept(0) == ETradeError::None && Session.Accept(1) == ETradeError::None);
	CHECK(Session.IsComplete());
	CHECK(Session.GetOffer(1).ItemIds.size() == 2);
}

static void TestGuard()
{
	const FGuardConfig Config;
	// A tie: nothing through, nothing back.
	FGuardOutcome Tie = ResolveGuard(Config, 3, 3, false, false);
	CHECK_NEAR(Tie.DefenderDamage, 0.0);
	CHECK_NEAR(Tie.AttackerDamage, 0.0);
	CHECK_NEAR(Tie.AttackerStunSeconds, 0.0);
	CHECK(Tie.bParryBonus);
	// The attacker wins by 1, 2, 3+.
	CHECK_NEAR(ResolveGuard(Config, 4, 3, false, false).DefenderDamage, 0.25);
	CHECK_NEAR(ResolveGuard(Config, 4, 3, false, false).DefenderStunSeconds, 0.0);
	CHECK_NEAR(ResolveGuard(Config, 5, 3, true, false).DefenderDamage, 0.5);
	CHECK_NEAR(ResolveGuard(Config, 5, 3, true, false).DefenderStunSeconds, 0.5);
	const FGuardOutcome Crushed = ResolveGuard(Config, 6, 1, false, false);
	CHECK_NEAR(Crushed.DefenderDamage, 1.0);
	CHECK_NEAR(Crushed.DefenderStunSeconds, 1.0);
	CHECK(!Crushed.bParryBonus);
	CHECK_NEAR(Crushed.AttackerDamage, 0.0);
	// The defender wins (mirror): the attacker takes the loser's share of the defender's weapon.
	const FGuardOutcome Parried = ResolveGuard(Config, 1, 3, false, false);
	CHECK_NEAR(Parried.DefenderDamage, 0.0);
	CHECK_NEAR(Parried.AttackerDamage, 0.5);
	CHECK_NEAR(Parried.AttackerStunSeconds, 0.5);
	CHECK(Parried.bParryBonus);
	// A shield deals nothing back and gives no parry bonus, but still stuns.
	const FGuardOutcome Shielded = ResolveGuard(Config, 1, 4, true, false);
	CHECK_NEAR(Shielded.AttackerDamage, 0.0);
	CHECK_NEAR(Shielded.AttackerStunSeconds, 1.0);
	CHECK(!Shielded.bParryBonus);
	// Piercing ([Triple Thrust]): a stopped hit deals half; a hit that wins by 1 at least half.
	CHECK_NEAR(ResolveGuard(Config, 5, 5, true, true).DefenderDamage, 0.5);
	CHECK_NEAR(ResolveGuard(Config, 5, 4, true, true).DefenderDamage, 0.5);
	CHECK_NEAR(ResolveGuard(Config, 5, 2, true, true).DefenderDamage, 1.0);

	// The guard's surface: hits from in front within the half angle.
	CHECK(HitMeetsGuard(Config, 1, 0, 0, 1, 0, 0));
	CHECK(HitMeetsGuard(Config, 1, 0, 0, 1, 1, 0));
	CHECK(!HitMeetsGuard(Config, 1, 0, 0, 0, 1, 0));
	CHECK(!HitMeetsGuard(Config, 1, 0, 0, -1, 0, 0));
	CHECK(!HitMeetsGuard(Config, 0, 0, 0, 1, 0, 0));
}

static void TestGathering()
{
	const FGatherConfig Config;
	// Placeholders (docs/design-decisions.md, Gathering); the XP is ruled.
	CHECK_NEAR(Config.GatherSeconds, 2.0);
	CHECK_NEAR(Config.RespawnSeconds, 0.0);
	CHECK(XpForSource(FXpConfig(), EXpSource::GatherNode) == 10);

	CHECK(CanStartGather(false, false, false) == EGatherStartError::None);
	CHECK(CanStartGather(true, false, false) == EGatherStartError::Depleted);
	CHECK(CanStartGather(false, true, false) == EGatherStartError::Busy);
	CHECK(CanStartGather(true, true, true) == EGatherStartError::GathererDead);

	// Standing still (within the slack) keeps the channel.
	CHECK(CheckGatherInterrupt(Config, false, 0.0, 0.0, false, true) == EGatherInterrupt::None);
	CHECK(CheckGatherInterrupt(Config, false, Config.MoveTolerance, 0.0, false, true) == EGatherInterrupt::None);
	CHECK(CheckGatherInterrupt(Config, false, Config.MoveTolerance + 1.0, 0.0, false, true) == EGatherInterrupt::Moved);
	CHECK(CheckGatherInterrupt(Config, false, 0.0, 0.5, false, true) == EGatherInterrupt::Damaged);
	// Healing is not damage.
	CHECK(CheckGatherInterrupt(Config, false, 0.0, -5.0, false, true) == EGatherInterrupt::None);
	CHECK(CheckGatherInterrupt(Config, false, 0.0, 0.0, true, true) == EGatherInterrupt::Attacked);
	CHECK(CheckGatherInterrupt(Config, false, 0.0, 0.0, false, false) == EGatherInterrupt::OutOfRange);
	CHECK(CheckGatherInterrupt(Config, true, 500.0, 10.0, true, false) == EGatherInterrupt::Died);
	CHECK(CheckGatherInterrupt(Config, false, 500.0, 10.0, true, false) == EGatherInterrupt::Moved);

	// RespawnSeconds 0: once per raid.
	CHECK(!HasGatherNodeRespawned(Config, 0.0));
	CHECK(!HasGatherNodeRespawned(Config, 1.0e9));
	FGatherConfig Respawning;
	Respawning.RespawnSeconds = 300.0;
	CHECK(!HasGatherNodeRespawned(Respawning, 299.0));
	CHECK(HasGatherNodeRespawned(Respawning, 300.0));
}

static void TestParty()
{
	const FPartyConfig Config;
	CHECK(Config.MaxMembers == FMatchConfig().MaxTeamSize);
	CHECK(Config.MaxMembers == 8);
	// Ruled: invites last 180 s.
	CHECK(Config.InviteExpirySeconds == 180.0);
	const double Expiry = Config.InviteExpirySeconds;

	// Keys and travel URLs.
	CHECK(IsValidPartyKey("0123abcdEF-_"));
	CHECK(IsValidPartyKey(std::string(MaxPartyKeyLength, 'a')));
	CHECK(!IsValidPartyKey(std::string(MaxPartyKeyLength + 1, 'a')));
	CHECK(!IsValidPartyKey(""));
	CHECK(!IsValidPartyKey("a?b"));
	CHECK(!IsValidPartyKey("a=b"));
	CHECK(!IsValidPartyKey("a b"));
	CHECK(!IsValidPartyKey("a#b"));
	CHECK(BuildRaidTravelUrl("/Game/Maps/Raid", "abc") == "/Game/Maps/Raid?Party=abc");
	CHECK(BuildRaidTravelUrl("203.0.113.5:7777", "abc") == "203.0.113.5:7777?Party=abc");
	CHECK(BuildRaidTravelUrl("/Game/Maps/Raid", "") == "/Game/Maps/Raid");
	CHECK(BuildRaidTravelUrl("/Game/Maps/Raid", "abc", {"listen"}) == "/Game/Maps/Raid?Party=abc?listen");
	CHECK(BuildRaidTravelUrl("/Game/Maps/Raid", "", {"listen"}) == "/Game/Maps/Raid?listen");
	CHECK(BuildRaidTravelUrl("", "abc").empty());
	CHECK(BuildRaidTravelUrl("/Game/Maps/Raid", "a?Party=b").empty());
	CHECK(BuildRaidTravelUrl("/Game/Maps/Raid", "abc", {"x=y"}).empty());

	// Travel choice.
	FRaidTravelSituation Situation;
	CHECK(PickRaidTravel(Situation) == ERaidTravel::NoDestination);
	Situation.bWholeServerInParty = true;
	CHECK(PickRaidTravel(Situation) == ERaidTravel::NoDestination);
	Situation.bHasRaidMap = true;
	CHECK(PickRaidTravel(Situation) == ERaidTravel::ServerTravel);
	Situation.bListenHostInParty = true;
	CHECK(PickRaidTravel(Situation) == ERaidTravel::ServerTravel);
	Situation.bWholeServerInParty = false;
	CHECK(PickRaidTravel(Situation) == ERaidTravel::NoDestination);
	Situation.bHasRaidServer = true;
	CHECK(PickRaidTravel(Situation) == ERaidTravel::HostInParty);
	Situation.bListenHostInParty = false;
	CHECK(PickRaidTravel(Situation) == ERaidTravel::ClientTravel);
	Situation.bHasRaidMap = false;
	Situation.bWholeServerInParty = true;
	CHECK(PickRaidTravel(Situation) == ERaidTravel::ClientTravel);

	// Create.
	FPartyRegistry Parties;
	CHECK(Parties.Create("p1", "lead") == EPartyError::None);
	const FParty* P1 = Parties.Find("p1");
	CHECK(P1 != nullptr && P1->LeaderId == "lead" && P1->Members.size() == 1 && P1->IsMember("lead"));
	CHECK(Parties.PartyOf("lead") == P1);
	CHECK(Parties.IsLeader("lead"));
	CHECK(Parties.Create("p1", "other") == EPartyError::PartyIdTaken);
	CHECK(Parties.Create("p2", "lead") == EPartyError::AlreadyInParty);
	CHECK(Parties.Create("p2", "") == EPartyError::InvalidPlayer);
	CHECK(Parties.Create("", "other") == EPartyError::InvalidPartyId);
	CHECK(Parties.Create("bad key", "other") == EPartyError::InvalidPartyId);
	CHECK(Parties.Find("p2") == nullptr && Parties.PartyOf("other") == nullptr);

	// Invite and accept.
	CHECK(Parties.Invite("lead", "b", 0.0) == EPartyError::None);
	CHECK(Parties.Invite("lead", "b", 1.0) == EPartyError::AlreadyInvited);
	CHECK(Parties.Invite("lead", "lead", 1.0) == EPartyError::CannotTargetSelf);
	CHECK(Parties.Invite("lead", "", 1.0) == EPartyError::InvalidPlayer);
	CHECK(Parties.Invite("", "b", 1.0) == EPartyError::InvalidPlayer);
	CHECK(Parties.InvitesFor("b", 1.0).size() == 1 && Parties.InvitesFor("b", 1.0)[0] == P1);
	CHECK(Parties.InvitesFor("b", Expiry).empty());
	CHECK(Parties.Accept("p1", "b", 1.0) == EPartyError::None);
	CHECK(P1->Members.size() == 2 && P1->IsMember("b") && P1->Invites.empty());
	CHECK(P1->LeaderId == "lead" && !Parties.IsLeader("b"));
	CHECK(Parties.Accept("p1", "b", 1.0) == EPartyError::NoInvite);
	CHECK(Parties.Invite("lead", "b", 1.0) == EPartyError::AlreadyInParty);
	// Only the leader invites.
	CHECK(Parties.Invite("b", "c", 1.0) == EPartyError::NotLeader);
	CHECK(Parties.Invite("b", "c", 1.0, "p9") == EPartyError::NotLeader);
	CHECK(Parties.Find("p9") == nullptr);
	CHECK(Parties.Accept("nope", "c", 1.0) == EPartyError::UnknownParty);
	CHECK(Parties.Accept("p1", "", 1.0) == EPartyError::InvalidPlayer);

	// Expiry: a failed accept keeps the invite; re-inviting replaces it; pruning drops it.
	CHECK(Parties.Invite("lead", "c", 10.0) == EPartyError::None);
	CHECK(Parties.Accept("p1", "c", 10.0 + Expiry) == EPartyError::InviteExpired);
	CHECK(Parties.PartyOf("c") == nullptr && P1->Invites.size() == 1);
	CHECK(Parties.Invite("lead", "c", 10.0 + Expiry) == EPartyError::None);
	CHECK(P1->Invites.size() == 1 && P1->Invites[0].ExpiresAt == 10.0 + 2.0 * Expiry);
	CHECK(Parties.Accept("p1", "c", 10.0 + 2.0 * Expiry - 0.5) == EPartyError::None);
	CHECK(Parties.Invite("lead", "d", 100.0) == EPartyError::None);
	CHECK(Parties.PruneExpiredInvites(100.0 + Expiry - 1.0) == 0);
	CHECK(Parties.PruneExpiredInvites(100.0 + Expiry) == 1);
	CHECK(Parties.Accept("p1", "d", 100.0) == EPartyError::NoInvite);

	// Decline.
	CHECK(Parties.Invite("lead", "d", 200.0) == EPartyError::None);
	CHECK(Parties.Decline("p1", "d") == EPartyError::None);
	CHECK(Parties.Decline("p1", "d") == EPartyError::NoInvite);
	CHECK(Parties.Decline("nope", "d") == EPartyError::UnknownParty);
	CHECK(Parties.Accept("p1", "d", 200.0) == EPartyError::NoInvite);

	// The invite inbox: two parties invite "x"; declining one keeps the other; DeclineAll clears the
	// rest (expired ones too) and leaves other players' invites alone.
	{
		FPartyRegistry Inbox;
		CHECK(Inbox.Create("i1", "l1") == EPartyError::None);
		CHECK(Inbox.Create("i2", "l2") == EPartyError::None);
		CHECK(Inbox.Invite("l1", "x", 0.0) == EPartyError::None);
		CHECK(Inbox.Invite("l2", "x", 0.0) == EPartyError::None);
		CHECK(Inbox.Invite("l1", "y", 0.0) == EPartyError::None);
		CHECK(Inbox.InvitesFor("x", 1.0).size() == 2);
		CHECK(Inbox.Decline("i1", "x") == EPartyError::None);
		CHECK(Inbox.InvitesFor("x", 1.0).size() == 1 && Inbox.InvitesFor("x", 1.0)[0]->Id == "i2");
		CHECK(Inbox.Invite("l1", "x", 1.0) == EPartyError::None);
		// Both open invites go (DeclineAll ignores expiry).
		CHECK(Inbox.DeclineAll("x") == 2);
		CHECK(Inbox.InvitesFor("x", 0.0).empty());
		CHECK(Inbox.DeclineAll("x") == 0);
		CHECK(Inbox.InvitesFor("y", 1.0).size() == 1);
		CHECK(Inbox.Accept("i2", "x", 1.0) == EPartyError::NoInvite);
		// Declining changes no party.
		CHECK(Inbox.Find("i1")->Members.size() == 1 && Inbox.Find("i2")->Members.size() == 1);
	}

	// A solo player's invite founds a party, only if the invite succeeds.
	CHECK(Parties.Invite("solo", "b", 300.0) == EPartyError::NotInParty);
	CHECK(Parties.Invite("solo", "b", 300.0, "p2") == EPartyError::AlreadyInParty);
	CHECK(Parties.Find("p2") == nullptr && Parties.PartyOf("solo") == nullptr);
	CHECK(Parties.Invite("solo", "e", 300.0, "p1") == EPartyError::PartyIdTaken);
	CHECK(Parties.Invite("solo", "e", 300.0, "bad key") == EPartyError::InvalidPartyId);
	CHECK(Parties.Invite("solo", "solo", 300.0, "p2") == EPartyError::CannotTargetSelf);
	CHECK(Parties.Find("p2") == nullptr);
	CHECK(Parties.Invite("solo", "e", 300.0, "p2") == EPartyError::None);
	const FParty* P2 = Parties.Find("p2");
	CHECK(P2 != nullptr && P2->LeaderId == "solo" && P2->Members.size() == 1 && P2->Invites.size() == 1);
	// An actor already in a party ignores NewPartyId.
	CHECK(Parties.Invite("lead", "f", 300.0, "p3") == EPartyError::None);
	CHECK(Parties.Find("p3") == nullptr);
	// Invites from two parties; accepting one drops the other.
	CHECK(Parties.Invite("solo", "f", 300.0) == EPartyError::None);
	CHECK(Parties.InvitesFor("f", 300.0).size() == 2);
	CHECK(Parties.Accept("p2", "f", 300.0) == EPartyError::None);
	CHECK(Parties.InvitesFor("f", 300.0).empty());
	CHECK(Parties.Accept("p1", "f", 300.0) == EPartyError::NoInvite);
	CHECK(P2->Members.size() == 2);

	// Kick and promote: leader only, members only, not oneself.
	CHECK(Parties.Kick("b", "c") == EPartyError::NotLeader);
	CHECK(Parties.Kick("lead", "lead") == EPartyError::CannotTargetSelf);
	CHECK(Parties.Kick("lead", "f") == EPartyError::TargetNotInParty);
	CHECK(Parties.Kick("nobody", "b") == EPartyError::NotInParty);
	CHECK(Parties.Kick("lead", "c") == EPartyError::None);
	CHECK(Parties.PartyOf("c") == nullptr && !P1->IsMember("c") && P1->Members.size() == 2);
	CHECK(Parties.Promote("b", "lead") == EPartyError::NotLeader);
	CHECK(Parties.Promote("lead", "lead") == EPartyError::CannotTargetSelf);
	CHECK(Parties.Promote("lead", "c") == EPartyError::TargetNotInParty);
	CHECK(Parties.Promote("lead", "b") == EPartyError::None);
	CHECK(Parties.IsLeader("b") && !Parties.IsLeader("lead") && P1->Members.size() == 2);
	CHECK(Parties.Kick("lead", "b") == EPartyError::NotLeader);

	// Raid launch: leader or solo.
	CHECK(Parties.CanStartRaid("b") == EPartyError::None);
	CHECK(Parties.CanStartRaid("lead") == EPartyError::NotLeader);
	CHECK(Parties.CanStartRaid("solo-player") == EPartyError::None);
	CHECK(Parties.CanStartRaid("") == EPartyError::InvalidPlayer);

	// Leave: a leader leaving hands over to the longest-standing member; the last one disbands.
	CHECK(Parties.Invite("b", "g", 400.0) == EPartyError::None);
	CHECK(Parties.Accept("p1", "g", 400.0) == EPartyError::None);
	CHECK(P1->Members.size() == 3 && P1->Members[0] == "lead" && P1->Members[2] == "g");
	FPartyLeaveResult Left = Parties.Leave("b");
	CHECK(Left.IsValid() && Left.PartyId == "p1" && !Left.bDisbanded && Left.NewLeaderId == "lead");
	CHECK(Parties.IsLeader("lead") && Parties.PartyOf("b") == nullptr);
	Left = Parties.Leave("g");
	CHECK(Left.IsValid() && !Left.bDisbanded && Left.NewLeaderId.empty() && Parties.IsLeader("lead"));
	CHECK(Parties.Leave("g").Error == EPartyError::NotInParty);
	CHECK(Parties.Invite("lead", "h", 500.0) == EPartyError::None);
	Left = Parties.Leave("lead");
	CHECK(Left.IsValid() && Left.bDisbanded && Left.PartyId == "p1" && Left.NewLeaderId.empty());
	CHECK(Parties.Find("p1") == nullptr && Parties.PartyOf("lead") == nullptr);
	// Its invites went with it; the id is free again.
	CHECK(Parties.InvitesFor("h", 500.0).empty());
	CHECK(Parties.Accept("p1", "h", 500.0) == EPartyError::UnknownParty);
	CHECK(Parties.Create("p1", "lead") == EPartyError::None);

	// Size cap: 8 members; a pending invite does not reserve a place.
	FPartyRegistry Full;
	CHECK(Full.Create("full", "m0") == EPartyError::None);
	for (int I = 1; I < 8; ++I)
	{
		const std::string Member = "m" + std::to_string(I);
		CHECK(Full.Invite("m0", Member, 0.0) == EPartyError::None);
		CHECK(Full.Accept("full", Member, 0.0) == EPartyError::None);
	}
	CHECK(Full.Find("full")->Members.size() == 8);
	CHECK(Full.Invite("m0", "m8", 0.0) == EPartyError::PartyFull);
	CHECK(Full.Leave("m7").IsValid());
	CHECK(Full.Invite("m0", "m8", 0.0) == EPartyError::None);
	CHECK(Full.Invite("m0", "m9", 0.0) == EPartyError::None);
	CHECK(Full.Accept("full", "m8", 0.0) == EPartyError::None);
	CHECK(Full.Accept("full", "m9", 0.0) == EPartyError::PartyFull);
	CHECK(Full.PartyOf("m9") == nullptr);
	CHECK(Full.Join("full", "m9") == EPartyError::PartyFull);

	// Join by key (a team back from the raid early rebuilds its party on its leader's hub): no
	// invite needed, the founder stays leader, other invites go.
	FPartyRegistry Rebuilt;
	CHECK(Rebuilt.Create("team", "leader") == EPartyError::None);
	CHECK(Rebuilt.Create("other", "x") == EPartyError::None);
	CHECK(Rebuilt.Invite("x", "member", 0.0) == EPartyError::None);
	CHECK(Rebuilt.Join("team", "member") == EPartyError::None);
	CHECK(Rebuilt.Find("team")->Members.size() == 2 && Rebuilt.Find("team")->LeaderId == "leader");
	CHECK(Rebuilt.Find("other")->Invites.empty());
	CHECK(Rebuilt.Join("team", "member") == EPartyError::AlreadyInParty);
	CHECK(Rebuilt.Join("other", "member") == EPartyError::AlreadyInParty);
	CHECK(Rebuilt.Join("nope", "y") == EPartyError::UnknownParty);
	CHECK(Rebuilt.Join("team", "") == EPartyError::InvalidPlayer);

	// The cap is configurable; a cap of 1 refuses every invite (and founds nothing).
	FPartyConfig Solo;
	Solo.MaxMembers = 1;
	FPartyRegistry SoloParties(Solo);
	CHECK(SoloParties.Invite("a", "b", 0.0, "s1") == EPartyError::PartyFull);
	CHECK(SoloParties.Find("s1") == nullptr);
}

static void TestJson()
{
	FJsonNode Node;
	CHECK(ParseJson("{\"a\": 1, \"b\": [true, false, null], \"c\": \"x\", \"d\": -0.5, \"e\": 1e3}", Node));
	CHECK(Node.IsObject());
	CHECK(Node.Keys().size() == 5);
	CHECK(Node.Find("a") != nullptr && Node.Find("a")->IsInteger() && Node.Find("a")->AsInt() == 1);
	CHECK(Node.Find("b") != nullptr && Node.Find("b")->Items().size() == 3 && Node.Find("b")->Items()[0].AsBool()
		&& Node.Find("b")->Items()[2].IsNull());
	CHECK(Node.Find("c") != nullptr && Node.Find("c")->AsString() == "x");
	CHECK(Node.Find("d") != nullptr && Node.Find("d")->Type() == FJsonNode::EType::Real);
	CHECK_NEAR(Node.Find("d")->AsReal(), -0.5);
	// An exponent makes a real, even when whole.
	CHECK(Node.Find("e") != nullptr && Node.Find("e")->Type() == FJsonNode::EType::Real);
	CHECK_NEAR(Node.Find("e")->AsReal(), 1000.0);
	CHECK(Node.Find("missing") == nullptr);

	// Integers stay exact across the whole int64 range.
	CHECK(ParseJson("[9223372036854775807, -9223372036854775808, 0]", Node));
	CHECK(Node.Items()[0].AsInt() == INT64_MAX);
	CHECK(Node.Items()[1].AsInt() == INT64_MIN);
	// Past int64: a real.
	CHECK(ParseJson("9223372036854775808", Node) && Node.Type() == FJsonNode::EType::Real);

	// Reals read back exactly as written.
	for (const double Value : {0.03, 0.1, 1.0 / 3.0, 2.5e-7, 123456.789, -4.0e10, 0.020000000298023224})
	{
		FJsonNode Back;
		CHECK(ParseJson(WriteJson(FJsonNode::MakeReal(Value), false), Back));
		CHECK(Back.AsReal() == Value);
	}
	CHECK(WriteJson(FJsonNode::MakeReal(0.1), false) == "0.1");
	CHECK(ParseJson("1e-999", Node) && Node.AsReal() == 0.0);
	CHECK(!ParseJson("1e999", Node));

	// Escapes and UTF-8, both ways.
	CHECK(ParseJson("\"a\\\"b\\\\c\\n\\u00e9\\ud83d\\ude00\"", Node));
	CHECK(Node.AsString() == "a\"b\\c\n\xC3\xA9\xF0\x9F\x98\x80");
	FJsonNode Back;
	CHECK(ParseJson(WriteJson(Node), Back) && Back.AsString() == Node.AsString());
	CHECK(WriteJson(FJsonNode::MakeString(std::string("\x01", 1)), false) == "\"\\u0001\"");

	// Objects keep key order and replace on Set.
	FJsonNode Object = FJsonNode::MakeObject();
	Object.Set("z", FJsonNode::MakeInt(1));
	Object.Set("a", FJsonNode::MakeInt(2));
	Object.Set("z", FJsonNode::MakeInt(3));
	CHECK(WriteJson(Object, false) == "{\"z\":3,\"a\":2}");
	CHECK(Object.Remove("z") && !Object.Remove("z") && Object.Keys().size() == 1);

	// Write -> parse -> write is stable.
	const std::string Text = "{\"list\": [1, 2.5, \"x\"], \"nested\": {\"empty\": [], \"obj\": {}}}";
	CHECK(ParseJson(Text, Node));
	const std::string Written = WriteJson(Node);
	CHECK(ParseJson(Written, Back) && WriteJson(Back) == Written);

	// A leading BOM is fine.
	CHECK(ParseJson("\xEF\xBB\xBF{}", Node) && Node.IsObject());

	// Refused.
	FJsonParseError Error;
	CHECK(!ParseJson("", Node, &Error) && !Error.Message.empty());
	CHECK(!ParseJson("{\"a\": 1, \"a\": 2}", Node, &Error) && Error.Message == "duplicate key");
	CHECK(!ParseJson("[1, 2,]", Node));
	CHECK(!ParseJson("{\"a\": 1,}", Node));
	CHECK(!ParseJson("{} {}", Node));
	CHECK(!ParseJson("01", Node));
	CHECK(!ParseJson("1.", Node));
	CHECK(!ParseJson("-", Node));
	CHECK(!ParseJson("nul", Node));
	CHECK(!ParseJson("\"tab\there\"", Node));
	CHECK(!ParseJson("\"\\ud83d\"", Node));
	CHECK(!ParseJson("\"\\x\"", Node));
	CHECK(!ParseJson("\"unterminated", Node));
	CHECK(!ParseJson("\"\xC3\"", Node));
	CHECK(!ParseJson("\"\xC0\xAF\"", Node));
	CHECK(!ParseJson("{\"a\" 1}", Node));
	// Too deep.
	CHECK(!ParseJson(std::string(100, '[') + std::string(100, ']'), Node));
	CHECK(ParseJson(std::string(10, '[') + std::string(10, ']'), Node, nullptr, 10));
	CHECK(!ParseJson(std::string(12, '[') + std::string(12, ']'), Node, nullptr, 10));
	// A failed parse leaves the output alone.
	FJsonNode Kept = FJsonNode::MakeInt(7);
	CHECK(!ParseJson("[", Kept) && Kept.AsInt() == 7);
}

namespace SaveTests
{
	FSavedItem MakeItem(EItemCategory Category, const char* Id, ERarity Rarity, int Count, FGridSize Size, FGridPlacement At)
	{
		FSavedItem Item;
		Item.Category = Category;
		Item.ItemId = Id;
		Item.Rarity = Rarity;
		Item.Count = Count;
		Item.Size = Size;
		Item.Placement = At;
		return Item;
	}

	FCharacterSave MakeFullSave()
	{
		FCharacterSave Save;
		Save.PlayerId = "76561198000000001";
		Save.SavedAt = 1760000000;
		// Level 8 (51 points).
		Save.TotalXp = 500;
		Save.Attributes = {20, 15, 10};
		Save.RespecTokens = 2;
		Save.SkillLibrary = {"fireball", "heal", "slash"};
		Save.EquippedSkills = {"heal", "fireball"};
		Save.Gold = 4321;
		Save.PurchasedStashTabs = 1;
		Save.StashTabs.resize(2);

		FSavedItem Sword = MakeItem(EItemCategory::Weapon, "sword_one_handed", ERarity::Rare, 1, {1, 3}, {0, 0, false});
		Sword.Affixes = {{"additional_attack_damage", 2.0}, {"action_speed", 0.03}};
		Save.StashTabs[0].Items.push_back(Sword);
		Save.StashTabs[0].Items.push_back(MakeItem(EItemCategory::Consumable, "healing_potion", ERarity::Uncommon, 4, {1, 1}, {3, 4, false}));
		Save.StashTabs[1].Items.push_back(MakeItem(EItemCategory::Ammo, "arrow", ERarity::Common, 20, {1, 3}, {5, 10, true}));
		FSavedItem Backpack = MakeItem(EItemCategory::Container, "backpack", ERarity::Epic, 1, {2, 2}, {0, 0, false});
		Backpack.ContainerKind = EContainerKind::Backpack;
		Save.StashTabs[1].Items.push_back(Backpack);

		FSavedItem Axe = MakeItem(EItemCategory::Weapon, "axe_one_handed", ERarity::Common, 1, {1, 3}, {});
		Axe.Worn = ESavedWorn::MainHand;
		Axe.WornSet = 1;
		Save.Carried.push_back(Axe);
		FSavedItem Helm = MakeItem(EItemCategory::Armor, "plate_helm", ERarity::Legendary, 1, {2, 2}, {});
		Helm.Affixes = {{"max_health", 3.0}};
		Helm.Worn = ESavedWorn::Armor;
		Save.Carried.push_back(Helm);
		FSavedItem Quiver = MakeItem(EItemCategory::Container, "quiver", ERarity::Common, 1, {2, 2}, {});
		Quiver.ContainerKind = EContainerKind::Quiver;
		Quiver.Worn = ESavedWorn::Container;
		Save.Carried.push_back(Quiver);
		FSavedItem Arrows = MakeItem(EItemCategory::Ammo, "arrow", ERarity::Rare, 15, {1, 3}, {2, 0, false});
		Arrows.Grid = EContainerKind::Quiver;
		Save.Carried.push_back(Arrows);
		Save.Carried.push_back(MakeItem(EItemCategory::Consumable, "healing_potion", ERarity::Common, 3, {1, 1}, {2, 1, false}));

		Save.QuarantinedItems.push_back(MakeItem(EItemCategory::Trinket, "old_amulet", ERarity::Mystic, 1, {1, 1}, {}));
		Save.QuarantinedSkills = {"old_skill"};
		return Save;
	}

	// The game data the tests pretend to have.
	bool LookUp(const FSavedItem& Item, FSavedItemInfo& Out)
	{
		const std::string& Id = Item.ItemId;
		if (Id == "sword_one_handed" || Id == "axe_one_handed")
		{
			Out = {{1, 3}, 1};
		}
		else if (Id == "healing_potion")
		{
			Out = {{1, 1}, 5};
		}
		else if (Id == "arrow")
		{
			Out = {{1, 3}, 20};
		}
		else if (Id == "backpack" || Id == "quiver" || Id == "plate_helm")
		{
			Out = {{2, 2}, 1};
		}
		else
		{
			return false;
		}
		return true;
	}

	bool KnownSkill(const std::string& Id)
	{
		return Id == "fireball" || Id == "heal" || Id == "slash" || Id == "cleave";
	}

	FSaveValidationRules GameRules()
	{
		FSaveValidationRules Rules;
		Rules.LookUpItem = LookUp;
		Rules.IsKnownSkill = KnownSkill;
		return Rules;
	}

	int CountIssues(const std::vector<FSaveIssue>& Issues, ESaveIssue Kind)
	{
		int Count = 0;
		for (const FSaveIssue& Issue : Issues)
		{
			Count += Issue.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	// No two items of a tab share a square and every item lies inside its tab.
	bool StashIsConsistent(const FCharacterSave& Save, const FInventoryConfig& Config)
	{
		for (const FSavedStashTab& Tab : Save.StashTabs)
		{
			FInventoryGrid Grid(FGridSize{Config.StashTabWidth, Config.StashTabHeight});
			int Id = 1;
			for (const FSavedItem& Item : Tab.Items)
			{
				if (!Grid.Place(Id++, Item.Size, Item.Placement))
				{
					return false;
				}
			}
		}
		return true;
	}

	// The full save with one change made through Edit on its parsed JSON.
	template <typename EditFunction>
	std::string EditedDocument(EditFunction Edit)
	{
		FJsonNode Root;
		ParseJson(WriteCharacterSave(MakeFullSave()), Root);
		Edit(Root);
		return WriteJson(Root);
	}
}

static void TestSaveRoundTrip()
{
	using namespace SaveTests;
	const FCharacterSave Original = MakeFullSave();
	const std::string Document = WriteCharacterSave(Original);

	const FSaveParseResult Parsed = ParseCharacterSave(Document, Original.PlayerId);
	CHECK(Parsed.IsValid());
	CHECK(Parsed.LoadedVersion == CurrentSaveVersion);
	CHECK(!Parsed.bMigrated);
	// Byte for byte the same document again.
	CHECK(WriteCharacterSave(Parsed.Save) == Document);

	const FCharacterSave& Save = Parsed.Save;
	CHECK(Save.Version == CurrentSaveVersion);
	CHECK(Save.PlayerId == "76561198000000001");
	CHECK(Save.SavedAt == 1760000000);
	CHECK(Save.TotalXp == 500);
	CHECK(Save.Attributes.Str == 20 && Save.Attributes.Agi == 15 && Save.Attributes.Int == 10);
	CHECK(Save.RespecTokens == 2);
	CHECK(Save.SkillLibrary == Original.SkillLibrary);
	CHECK(Save.EquippedSkills == Original.EquippedSkills);
	CHECK(Save.Gold == 4321);
	CHECK(Save.PurchasedStashTabs == 1);
	CHECK(Save.StashTabs.size() == 2);
	if (Save.StashTabs.size() == 2 && Save.StashTabs[0].Items.size() == 2 && Save.StashTabs[1].Items.size() == 2)
	{
		const FSavedItem& Sword = Save.StashTabs[0].Items[0];
		CHECK(Sword.Category == EItemCategory::Weapon && Sword.ItemId == "sword_one_handed" && Sword.Rarity == ERarity::Rare);
		CHECK(Sword.Affixes.size() == 2 && Sword.Affixes[0].Id == "additional_attack_damage");
		CHECK(Sword.Affixes.size() == 2 && Sword.Affixes[1].Value == 0.03);
		CHECK(Sword.Size.Width == 1 && Sword.Size.Height == 3);
		const FSavedItem& Potions = Save.StashTabs[0].Items[1];
		CHECK(Potions.Count == 4 && Potions.Rarity == ERarity::Uncommon && Potions.Placement.X == 3 && Potions.Placement.Y == 4);
		const FSavedItem& Arrows = Save.StashTabs[1].Items[0];
		CHECK(Arrows.Count == 20 && Arrows.Placement.bRotated && Arrows.Placement.X == 5 && Arrows.Placement.Y == 10);
		CHECK(Save.StashTabs[1].Items[1].ContainerKind == EContainerKind::Backpack);
	}
	CHECK(!Save.bInRaid);
	CHECK(Save.Carried.size() == 5);
	if (Save.Carried.size() == 5)
	{
		CHECK(Save.Carried[0].Worn == ESavedWorn::MainHand && Save.Carried[0].WornSet == 1);
		CHECK(Save.Carried[1].Worn == ESavedWorn::Armor && Save.Carried[1].Affixes.size() == 1);
		CHECK(Save.Carried[2].Worn == ESavedWorn::Container && Save.Carried[2].ContainerKind == EContainerKind::Quiver);
		CHECK(Save.Carried[3].Grid == EContainerKind::Quiver && Save.Carried[3].Count == 15);
		CHECK(Save.Carried[4].Worn == ESavedWorn::None && Save.Carried[4].Grid == EContainerKind::Main);
	}
	CHECK(Save.QuarantinedItems.size() == 1 && Save.QuarantinedItems[0].ItemId == "old_amulet");
	CHECK(Save.QuarantinedSkills.size() == 1 && Save.QuarantinedSkills[0] == "old_skill");

	// A valid save needs no repair (apart from the quarantine, still unknown).
	FCharacterSave Valid = Parsed.Save;
	const std::vector<FSaveIssue> Issues = ValidateCharacterSave(Valid, GameRules());
	CHECK(Issues.empty());
	CHECK(WriteCharacterSave(Valid) == Document);

	// An empty character round-trips too.
	FCharacterSave Empty;
	Empty.PlayerId = "session-256";
	const FSaveParseResult EmptyParsed = ParseCharacterSave(WriteCharacterSave(Empty), "session-256");
	CHECK(EmptyParsed.IsValid() && WriteCharacterSave(EmptyParsed.Save) == WriteCharacterSave(Empty));

	// Odd characters in ids survive.
	FCharacterSave Odd = MakeFullSave();
	Odd.PlayerId = "player \"one\"\\\xC3\xA9";
	const FSaveParseResult OddParsed = ParseCharacterSave(WriteCharacterSave(Odd), Odd.PlayerId);
	CHECK(OddParsed.IsValid() && OddParsed.Save.PlayerId == Odd.PlayerId);
}

static void TestSaveRefusesCorruptDocuments()
{
	using namespace SaveTests;
	const std::string Document = WriteCharacterSave(MakeFullSave());

	CHECK(ParseCharacterSave("").Error == ESaveLoadError::InvalidJson);
	CHECK(ParseCharacterSave("not json").Error == ESaveLoadError::InvalidJson);
	// Cut off halfway (a crash during a write).
	CHECK(ParseCharacterSave(Document.substr(0, Document.size() / 2)).Error == ESaveLoadError::InvalidJson);
	CHECK(ParseCharacterSave("[]").Error == ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave("{}").Error == ESaveLoadError::InvalidField);

	// Another player's file.
	const FSaveParseResult Wrong = ParseCharacterSave(Document, "76561198000000002");
	CHECK(Wrong.Error == ESaveLoadError::WrongPlayer);
	CHECK(Wrong.Save.Gold == 0 && Wrong.Save.StashTabs.empty());

	// Missing and mistyped fields.
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Remove("gold"); })).Error == ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Set("gold", FJsonNode::MakeString("100")); })).Error
		== ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Remove("stash"); })).Error == ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Find("progression")->Set("totalXp", FJsonNode::MakeReal(1.5)); })).Error
		== ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Find("progression")->Set("totalXp", FJsonNode::MakeInt(3000000000LL)); }))
			  .Error
		== ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Find("progression")->Set("skillLibrary", FJsonNode::MakeInt(1)); })).Error
		== ESaveLoadError::InvalidField);

	// A broken item refuses the whole document.
	const auto EditFirstItem = [](const char* Key, FJsonNode Value)
	{
		return EditedDocument([&](FJsonNode& Root)
		{
			Root.Find("stash")->Find("tabs")->At(0)->Find("items")->At(0)->Set(Key, Value);
		});
	};
	CHECK(ParseCharacterSave(EditFirstItem("rarity", FJsonNode::MakeString("shiny"))).Error == ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditFirstItem("category", FJsonNode::MakeString("spaceship"))).Error == ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditFirstItem("count", FJsonNode::MakeString("1"))).Error == ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditFirstItem("size", FJsonNode::MakeInt(3))).Error == ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditFirstItem("worn", FJsonNode::MakeString("head"))).Error == ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditFirstItem("id", FJsonNode::MakeString(std::string(500, 'x')))).Error == ESaveLoadError::InvalidField);

	// Versions: newer refused; older without a migration refused.
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Set("version", FJsonNode::MakeInt(CurrentSaveVersion + 1)); })).Error
		== ESaveLoadError::UnsupportedVersion);
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Set("version", FJsonNode::MakeInt(0)); })).Error
		== ESaveLoadError::UnsupportedVersion);
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Set("version", FJsonNode::MakeInt(-1)); })).Error
		== ESaveLoadError::InvalidField);
	CHECK(ParseCharacterSave(EditedDocument([](FJsonNode& Root) { Root.Remove("version"); })).Error == ESaveLoadError::InvalidField);

	// Size limits.
	FSaveLimits Small;
	Small.MaxDocumentBytes = 100;
	CHECK(ParseCharacterSave(Document, std::string(), DefaultSaveMigrations(), Small).Error == ESaveLoadError::TooLarge);
	FSaveLimits FewSkills;
	FewSkills.MaxSkills = 2;
	CHECK(ParseCharacterSave(Document, std::string(), DefaultSaveMigrations(), FewSkills).Error == ESaveLoadError::TooManyEntries);
	FSaveLimits FewItems;
	FewItems.MaxItemsPerTab = 1;
	CHECK(ParseCharacterSave(Document, std::string(), DefaultSaveMigrations(), FewItems).Error == ESaveLoadError::TooManyEntries);
	CHECK(ParseCharacterSave("{\"version\": 1, \"playerId\": " + std::string(40, '[') + std::string(40, ']') + "}").Error
		== ESaveLoadError::InvalidJson);
}

static void TestSaveMigration()
{
	using namespace SaveTests;
	// A made-up version 0 that called gold "money".
	FSaveMigrations Migrations;
	Migrations.CurrentVersion = 1;
	Migrations.Steps.push_back({0, [](FJsonNode& Root)
	{
		const FJsonNode* Money = Root.Find("money");
		if (Money == nullptr)
		{
			return false;
		}
		const FJsonNode Gold = *Money;
		Root.Remove("money");
		Root.Set("gold", Gold);
		return true;
	}});

	const std::string Old = EditedDocument([](FJsonNode& Root)
	{
		Root.Set("version", FJsonNode::MakeInt(0));
		const FJsonNode Gold = *Root.Find("gold");
		Root.Remove("gold");
		Root.Set("money", Gold);
	});
	const FSaveParseResult Migrated = ParseCharacterSave(Old, std::string(), Migrations);
	CHECK(Migrated.IsValid());
	CHECK(Migrated.bMigrated && Migrated.LoadedVersion == 0 && Migrated.Save.Version == 1);
	CHECK(Migrated.Save.Gold == 4321);
	// Written back in the current format.
	CHECK(WriteCharacterSave(Migrated.Save) == WriteCharacterSave(MakeFullSave()));

	// The step refuses a document it cannot handle.
	const std::string Broken = EditedDocument([](FJsonNode& Root) { Root.Set("version", FJsonNode::MakeInt(0)); });
	CHECK(ParseCharacterSave(Broken, std::string(), Migrations).Error == ESaveLoadError::MigrationFailed);
	// The current version needs no step.
	CHECK(ParseCharacterSave(WriteCharacterSave(MakeFullSave()), std::string(), Migrations).IsValid());
}

static void TestSaveValidationClamps()
{
	using namespace SaveTests;
	const FSaveValidationRules Rules = GameRules();

	FCharacterSave Save = MakeFullSave();
	Save.TotalXp = -10;
	Save.Gold = -5;
	Save.RespecTokens = -1;
	Save.PurchasedStashTabs = 7;
	std::vector<FSaveIssue> Issues = ValidateCharacterSave(Save, Rules);
	CHECK(Save.TotalXp == 0 && CountIssues(Issues, ESaveIssue::NegativeXp) == 1);
	CHECK(Save.Gold == 0 && CountIssues(Issues, ESaveIssue::GoldOutOfRange) == 1);
	CHECK(Save.RespecTokens == 0 && CountIssues(Issues, ESaveIssue::NegativeRespecTokens) == 1);
	CHECK(Save.PurchasedStashTabs == 3 && Save.StashTabs.size() == 4 && CountIssues(Issues, ESaveIssue::StashTabsOutOfRange) == 1);
	// 45 points at level 1 (30): all unassigned.
	CHECK(Save.Attributes.Total() == 0 && CountIssues(Issues, ESaveIssue::InvalidAllocation) == 1);

	FCharacterSave Rich = MakeFullSave();
	Rich.Gold = 5000000000LL;
	ValidateCharacterSave(Rich, Rules);
	CHECK(Rich.Gold == 2147483647);
	FCharacterSave Negative = MakeFullSave();
	Negative.Attributes = {-1, 10, 10};
	ValidateCharacterSave(Negative, Rules);
	CHECK(Negative.Attributes.Total() == 0);
	// The values that came out load again unchanged.
	const FSaveParseResult Again = ParseCharacterSave(WriteCharacterSave(Save));
	CHECK(Again.IsValid());
	FCharacterSave Reloaded = Again.Save;
	CHECK(ValidateCharacterSave(Reloaded, Rules).empty());
}

static void TestSaveValidationSkills()
{
	using namespace SaveTests;
	FCharacterSave Save = MakeFullSave();
	Save.SkillLibrary = {"fireball", "heal", "fireball", "", "removed_skill"};
	Save.EquippedSkills = {"heal", "slash", "heal", "removed_skill"};
	Save.QuarantinedSkills = {"cleave", "old_skill", "heal"};
	const std::vector<FSaveIssue> Issues = ValidateCharacterSave(Save, GameRules());
	CHECK(CountIssues(Issues, ESaveIssue::InvalidSkill) == 2);
	CHECK(CountIssues(Issues, ESaveIssue::UnknownSkill) == 1);
	// cleave is known now: back in the library.
	CHECK(CountIssues(Issues, ESaveIssue::Restored) == 1);
	CHECK((Save.SkillLibrary == std::vector<std::string>{"fireball", "heal", "cleave"}));
	CHECK((Save.QuarantinedSkills == std::vector<std::string>{"removed_skill", "old_skill"}));
	// slash is not learned, heal twice, removed_skill quarantined.
	CHECK((Save.EquippedSkills == std::vector<std::string>{"heal"}));
	CHECK(CountIssues(Issues, ESaveIssue::LoadoutSkillDropped) == 3);
}

static void TestSaveValidationStash()
{
	using namespace SaveTests;
	const FSaveValidationRules Rules = GameRules();
	const FInventoryConfig Config;

	// Overlaps, out of bounds, unknown items, oversized stacks, broken entries.
	FCharacterSave Save = MakeFullSave();
	std::vector<FSavedItem>& Tab = Save.StashTabs[0].Items;
	Tab.push_back(MakeItem(EItemCategory::Weapon, "axe_one_handed", ERarity::Common, 1, {1, 3}, {0, 1, false}));
	Tab.push_back(MakeItem(EItemCategory::Consumable, "healing_potion", ERarity::Common, 2, {1, 1}, {10, 0, false}));
	Tab.push_back(MakeItem(EItemCategory::Weapon, "sword_one_handed", ERarity::Common, 1, {1, 3}, {9, 28, false}));
	Tab.push_back(MakeItem(EItemCategory::Trinket, "removed_gem", ERarity::Rare, 1, {1, 1}, {5, 5, false}));
	Tab.push_back(MakeItem(EItemCategory::Consumable, "healing_potion", ERarity::Rare, 9, {1, 1}, {6, 6, false}));
	Tab.push_back(MakeItem(EItemCategory::Consumable, "healing_potion", ERarity::Rare, 0, {1, 1}, {7, 7, false}));
	Tab.push_back(MakeItem(EItemCategory::Consumable, "", ERarity::Rare, 1, {1, 1}, {8, 8, false}));
	// Saved with a stale size: the data now says 1 x 3.
	Tab.push_back(MakeItem(EItemCategory::Ammo, "arrow", ERarity::Common, 5, {1, 1}, {4, 20, false}));
	FSavedItem BrokenAffix = MakeItem(EItemCategory::Weapon, "sword_one_handed", ERarity::Uncommon, 1, {1, 3}, {8, 20, false});
	BrokenAffix.Affixes = {{"", 1.0}, {"strength", std::nan("")}, {"agility", 2.0}};
	Tab.push_back(BrokenAffix);

	std::vector<FSaveIssue> Issues = ValidateCharacterSave(Save, Rules);
	// The axe overlapped the sword at (0, 0); the potion lay right of the tab; the sword at (9, 28) stuck out at the bottom.
	CHECK(CountIssues(Issues, ESaveIssue::ItemMoved) == 3);
	CHECK(CountIssues(Issues, ESaveIssue::UnknownItem) == 1);
	CHECK(CountIssues(Issues, ESaveIssue::StackTooLarge) == 1);
	CHECK(CountIssues(Issues, ESaveIssue::InvalidItem) == 2);
	CHECK(CountIssues(Issues, ESaveIssue::InvalidAffix) == 1);
	CHECK(StashIsConsistent(Save, Config));
	CHECK(Save.QuarantinedItems.size() == 2 && Save.QuarantinedItems[1].ItemId == "removed_gem");
	int Potions = 0;
	bool bArrowResized = false;
	bool bAffixKept = false;
	for (const FSavedStashTab& Each : Save.StashTabs)
	{
		for (const FSavedItem& Item : Each.Items)
		{
			Potions += Item.ItemId == "healing_potion" && Item.Rarity == ERarity::Rare ? Item.Count : 0;
			bArrowResized = bArrowResized || (Item.ItemId == "arrow" && Item.Count == 5 && Item.Size.Height == 3);
			bAffixKept = bAffixKept || (Item.Rarity == ERarity::Uncommon && Item.Affixes.size() == 1 && Item.Affixes[0].Id == "agility");
		}
	}
	CHECK(Potions == 5);
	CHECK(bArrowResized);
	CHECK(bAffixKept);
	// Validating the result again changes nothing.
	FCharacterSave Twice = Save;
	CHECK(ValidateCharacterSave(Twice, Rules).empty());
	CHECK(WriteCharacterSave(Twice) == WriteCharacterSave(Save));

	// Items in a tab that was never bought move into the bought ones.
	FCharacterSave Extra = MakeFullSave();
	Extra.PurchasedStashTabs = 0;
	Issues = ValidateCharacterSave(Extra, Rules);
	CHECK(Extra.StashTabs.size() == 1 && Extra.StashTabs[0].Items.size() == 4);
	CHECK(CountIssues(Issues, ESaveIssue::ItemMoved) == 2);
	CHECK(StashIsConsistent(Extra, Config));

	// A full stash: what does not fit is quarantined, not lost.
	FCharacterSave Full = MakeFullSave();
	Full.PurchasedStashTabs = 0;
	Full.StashTabs.assign(1, FSavedStashTab());
	for (int Y = 0; Y < Config.StashTabHeight; ++Y)
	{
		for (int X = 0; X < Config.StashTabWidth; ++X)
		{
			Full.StashTabs[0].Items.push_back(MakeItem(EItemCategory::Consumable, "healing_potion", ERarity::Common, 1, {1, 1}, {X, Y, false}));
		}
	}
	Full.StashTabs[0].Items.push_back(MakeItem(EItemCategory::Consumable, "healing_potion", ERarity::Epic, 1, {1, 1}, {0, 0, false}));
	Issues = ValidateCharacterSave(Full, Rules);
	CHECK(CountIssues(Issues, ESaveIssue::NoRoom) == 1);
	CHECK(Full.StashTabs[0].Items.size() == 300);
	CHECK(Full.QuarantinedItems.size() == 2 && Full.QuarantinedItems[1].Rarity == ERarity::Epic);

	// Quarantined items come back once the game knows them and there is room.
	FCharacterSave Returning = MakeFullSave();
	Returning.QuarantinedItems.push_back(MakeItem(EItemCategory::Weapon, "sword_one_handed", ERarity::Mystic, 1, {0, 0}, {}));
	Issues = ValidateCharacterSave(Returning, Rules);
	CHECK(CountIssues(Issues, ESaveIssue::Restored) == 1);
	CHECK(Returning.QuarantinedItems.size() == 1 && Returning.QuarantinedItems[0].ItemId == "old_amulet");
	CHECK(StashIsConsistent(Returning, Config));

	// The quarantine has a cap; the oldest go first.
	FCharacterSave Crowded = MakeFullSave();
	FSaveValidationRules Tight = Rules;
	Tight.MaxQuarantinedItems = 1;
	Crowded.StashTabs[0].Items.push_back(MakeItem(EItemCategory::Trinket, "removed_gem", ERarity::Rare, 1, {1, 1}, {5, 5, false}));
	Issues = ValidateCharacterSave(Crowded, Tight);
	CHECK(CountIssues(Issues, ESaveIssue::QuarantineFull) == 1);
	CHECK(Crowded.QuarantinedItems.size() == 1 && Crowded.QuarantinedItems[0].ItemId == "removed_gem");

	// Without game data every item is known and keeps its size (so the quarantined amulet comes
	// back); zero-size items cannot be placed.
	FCharacterSave NoData = MakeFullSave();
	NoData.StashTabs[0].Items.push_back(MakeItem(EItemCategory::Other, "mystery", ERarity::Common, 1, {0, 0}, {5, 5, false}));
	Issues = ValidateCharacterSave(NoData, FSaveValidationRules());
	CHECK(CountIssues(Issues, ESaveIssue::NoRoom) == 1);
	CHECK(CountIssues(Issues, ESaveIssue::Restored) == 2);
	CHECK(NoData.QuarantinedItems.size() == 1 && NoData.QuarantinedItems[0].ItemId == "mystery");
}

static void TestSaveValidationCarried()
{
	using namespace SaveTests;
	const FSaveValidationRules Rules = GameRules();

	// Saved during a raid: carried gear is gone, the raid is over.
	FCharacterSave Raid = MakeFullSave();
	Raid.bInRaid = true;
	std::vector<FSaveIssue> Issues = ValidateCharacterSave(Raid, Rules);
	CHECK(CountIssues(Issues, ESaveIssue::UnfinishedRaid) == 1);
	CHECK(!Raid.bInRaid && Raid.Carried.empty());
	// The stash and progression are untouched.
	CHECK(Raid.Gold == 4321 && Raid.StashTabs.size() == 2 && Raid.TotalXp == 500);

	// Two weapons in one hand, a bad set, armor worn as a weapon, two quivers.
	FCharacterSave Worn = MakeFullSave();
	FSavedItem Second = MakeItem(EItemCategory::Weapon, "sword_one_handed", ERarity::Common, 1, {1, 3}, {});
	Second.Worn = ESavedWorn::MainHand;
	Second.WornSet = 1;
	Worn.Carried.push_back(Second);
	FSavedItem BadSet = Second;
	BadSet.WornSet = 5;
	Worn.Carried.push_back(BadSet);
	FSavedItem HelmInHand = MakeItem(EItemCategory::Armor, "plate_helm", ERarity::Common, 1, {2, 2}, {});
	HelmInHand.Worn = ESavedWorn::OffHand;
	Worn.Carried.push_back(HelmInHand);
	FSavedItem OtherQuiver = MakeItem(EItemCategory::Container, "quiver", ERarity::Rare, 1, {2, 2}, {});
	OtherQuiver.ContainerKind = EContainerKind::Quiver;
	OtherQuiver.Worn = ESavedWorn::Container;
	Worn.Carried.push_back(OtherQuiver);
	FSavedItem Unknown = MakeItem(EItemCategory::Weapon, "laser_sword", ERarity::Common, 1, {1, 3}, {});
	Unknown.Worn = ESavedWorn::MainHand;
	Worn.Carried.push_back(Unknown);
	FSavedItem TooMany = MakeItem(EItemCategory::Consumable, "healing_potion", ERarity::Common, 50, {1, 1}, {});
	Worn.Carried.push_back(TooMany);
	Issues = ValidateCharacterSave(Worn, Rules);
	CHECK(CountIssues(Issues, ESaveIssue::InvalidWorn) == 4);
	CHECK(CountIssues(Issues, ESaveIssue::UnknownItem) == 1);
	CHECK(CountIssues(Issues, ESaveIssue::StackTooLarge) == 1);
	CHECK(Worn.Carried.size() == 10);
	int StillWorn = 0;
	for (const FSavedItem& Item : Worn.Carried)
	{
		StillWorn += Item.Worn != ESavedWorn::None ? 1 : 0;
		CHECK(Item.Worn == ESavedWorn::MainHand || Item.Worn == ESavedWorn::OffHand || Item.WornSet == 0);
	}
	CHECK(StillWorn == 3);
	CHECK(Worn.QuarantinedItems.size() == 2 && Worn.QuarantinedItems[1].ItemId == "laser_sword"
		&& Worn.QuarantinedItems[1].Worn == ESavedWorn::None);
}

static void TestPlaceInStash()
{
	using namespace SaveTests;
	const FInventoryConfig Config;
	std::vector<FSavedStashTab> Tabs(2);
	FSavedItem Spear = MakeItem(EItemCategory::Weapon, "spear", ERarity::Common, 1, {1, 5}, {});
	CHECK(PlaceInStash(Tabs, Spear, Config));
	CHECK(Tabs[0].Items.size() == 1 && Tabs[0].Items[0].Placement.X == 0 && Tabs[0].Items[0].Placement.Y == 0);
	FSavedItem Next = Spear;
	CHECK(PlaceInStash(Tabs, Next, Config) && Next.Placement.X == 1 && Next.Placement.Y == 0);
	FSavedItem NoSize = MakeItem(EItemCategory::Other, "x", ERarity::Common, 1, {0, 1}, {});
	CHECK(!PlaceInStash(Tabs, NoSize, Config));
	// Too big for any tab.
	FSavedItem Huge = MakeItem(EItemCategory::Other, "x", ERarity::Common, 1, {11, 31}, {});
	CHECK(!PlaceInStash(Tabs, Huge, Config));
	CHECK(std::string(ItemCategoryName(EItemCategory::SkillGem)) == "skillGem");
	CHECK(std::string(RarityName(ERarity::Legendary)) == "legendary");
	CHECK(std::string(SaveLoadErrorName(ESaveLoadError::WrongPlayer)) == "WrongPlayer");
	CHECK(std::string(SaveIssueName(ESaveIssue::QuarantineFull)) == "QuarantineFull");
}

int main()
{
	TestProgression();
	TestDerivedStats();
	TestDamage();
	TestReductions();
	TestLoadout();
	TestIncludeCycle();
	TestRarityAndWeapons();
	TestEquipment();
	TestSecondaryAction();
	TestTeams();
	TestArmor();
	TestAffixRolls();
	TestExperience();
	TestInventoryGrid();
	TestSkillLibrary();
	TestEconomy();
	TestPortal();
	TestGathering();
	TestLoot();
	TestGuild();
	TestParty();
	TestTrade();
	TestGuard();
	TestJson();
	TestSaveRoundTrip();
	TestSaveRefusesCorruptDocuments();
	TestSaveMigration();
	TestSaveValidationClamps();
	TestSaveValidationSkills();
	TestSaveValidationStash();
	TestSaveValidationCarried();
	TestPlaceInStash();

	std::printf("%d checks, %d failures\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
