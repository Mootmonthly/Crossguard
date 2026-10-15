#include "CrossguardRules/Stats.h"

namespace Crossguard
{
	FDerivedStats DeriveStats(const FAttributes& Attributes, const FScalingConfig& Config)
	{
		const double Str = Attributes.Str - Config.ReferenceAttribute;
		const double Agi = Attributes.Agi - Config.ReferenceAttribute;
		const double Int = Attributes.Int - Config.ReferenceAttribute;

		FDerivedStats Stats;
		Stats.MaxHp = Config.BaseMaxHp + Config.MaxHpPerStr * Str;
		Stats.Adb = Config.AdbPerStr * Str;
		Stats.Ar = Config.ArPerStr * Str;
		Stats.Phb = Config.PhbPerStr * Str;
		Stats.ActiveCastTimeReduction = Config.ActiveCastTimeReductionPerStr * Str;
		Stats.ActiveCdr = Config.ActiveCdrPerStr * Str;

		Stats.MoveSpeed = Config.BaseMoveSpeed + Config.MoveSpeedPerAgi * Agi;
		Stats.ActionSpeed = Config.ActionSpeedPerAgi * Agi;
		Stats.Dexterity = Config.DexterityPerAgi * Agi;
		Stats.ArtCastTimeReduction = Config.ArtCastTimeReductionPerAgi * Agi;
		Stats.ArtCdr = Config.ArtCdrPerAgi * Agi;

		Stats.Mdb = Config.MdbPerInt * Int;
		Stats.Mr = Config.MrPerInt * Int;
		Stats.Mhb = Config.MhbPerInt * Int;
		Stats.SpellCastTimeReduction = Config.SpellCastTimeReductionPerInt * Int;
		Stats.SpellCdr = Config.SpellCdrPerInt * Int;
		return Stats;
	}

	double ApplyMaxHpBonus(double BaseMaxHp, double Mhpb)
	{
		const double MaxHp = BaseMaxHp * (1.0 + Mhpb);
		return MaxHp < 1.0 ? 1.0 : MaxHp;
	}

	double CategoryCdr(const FDerivedStats& Stats, ESkillCategory Category)
	{
		switch (Category)
		{
		case ESkillCategory::Active: return Stats.ActiveCdr;
		case ESkillCategory::Spell: return Stats.SpellCdr;
		case ESkillCategory::Art: return Stats.ArtCdr;
		case ESkillCategory::Passive: return 0.0;
		}
		return 0.0;
	}

	double CategoryCastTimeReduction(const FDerivedStats& Stats, ESkillCategory Category)
	{
		switch (Category)
		{
		case ESkillCategory::Active: return Stats.ActiveCastTimeReduction;
		case ESkillCategory::Spell: return Stats.SpellCastTimeReduction;
		case ESkillCategory::Art: return Stats.ArtCastTimeReduction;
		case ESkillCategory::Passive: return 0.0;
		}
		return 0.0;
	}
}
