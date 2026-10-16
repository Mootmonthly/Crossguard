# Skill Coverage

Every skill in `Data/Skills/Skills.json` against the code, as of the 2026-10 audit. Rulings:
[design-decisions.md](design-decisions.md) (Skills). Tests: [editor-tests.md](editor-tests.md).

**How a skill is played**

- **C++**: its own ability class, registered in `CrossguardNativeSkills.cpp`.
- **Data**: a Passive with no class is played by `UCrossguardDataPassiveAbility` from its row: Self stat
  modifiers, wielding / armor / per-empty-slot conditions, and the skill's own `requirements`
  (weapons, armor). Off while Muted.
- **Rider**: an on-hit part added to basic attacks in `UCrossguardWeaponAttackAbility::GatherRiders`
  (players) and `ACrossguardAICharacter` (AI), applied by `ApplyRiders`.

**Status**: done = everything the text and rulings say is in code; partial = a part is missing (what
and why); blocked = cannot be done until another system exists.

| Skill | Category | Played by | Status | Test (`Crossguard.Scenario.Skills.*`) |
| --- | --- | --- | --- | --- |
| Antimagic Garb | Passive | Data | done (off with its bundle's requirement) | SpearOfLiscorNeedsSpear, MithrilKnightNeedsFullPlate |
| Arcane Arrows | Active | C++ + rider | partial: "dropoff reduced by 50%" waits for arrow damage dropoff (arrows have none) | ArcaneArrows |
| Arcane Intellect | Passive | Data | done | none of its own (as HeroicMight) |
| Armor Mastery | Passive | Data | done (Plate requirement enforced since this audit) | ArmorMasteryNeedsPlate |
| Axe Mastery | Passive | Data | done (wielding axe, melee CDR) | none of its own (as SwordMastery) |
| Benediction | Spell | C++ | done (full heal, revives a body) | Benediction |
| Bow Mastery | Passive | Data | done (wielding bow, ranged CDR) | none of its own (as SwordMastery) |
| Chop Down | Active | C++ + rider | done (110%, slow, Grounds fliers; a thrown throwing axe uses it up) | ChopDown, ThrownAxeChopDown |
| Cleave | Art | C++ | done | Cleave |
| Cor Leonis | Passive | Data | done (per empty armor slot) | CorLeonisEmptySlots |
| Dagger Mastery | Passive | Data | done | none of its own (as SwordMastery) |
| Divine Alacrity | Passive | Data | done | none of its own (as HeroicMight) |
| Dragonification | Art | C++ | done (dragon look is Editor work) | Dragonification |
| Elder Summoner | Passive | Data | done (Minion CDR on `Skill.Minion` summons) | none of its own |
| Eldritch Shield | Spell | C++ | done | EldritchShield |
| Fireball | Spell | C++ | done | Fireball, FireballSelfSplash |
| Flare Breath | Spell | C++ | done | Dragonification (cast in dragon form) |
| Flight | Spell | C++ | done | Flight, FlightDescend |
| Gouge | Active | C++ + rider | done (a thrown throwing knife uses it up) | Gouge, ThrownKnifeGouge |
| Greater Heal | Spell | C++ (Heal subclass) | done | none of its own (as Heal) |
| Heal | Spell | C++ | done | Heal |
| Heroic Might | Passive | Data | done | HeroicMight |
| Hunter's Leap | Art | C++ | done | HuntersLeap |
| Hunter's Tracking | Passive | C++ | partial: footprints done; "+20 m hearing" blocked (no sound system) | HuntersTracking |
| Immutable | Active | C++ | done (permanent and party CC immunity; both off with The Mithril Knight's requirement) | Immutable, MithrilKnightNeedsFullPlate |
| Light | Spell | C++ | done | Light |
| Like A Lion, He Leapt | Art | C++ | done | LikeALion |
| Mortal Strike | Active | C++ + rider | done (cooldown back on a kill; thrown knives and axes use it up, untested) | NextAttackStack, MortalStrikeKillRefresh |
| Multiscale | Passive | C++ (party aura) | done (off with The Mithril Knight's requirement) | Multiscale, MithrilKnightNeedsFullPlate |
| Mute | Spell | C++ | done (passives off too) | Mute |
| Neckbite | Active | C++ + rider | blocked for players: needs Beastial forms; the tiger has it | NeckbiteSilence, SummonTigerLeapAndRiders |
| Parting the Land | Active | C++ | done | PartingTheLand |
| Purge Shot | Active | C++ + rider | done | PurgeShot |
| Quickshot | Active | C++ | done | Quickshot |
| Shade Blow | Active | C++ + rider | done (heal block; a thrown throwing axe uses it up, untested) | NextAttackStack, ShadeBlowHealBlock |
| Shooting Star | Art | C++ | done (Blind screen effect is Editor work) | ShootingStar |
| Slash | Active | C++ + rider | done (a thrown throwing knife uses it up) | NextAttackStack, ThrownKnifeSlash |
| Spear Mastery | Passive | Data | done | none of its own (as SwordMastery) |
| Spear of Liscor | Passive | Data (bundle) | done (Spear requirement enforced since this audit; it also switches off its components) | SpearOfLiscorNeedsSpear |
| Splinter of Winter | Passive | C++ + rider (Dagger Mastery from data) | done (thrown throwing knives too) | SplinterOfWinter, ThrownKnifeSplinterOfWinter |
| Stealth | Active | C++ | done (enemy position leak: roadmap decision) | Stealth, StealthNetRelevancy |
| Summon Greater Demon | Spell | C++ | done (reach scales with its 3x size: 6 m) | SummonGreaterDemon, SummonGreaterDemonFights |
| Summon Skeletons | Spell | C++ | done (Mage Fireball every 8 s) | SummonSkeletons, SummonSkeletonsFollow |
| Summon Tiger | Spell | C++ | done (Pet Gem needed, kept) | SummonTiger, SummonTigerLeapAndRiders |
| Sword Mastery | Passive | Data | done | SwordMastery |
| The Mithril Knight | Passive | Data (bundle) | done (Plate in every slot enforced since this audit; it also switches off its components) | MithrilKnightNeedsFullPlate |
| Trauma Mastery | Passive | Data | done | none of its own (as SwordMastery) |
| Triple Thrust | Art | C++ | done (piercing through guards for half) | TripleThrust |
| Unleash | Active | C++ | done | Unleash |
| Wall | Spell | C++ | done | Wall |

Totals: 50 skills; 47 done, 2 partial (Arcane Arrows dropoff, Hunter's Tracking hearing), 1 blocked for
players (Neckbite).

## Fixed in this audit

- Passives played from data ignored the skill's own `requirements`: Armor Mastery worked without
  Plate, The Mithril Knight without Plate in every slot, Spear of Liscor without a Spear. Now their
  own bonuses count only while the requirement is met (AI characters skip requirements, as for active
  skills).
- Ruled since: a bundle's requirement also switches off the skills it includes
  (UCrossguardLoadoutComponent::IsSkillSwitchedOn, rechecked on equipment changes), and a thrown
  throwing knife is a dagger attack for Splinter of Winter and Gouge. Then: every next-attack skill
  whose weapon requirement accepts the throw's weapon (knife: Dagger, axe: Axe) rides on it as on a
  melee attack with that weapon; a throw never uses up a parry's riposte (ThrownKnifeSlash checks).

## Open questions

None left from this audit.
