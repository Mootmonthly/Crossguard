// Unit tests for CrossguardRules. Minimal harness: no exceptions, no dependencies.

#include "CrossguardRules/Combat.h"
#include "CrossguardRules/Loadout.h"
#include "CrossguardRules/Progression.h"
#include "CrossguardRules/Stats.h"

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

	CHECK_NEAR(ApplyReduction(12.0, 0.25), 9.0);
	CHECK_NEAR(ApplyReduction(12.0, -0.10), 13.2);
}

static FSkillCatalog MakeCatalog()
{
	FSkillCatalog Catalog;
	CHECK(Catalog.Add({"dagger_mastery", 2, {}}));
	CHECK(Catalog.Add({"splinter_of_winter", 2, {"dagger_mastery"}}));
	CHECK(Catalog.Add({"antimagic_garb", 5, {}}));
	CHECK(Catalog.Add({"spear_mastery", 2, {}}));
	CHECK(Catalog.Add({"triple_thrust", 8, {}}));
	CHECK(Catalog.Add({"spear_of_liscor", 15, {"triple_thrust", "spear_mastery", "antimagic_garb"}}));
	CHECK(Catalog.Add({"multiscale", 9, {}}));
	CHECK(Catalog.Add({"immutable", 9, {}}));
	CHECK(Catalog.Add({"the_mithril_knight", 15, {"multiscale", "immutable", "antimagic_garb"}}));
	CHECK(Catalog.Add({"fireball", 3, {}}));
	CHECK(Catalog.Add({"heal", 2, {}}));
	CHECK(!Catalog.Add({"heal", 2, {}}));
	CHECK(!Catalog.Add({"", 1, {}}));
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
	CHECK(Result.Error == ELoadoutError::OverlappingSkills);
	CHECK(Result.SkillId == "dagger_mastery");
	CHECK(Result.OtherSkillId == "splinter_of_winter");
	CHECK(ValidateLoadout(Catalog, {"dagger_mastery", "splinter_of_winter"}, 20).Error == ELoadoutError::OverlappingSkills);

	// Two bundles sharing Antimagic Garb are blocked (pending design confirmation).
	CHECK(ValidateLoadout(Catalog, {"spear_of_liscor", "the_mithril_knight"}, 30).Error == ELoadoutError::OverlappingSkills);

	// Overlap is reported before capacity.
	CHECK(ValidateLoadout(Catalog, {"spear_of_liscor", "antimagic_garb"}, 1).Error == ELoadoutError::OverlappingSkills);
}

static void TestIncludeCycle()
{
	FSkillCatalog Catalog;
	Catalog.Add({"a", 1, {"b"}});
	Catalog.Add({"b", 1, {"a"}});
	CHECK(Catalog.EffectiveSkills("a").size() == 2);
}

int main()
{
	TestProgression();
	TestDerivedStats();
	TestDamage();
	TestReductions();
	TestLoadout();
	TestIncludeCycle();

	std::printf("%d checks, %d failures\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
