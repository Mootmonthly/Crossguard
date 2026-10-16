#include "AbilitySystem/CrossguardGameplayTags.h"

namespace CrossguardTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Category_Active, "Skill.Category.Active", "Activated skill that is neither a Spell nor an Art. STR cast time and CDR.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Category_Spell, "Skill.Category.Spell", "Spell. INT cast time and CDR.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Category_Art, "Skill.Category.Art", "Art. AGI cast time and CDR.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Category_Passive, "Skill.Category.Passive", "Never activated.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Weapon_Melee, "Skill.Weapon.Melee", "Requires a melee weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Weapon_Ranged, "Skill.Weapon.Ranged", "Requires a ranged weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill_Minion, "Skill.Minion", "Summons minions.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage_Attack, "SetByCaller.Damage.Attack", "Base Attack Damage (weapon AD or skill amount).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage_AttackBonus, "SetByCaller.Damage.AttackBonus", "Extra Attack Damage Bonus for this hit only (a parry's riposte).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage_AttackShare, "SetByCaller.Damage.AttackShare", "Share of the finished Attack Damage (True Damage included) that lands (a guard); 1 when unset.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage_Magic, "SetByCaller.Damage.Magic", "Base Magic Damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Healing_Physical, "SetByCaller.Healing.Physical", "Base Physical Healing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Healing_Magical, "SetByCaller.Healing.Magical", "Base Magical Healing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Coefficient, "SetByCaller.Coefficient", "Fraction of ADB/MDB/PHB/MHB that applies. Defaults to 1.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "Health reached 0.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Muted, "State.Muted", "Cannot cast skills; passive skills are off.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Silenced, "State.Silenced", "Cannot activate skills; passives and basic attacks still work. Crowd control.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Stunned, "State.Stunned", "Cannot move, attack, guard or use skills (a lost weapon collision). Crowd control.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Rooted, "State.Rooted", "Cannot walk, jump, leap or take flight; can still attack, guard and use other skills. Crowd control.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Guarding, "State.Guarding", "Blocking or parrying (right click held): half speed, no attacks.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Slowed, "State.Slowed", "Movement speed reduced.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Blinded, "State.Blinded", "Screen flashed white.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Flying, "State.Flying", "Flying (Flight).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dragonified, "State.Dragonified", "In dragon form: weapons and equipped skills blocked.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_ChopDownReady, "State.ChopDownReady", "Next axe attack is empowered by Chop Down.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_NextAttack_Slash, "State.NextAttack.Slash", "Next melee attack: +7 weapon damage (Slash).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_NextAttack_MortalStrike, "State.NextAttack.MortalStrike", "Next melee attack: +20 Attack Damage; a kill refreshes the cooldown (Mortal Strike).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_NextAttack_ShadeBlow, "State.NextAttack.ShadeBlow", "Next melee attack: +20 Magic Damage and blocks healing 5 s (Shade Blow).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_NextAttack_Gouge, "State.NextAttack.Gouge", "Next melee attack: 25 Attack Damage over 5 s and a 5% slow (Gouge).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_NextAttack_PurgeShot, "State.NextAttack.PurgeShot", "Next bow or crossbow attack: +10 Magic Damage and a 3 s Mute (Purge Shot).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_NextAttack_Neckbite, "State.NextAttack.Neckbite", "Next melee attack: +10 Magic Damage and a 2 s Silence (Neckbite).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_NextAttack_Riposte, "State.NextAttack.Riposte", "Next attack: +50% Attack Damage Bonus (a parry).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_RiposteSpeed, "State.RiposteSpeed", "+10% Action Speed for 3 s after a parry (one at a time).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_ArcaneArrows, "State.ArcaneArrows", "Bow attacks deal +5 Magic Damage (Arcane Arrows).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_HealBlocked, "State.HealBlocked", "Cannot be healed (Shade Blow).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Stealthed, "State.Stealthed", "Invisible to enemies (Stealth).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_CCImmune, "State.CCImmune", "Immune to crowd control (Immutable).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_KnockedUp, "State.KnockedUp", "Knocked into the air (Parting the Land).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Passive_Immutable, "Passive.Immutable", "Permanently immune to crowd control while Immutable is equipped and not Muted.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Passive_SplinterOfWinter, "Passive.SplinterOfWinter", "Dagger attacks deal +5 Magic Damage and slow 10% for 3 s.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Passive_HuntersTracking, "Passive.HuntersTracking", "Sees enemy players' footsteps from the last 30 s (Hunter's Tracking). On the owner's client too.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Multiscale, "State.Multiscale", "Has the Multiscale party aura (one at a time).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Flight, "Ability.Flight", "The Flight ability.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Guard, "Ability.Guard", "Blocking or parrying (UCrossguardGuardAbility).");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Dragonification, "Cooldown.Skill.Dragonification");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_FlareBreath, "Cooldown.Skill.FlareBreath");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Flight, "Cooldown.Skill.Flight");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Fireball, "Cooldown.Skill.Fireball");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_ChopDown, "Cooldown.Skill.ChopDown");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_SummonSkeletons, "Cooldown.Skill.SummonSkeletons");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_SummonTiger, "Cooldown.Skill.SummonTiger");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_SummonGreaterDemon, "Cooldown.Skill.SummonGreaterDemon");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_EldritchShield, "Cooldown.Skill.EldritchShield");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_ShootingStar, "Cooldown.Skill.ShootingStar");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Heal, "Cooldown.Skill.Heal");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Mute, "Cooldown.Skill.Mute");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Slash, "Cooldown.Skill.Slash");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_MortalStrike, "Cooldown.Skill.MortalStrike");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_ShadeBlow, "Cooldown.Skill.ShadeBlow");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Gouge, "Cooldown.Skill.Gouge");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_PurgeShot, "Cooldown.Skill.PurgeShot");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_ArcaneArrows, "Cooldown.Skill.ArcaneArrows");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Quickshot, "Cooldown.Skill.Quickshot");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Cleave, "Cooldown.Skill.Cleave");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_TripleThrust, "Cooldown.Skill.TripleThrust");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_PartingTheLand, "Cooldown.Skill.PartingTheLand");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_HuntersLeap, "Cooldown.Skill.HuntersLeap");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Neckbite, "Cooldown.Skill.Neckbite");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_GreaterHeal, "Cooldown.Skill.GreaterHeal");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Benediction, "Cooldown.Skill.Benediction");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Unleash, "Cooldown.Skill.Unleash");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Light, "Cooldown.Skill.Light");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Wall, "Cooldown.Skill.Wall");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Stealth, "Cooldown.Skill.Stealth");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_Immutable, "Cooldown.Skill.Immutable");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Skill_LikeALion, "Cooldown.Skill.LikeALion");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Lockpicking, "Ability.Lockpicking", "Picks locks without a lockpick item (the lockpicking skill).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Sword, "Equipment.Wielding.Sword", "Holding a Sword.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Axe, "Equipment.Wielding.Axe", "Holding an Axe.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Dagger, "Equipment.Wielding.Dagger", "Holding a Dagger.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Spear, "Equipment.Wielding.Spear", "Holding a Spear.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Mace, "Equipment.Wielding.Mace", "Holding a Mace.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Quarterstaff, "Equipment.Wielding.Quarterstaff", "Holding a Quarterstaff.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Hammer, "Equipment.Wielding.Hammer", "Holding a Hammer.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Shield, "Equipment.Wielding.Shield", "Holding a Shield.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Bow, "Equipment.Wielding.Bow", "Holding a Bow.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Crossbow, "Equipment.Wielding.Crossbow", "Holding a Crossbow.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Sling, "Equipment.Wielding.Sling", "Holding a Sling.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Throwing, "Equipment.Wielding.Throwing", "Holding a Throwing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_Wand, "Equipment.Wielding.Wand", "Holding a Wand.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_MagicStaff, "Equipment.Wielding.MagicStaff", "Holding a magic staff.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_AnyMelee, "Equipment.Wielding.AnyMelee", "Holding any melee weapon (shields count).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Wielding_TwoHandedMelee, "Equipment.Wielding.TwoHandedMelee", "Holding a two-handed melee weapon in the main hand.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Armor_Plate, "Equipment.Armor.Plate", "Wearing at least one plate piece.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_Armor_PlateAllSlots, "Equipment.Armor.PlateAllSlots", "Plate in Head, Chest, Hands, Legs and Feet.");
}
