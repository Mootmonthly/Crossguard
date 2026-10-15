// Unit tests for CrossguardRules. Minimal harness: no exceptions, no dependencies.

#include "CrossguardRules/Combat.h"
#include "CrossguardRules/Economy.h"
#include "CrossguardRules/Experience.h"
#include "CrossguardRules/Inventory.h"
#include "CrossguardRules/Portal.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/Loadout.h"
#include "CrossguardRules/Progression.h"
#include "CrossguardRules/Stats.h"
#include "CrossguardRules/Teams.h"

#include <cmath>
#include <cstdio>

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

	std::printf("%d checks, %d failures\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
