# Third-party placeholder art

Everything here is **CC0 1.0** (public domain dedication): free for commercial use, no attribution
required. Credit is given anyway. Imported by `Tools/import_art.py` (docs/art-placeholder-plan.md).
Downloaded 2026-10-16; the raw zips and FBX files are not in the repository.

| Folder | Source | Licence | Assets |
| --- | --- | --- | --- |
| `Kenney/MiniDungeon` | Kenney, Mini Dungeon (https://kenney.nl/assets/mini-dungeon) | CC0 1.0 | `SM_Sword`, `SM_Spear`, `SM_ShieldRound` |
| `Kenney/SurvivalKit` | Kenney, Survival Kit (https://kenney.nl/assets/survival-kit) | CC0 1.0 | `SM_ToolAxe`, `SM_Bottle` |
| `Kenney/GraveyardKit` | Kenney, Graveyard Kit 5.0 (https://kenney.nl/assets/graveyard-kit) | CC0 1.0 | `SK_Skeleton`, `SK_Zombie` (with their skeletons and animation) |
| `Kenney/ImpactSounds` | Kenney, Impact Sounds (https://kenney.nl/assets/impact-sounds) | CC0 1.0 | `S_Hit`, `S_Block`, `S_Death`, `S_ImpactWood`, `S_ImpactMetal`, `S_ImpactGlass` |
| `Kenney/RpgAudio` | Kenney, RPG Audio (https://kenney.nl/assets/rpg-audio) | CC0 1.0 | `S_Swing` |
| `PolyHaven/*` | Poly Haven (https://polyhaven.com): `ornate_medieval_dagger`, `wooden_axe`, `kite_shield`, `treasure_chest`, `large_castle_door`, `nettle_plant`, `rock_moss_set_01` (1k FBX) | CC0 1.0 | one `SM_<Name>` per folder, with its textures and materials |
| `KayKit/Skeletons` | KayKit Skeletons 1.1 (free), Kay Lousberg (https://kaylousberg.itch.io/kaykit-skeletons) | CC0 1.0 | `SK_Skeleton_Warrior`, `_Rogue`, `_Mage`, `_Minion` (one skeleton, `SK_Skeleton_Warrior_Skeleton`), `SM_Skeleton_Staff` |
| `KayKit/Animations` | KayKit Character Animations 1.1, Kay Lousberg (https://kaylousberg.itch.io/kaykit-character-animations) | CC0 1.0 | the Rig_Medium animations (General, MovementBasic, CombatMelee, CombatRanged, Special) |
| `Quaternius/MedievalWeapons` | Quaternius, Medieval Weapons Pack (https://quaternius.com) | CC0 1.0 | `SM_Sword`, `SM_Sword_Big`, `SM_Dagger`, `SM_Axe_Small`, `SM_Axe_Double`, `SM_Spear`, `SM_Shield_Heater`, `SM_Bow_Wooden`, `SM_Arrow` (imported at 0.2) |
| `Quaternius/UltimateMonsters` | Quaternius, Ultimate Monsters (https://quaternius.com) | CC0 1.0 | `SK_Orc`, `SK_Dragon_Evolved` with their animations, `Atlas_Monsters` |

**`Content/NiagaraExamples`** is Epic's **Niagara Examples Pack** (Fab, free): the Fab Standard
License, **for Unreal Engine projects only** (not CC0). Only the systems in use and what they need
are in the repository (105 files, `git add -f`); the rest of the pack is ignored (`.gitignore`) and
stays on the owner's PC: `NS_Impact_Concrete` (character hits), `NS_Spark_Burst` (block / parry
clank), `NS_SimpleRibbonTrail` and `NS_Impact_Wood` (arrows, bolts), `NS_Impact_Metal` (thrown axes
and knives), `NS_Explosion_Small` (explosive bottle). To use another system: add it to Fab's copy on
the PC, then `git add -f` it and its dependencies.

Downloaded but not imported yet (in the same CC0 packs): KayKit Dungeon Remastered 1.1, Quaternius
Animated Monster Pack and Ultimate Animated Animals (the Wolf for the tiger summon), Kenney Castle Kit, Fantasy Town Kit 2.0,
Particle Pack (PNG sprites only, for Niagara later); Poly Haven `large_iron_gate`, `wooden_crate_01`,
`Barrel_01`, `wooden_lantern_01`, `rock_07`, `periwinkle_plant`, `stone_fire_pit`.
