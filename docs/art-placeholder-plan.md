# Placeholder Art Plan

Free art for testing feel: seeing weapons, projectiles, distances and monster attacks. None of it is
final art. Checked live on **2026-10-16** unless a line says otherwise. Licences change, so re-check a
page before shipping anything from it.

Rules for picking:

- **Commercial use OK, no NC licences.** CC0 is best (no attribution). The Fab Standard License and
  Epic's own free content are fine for an Unreal game. Epic content such as Paragon, Lyra and the
  Niagara Examples is **UE-only**: fine here, but it can't go into another engine.
- **Scriptable first.** A Claude Code session on the PC can download direct links and import FBX/GLB
  with Unreal Python. It **cannot** click through Fab, itch.io, Mixamo or Adobe sign-ins. Each item
  below is marked:
  - **[script]**: a stable direct URL that needs no account.
  - **[click]**: a browser click, no account (itch.io "No thanks, just take me to the downloads").
  - **[Fab]**: needs the owner's Epic account in Fab and the editor (see [Fab steps](#owners-manual-steps-for-fab-items)).
  - **[login]**: needs another account (Adobe for Mixamo).
- Low-poly stylised (Quaternius, KayKit, Kenney) and realistic (Poly Haven, Epic) assets will clash.
  That doesn't matter for feel tests. Pick one look before real art starts.

## Starter set

The smallest set that makes every feel test visible. All of it is free for commercial use.

| Need | Pick | Licence | Get it |
| --- | --- | --- | --- |
| Body, death, hit reacts | **Already in the repo**: `Content/Characters/Mannequins` (Manny/Quinn, `MM_Attack_01-03`, `MM_ChargedAttack`, `MM_Death_*`, `MM_HitReact_*`) | Epic (UE-only) | Nothing to do |
| First-person arms | **UE 5.8 First Person template content** (Add → Add Feature or Content Pack → First Person), plus the engine's First Person Rendering (camera FOV/scale, `FirstPersonPrimitiveType`) | Epic (UE-only) | Bundled with the engine |
| Melee and bow animations | **KayKit Character Animations** (161 humanoid anims: 1H/2H melee, block, dual-wield, bow and spellcasting, death) retargeted onto Manny with the IK Retargeter | CC0 | [click] https://kaylousberg.itch.io/kaykit-character-animations |
| Weapons and shields | **Quaternius Modular Weapons Pack** (24 FBX: Sword, Sword_2, Claymore, Axe, Axe_Double, Dagger, Spear, Bow_Wooden, Arrow, Shield_Heater, Shield_Round, …) | CC0 | [script] Google Drive, IDs [below](#direct-download-list-for-a-local-claude-session) |
| Skeleton soldier, archer and mage | **KayKit Skeletons** (free tier: Warrior, Rogue, Mage, Minion, rigged; swords, shields, axes, crossbow, staff) + KayKit Character Animations | CC0 | [click] https://kaylousberg.itch.io/kaykit-skeletons |
| Dragon | **Quaternius Animated Monster Pack**: `Dragon.fbx` (animated; the pack also has a `Skeleton.fbx`) | CC0 | [script] Drive |
| Greater demon | **Quaternius Ultimate Monsters**: `Big/Demon.fbx` (animated, cute style) | CC0 | [script] Drive |
| Tiger | No good free CC0 big cat found. Stand-in: **Quaternius Ultimate Animated Animals `Wolf.fbx`** in an orange material | CC0 | [script] Drive |
| Fireball, impacts, sparks, beam | **Niagara Examples Pack** (Epic, 50+ systems: fire, explosions, impacts, sparks, lightning, smoke, with a gallery level) + **Kenney Particle Pack** sprites for custom Niagara (fire, flame, magic, spark, smoke, light, trace) | Epic (UE-only) / CC0 | [Fab] / [script] |
| Chests, doors, dungeon | **KayKit Dungeon Remastered** (200 free: walls, floors, stairs, doors, chests, barrels, crates, traps) | CC0 | [click] https://kaylousberg.itch.io/kaykit-dungeon-remastered |
| Realistic chest, door, ore, herb | **Poly Haven** `treasure_chest`, `large_castle_door`, `large_iron_gate`, `rock_moss_set_01`, `nettle_plant` | CC0 | [script] |
| Clank, hits, doors, bow | **Kenney Impact Sounds** (`impactMetal_*`, `impactPlate_*`, `impactWood_*`, `impactMining_*`) + **Kenney RPG Audio** (`drawKnife*`, `knifeSlice*`, `doorOpen_*`, `creak*`, `chop`) | CC0 | [script] |

With this set the owner needs one Fab click (Niagara Examples) and three itch.io clicks (KayKit). A
local Claude session can script everything else.

## Detailed options by category

### 1. First-person arms and animations

| Asset | What | Licence | Access | Notes |
| --- | --- | --- | --- | --- |
| UE 5.8 First Person template | Arms/full-body first-person character, Control Rig, first-person rendering (independent FOV and scale, no clipping). Since 5.6 it also has Arena Shooter and Survival Horror variants. | Epic, UE-only | Bundled: Add → Add Feature or Content Pack → First Person | Its animations are guns. Melee comes from the packs below. (Assumed: the 5.8 content pack still ships the 5.6 setup; check the template.) |
| Repo mannequin anims | `MM_Attack_01/02/03`, `MM_ChargedAttack` (unarmed combo variant), deaths, hit reacts | Epic, UE-only | Already in `Content/Characters/Mannequins` | Unarmed swings are enough to time a melee hit. |
| KayKit Character Animations | 161 anims, CC0: melee 1H/2H/dual/block, ranged (bow), spellcasting, death, hit | CC0 | [click] itch | Rig_Medium/Rig_Large. Retarget onto `SK_Mannequin` with an IK Rig per skeleton + IK Retargeter (auto-mapping works for humanoids). |
| Quaternius Universal Animation Library (1 and 2) | UAL1: 45 free of 120+ (combat, sword swing). UAL2 (January 2026): melee combos split per hit with recoveries; "60-70% free". | CC0 | [click] https://quaternius.itch.io/universal-animation-library , https://quaternius.itch.io/universal-animation-library-2 | Universal humanoid rig, described as Mixamo- and UE-ready. The free tier has no root motion. |
| Mixamo | Large third-person library: Sword and Shield, Great Sword, Longbow, Magic packs | Royalty-free for games; raw files can't be redistributed | [login] free Adobe account | Download FBX "without skin" on the Y Bot, then retarget to Manny. Adobe's current FAQ couldn't be fetched; the terms are from Adobe's community FAQ copy. |
| Game Animation Sample Project (GASP) | ~500 motion-matched locomotion anims, Mover. Updated for UE 5.8 on 2026-08-12 (Epic tech blog). | Epic, UE-only | [Fab] | No combat. Only worth it later for third-person locomotion of other players. |
| Lyra Starter Game | Full sample game, Manny/Quinn and guns | Epic, UE-only (listing says "use only with Unreal Engine-based products") | [Fab] https://www.fab.com/listings/93faede1-4434-47c0-85f1-bf27c0820ad0 | Not needed: the repo already has the mannequins. |
| "First Person Melee Animations" (Fab) | 31 first-person blade anims built for the UE FP template | Not confirmed | [Fab] https://www.fab.com/listings/2af6e911-4066-4cbe-91cc-dad8e929f291 | Fab blocks scripted reads, so the **price wasn't confirmed**. Check whether it is free before relying on it. |

**Recommendation (Assumed, placeholder):** for the very first feel pass, skip arms. Attach the
weapon static mesh to the first-person camera with first-person rendering on, and play a short
procedural swing (a Timeline rotating the weapon over the swing time) when an attack starts. It's
quick and shows reach and timing. Then move to Manny arms with retargeted KayKit or Mixamo swings.

### 2. Weapons and shields

| Asset | Covers | Licence | Access |
| --- | --- | --- | --- |
| Quaternius Modular Weapons Pack | Sword ×4, Claymore, Axe ×3, Dagger ×2, Spear, Scythe, Hammer ×2, Bow ×4, Arrow, Shield ×5 (untextured, vertex/flat colours) | CC0 | [script] Drive folder `1Z6vYiQxY8W73FXuMWzaTQAg9rzbumnOr` (FBX subfolder `1kcJNPDGro3s9Eoa9Z4PCf6jCAIPJp_c-`) |
| KayKit Skeletons (free) | Skeleton swords, shields, axes, crossbow, arrows, **staff** | CC0 | [click] |
| Quaternius Fantasy Props MegaKit (Standard, 94 free) | Weapons, tools, potions, chests, books, breakables | CC0 | [click] https://quaternius.itch.io/fantasy-props-megakit |
| Poly Haven | `kite_shield`, `ornate_medieval_dagger`, `ornate_medieval_mace`, `wooden_axe` (realistic PBR) | CC0 | [script] |
| Infinity Blade: Weapons (Epic) | Large set of realistic swords and axes | Epic, free "permanently" (2019) | [Fab]. **Not confirmed** that it is still listed on Fab. |

Wands and staffs: KayKit staff, or a scaled Quaternius Spear/Hammer handle until a real one exists.

### 3. Monsters

| Mob (Data/Mobs) | Pick | Licence | Access |
| --- | --- | --- | --- |
| skeleton_soldier / archer / mage | KayKit Skeletons: Warrior / Rogue (crossbow; give it a Quaternius bow) / Mage (staff) + KayKit Character Animations | CC0 | [click] |
| (scriptable alternative) | Quaternius Animated Monster Pack `Skeleton.fbx` | CC0 | [script] |
| mithril_dragon | Quaternius Animated Monster Pack `Dragon.fbx`, silver metallic material instance; or Ultimate Monsters `Flying/Dragon_Evolved.fbx` | CC0 | [script] |
| Greater Demon (summon) | Ultimate Monsters `Big/Demon.fbx` (or `BlueDemon.fbx`, `Flying/Demon.fbx`), scale 3 | CC0 | [script] |
| Tiger (summon) | Stand-in: Ultimate Animated Animals `Wolf.fbx`, orange material. Fab search "tiger" with the Free filter if a real one is wanted (not checked: Fab blocks scripted reads) | CC0 | [script] |
| Skeleton warrior/mage minions | Same as the skeleton mobs | CC0 | [click] |
| example_brute | Ultimate Monsters `Big/Orc.fbx` or `Yeti.fbx` | CC0 | [script] |
| Bigger, realistic (later) | Paragon: Minions (Epic, free, models + animations + FX; can't use the PARAGON name). Other Paragon heroes (e.g. Kwang, sword) are also free Epic listings. | Epic, UE-only | [Fab] https://www.fab.com/listings/039ea035-9360-4e76-ad06-5d3a92da6f65 |
| Quaternius Bestiary - Dungeon Monsters Kit | 7 monsters with weapons, no animations, "QAL" licence (not CC0; not checked) | QAL | [click]. Skip until the QAL terms are read. |

The Quaternius monsters have built-in animation clips. Ultimate Monsters lists itself as "50 fully
animated monsters". The clip names couldn't be read here (see the download note below), so check
them in the import dialog.

### 4. VFX

| Asset | Covers | Licence | Access |
| --- | --- | --- | --- |
| Niagara Examples Pack (Epic, 2026-01-28, UE 5.7) | 50+ Niagara systems: explosions, impacts, sparks, fire, smoke, lightning; gallery level | Epic, UE-only, free | [Fab]. 5.8 compatibility assumed (Fab lets you pick the engine version when adding) |
| Content Examples (Epic) | Niagara and other example maps (beams, ribbons, meshes) | Epic, UE-only, free | [Fab]. A full project: open it on its own and migrate what you need. |
| Kenney Particle Pack | 80 transparent sprites: fire, flame, magic, spark, smoke, light, trace, star, twirl… | CC0 | [script] |
| Engine built-ins | Niagara emitter templates (Fountain, Simple Sprite Burst, Beam) in the "New Niagara System" wizard | Epic | None. A beam template covers the portal beam and beam spells. |
| Fab Limited-Time Free | Rotates every 2 weeks. 2026-10-06 to 10-20: a bridge environment, a pathfinding plugin, a farm environment (none needed). Past fits: "RPG - Crafting & Environment VFX" (Sept 8-22), "Dragon Cave" (July 28-Aug 11). | Fab Standard | [Fab]. Claimed items stay in the library; unclaimed ones go back to paid. Check https://www.fab.com/limited-time-free each fortnight. |

### 5. Props and environment

| Asset | Covers | Licence | Access |
| --- | --- | --- | --- |
| KayKit Dungeon Remastered | 200 free: walls, floors, stairs, doors, chests, barrels, crates, traps, banners | CC0 | [click] |
| Quaternius Modular Dungeon / Medieval Dungeon packs | Older dungeon kits (FBX/OBJ/Blend) | CC0 | [script] Drive `1CvofJVYHf00StUTsIWtOuyVxeV0CN3UI`, `13MoeF0Uy9PHqXXi6gh63tV2PLRD6YXJv` |
| Quaternius Ultimate Modular Ruins | Ruins kit | CC0 | [script] Drive `1ETp2ldaHaP0BkS4FBmkT-g9Yf88T_cIX` |
| Quaternius Medieval Village MegaKit / Stylized Nature MegaKit (free tiers) | Buildings; rocks, plants (herb and ore node stand-ins) | CC0 | [click] |
| Kenney Mini Dungeon, Castle Kit, Graveyard Kit, Survival Kit, Fantasy Town Kit | Small low-poly kits (survival has rocks/resources) | CC0 | [script] |
| Poly Haven | `treasure_chest`, `large_castle_door`, `large_iron_gate`, `wooden_crate_01/02`, `Barrel_01`, `wooden_lantern_01`, `rock_moss_set_01/02`, `rock_07`, `nettle_plant`, `periwinkle_plant`, `stone_fire_pit` (PBR, 1k-8k) | CC0 | [script] |
| Megascans | Only free for everyone until the end of 2024. Now mostly paid; a free subset is on Fab. Anything claimed in 2024 stays usable. | Fab Standard | [Fab] |

### 6. SFX

| Asset | Covers | Licence | Access |
| --- | --- | --- | --- |
| Kenney Impact Sounds | Metal, plate, wood, mining, punch impacts; footsteps | CC0 | [script] (OGG) |
| Kenney RPG Audio | Knife draw/slice, chop, door open/close, creak, cloth, coins, footsteps | CC0 | [script] (OGG) |
| Sonniss GDC 2026 Game Audio Bundle | 7.47 GB+ pro SFX (17 vendors) | Royalty-free, commercial, no attribution; **no AI/ML training**; can't be resold as sound files | [click] https://gdc.sonniss.com (5 zip parts, Google Drive mirror, or torrent; no account). Scripted curl gets a 403 (Cloudflare), so download it in a browser. Older years (2015-2024) at https://sonniss.com/gameaudiogdc. |
| Freesound (CC0 filter) | Anything | Per-sound; filter to CC0 (CC-BY needs credits; **avoid CC-BY-NC**) | [login]: downloading needs a free Freesound account |

UE 5 imports WAV directly. Assumed: 5.8 also imports OGG; if it doesn't, convert with
`ffmpeg -i in.ogg out.wav`.

## Owner's manual steps for Fab items

1. Sign in on https://www.fab.com with the Epic account. Open each listing and click **Add to My
   Library** (free, "Personal" licence tier is fine under $100k revenue).
   - Niagara Examples Pack (search "Niagara Examples Pack", seller Epic Games)
   - Optional: Paragon: Minions, Content Examples, Game Animation Sample Project
2. In the Unreal editor with Crossguard open: **Window → Fab** (the Fab plugin), **My Library**, pick
   the asset, **Add to Project**. Asset packs land under `Content/<PackName>`.
   - Sample *projects* (Content Examples, GASP, Lyra) can't be added to a project. Create them from
     the Epic launcher's Library (**Create Project**, choose 5.8), then right-click the assets you
     want → **Asset Actions → Migrate** into `Crossguard/Content`.
3. Tell the local Claude session which folders arrived. It can then wire them up with Unreal Python.
4. itch.io [click] items: open the page, **Download Now**, **No thanks, just take me to the
   downloads**, download the zip into `D:\CrossguardArtDrop` (any folder outside the repo). The
   session imports from there. No account is needed. (Scripting the itch flow was tried: its file
   endpoint answers "invalid key" without a browser session, so it stays a manual click.)
5. Mixamo: sign in with Adobe, choose the Y Bot, download each animation as FBX **Without Skin**, 30
   fps, into the same drop folder.

## Direct-download list for a local Claude session

Put downloads outside the repo (e.g. `%TEMP%\CrossguardArt\<pack>`), extract each pack into its own
empty folder, then import into `Content/ThirdParty/<Pack>/` (`Tools/import_art.py`, done 2026-10-16:
see `Content/ThirdParty/LICENSES.md`) with Unreal Python
(`unreal.AssetImportTask` + `unreal.FbxImportUI`, or Interchange for GLB). Import only what's used.
`*.fbx`, `*.png`, `*.wav` and `*.uasset` go through Git LFS here; don't commit the raw zips.

**Kenney (CC0, direct zip, checked 2026-10-16; the hashed path changes when Kenney updates a pack,
so re-read the asset page if a URL 404s):**

```
https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip
https://kenney.nl/media/pages/assets/rpg-audio/8e99002d76-1677590336/kenney_rpg-audio.zip
https://kenney.nl/media/pages/assets/particle-pack/f8fe0f8cb8-1677578741/kenney_particle-pack.zip
https://kenney.nl/media/pages/assets/mini-dungeon/6cd72dc849-1785314274/kenney_mini-dungeon.zip
https://kenney.nl/media/pages/assets/castle-kit/a395102d20-1711543616/kenney_castle-kit.zip
https://kenney.nl/media/pages/assets/survival-kit/4065a8185b-1712149243/kenney_survival-kit.zip
https://kenney.nl/media/pages/assets/graveyard-kit/ba8d4b4517-1760691807/kenney_graveyard-kit_5.0.zip
https://kenney.nl/media/pages/assets/fantasy-town-kit/efe948d309-1754222374/kenney_fantasy-town-kit_2.0.zip
```

**Poly Haven (CC0, stable API).** `GET https://api.polyhaven.com/files/<id>` returns JSON. Take
`fbx["1k"]["fbx"]["url"]` and download every `include` entry (textures) next to it, keeping the
relative `textures/` paths. Checked URLs:

```
https://dl.polyhaven.org/file/ph-assets/Models/fbx/1k/treasure_chest/treasure_chest_1k.fbx
https://dl.polyhaven.org/file/ph-assets/Models/fbx/1k/large_castle_door/large_castle_door_1k.fbx
https://dl.polyhaven.org/file/ph-assets/Models/fbx/1k/kite_shield/kite_shield_1k.fbx
https://dl.polyhaven.org/file/ph-assets/Models/fbx/1k/ornate_medieval_dagger/ornate_medieval_dagger_1k.fbx
https://dl.polyhaven.org/file/ph-assets/Models/fbx/1k/ornate_medieval_mace/ornate_medieval_mace_1k.fbx
https://dl.polyhaven.org/file/ph-assets/Models/fbx/1k/rock_moss_set_01/rock_moss_set_01_1k.fbx
https://dl.polyhaven.org/file/ph-assets/Models/fbx/1k/nettle_plant/nettle_plant_1k.fbx
https://dl.polyhaven.org/file/ph-assets/Models/fbx/1k/wooden_axe/wooden_axe_1k.fbx
```

Other ids: `large_iron_gate`, `wooden_crate_01`, `Barrel_01`, `wooden_lantern_01`, `rock_07`,
`periwinkle_plant`, `stone_fire_pit`. The full model list is at `https://api.polyhaven.com/assets?t=models`.

**Quaternius (CC0, public Google Drive folders, linked from quaternius.com).** List a folder with
`https://drive.google.com/embeddedfolderview?id=<folderId>` (no account). Download a file with
`https://drive.usercontent.google.com/download?id=<fileId>&export=download&confirm=t`, or use
`pip install gdown` then `gdown --folder https://drive.google.com/drive/folders/<folderId>`.

> **Caveat:** on 2026-10-16 every file download from these folders returned Google's **"Quota
> exceeded"** page (popular public files hit Drive's daily download cap). Retry later. If it
> persists, the owner opens the folder in a browser while signed in to Google and uses **Download**
> (zip). Check that a downloaded file starts with `Kaydara FBX Binary` and isn't HTML.

| Pack | Folder | Key FBX file ids |
| --- | --- | --- |
| Modular Weapons | `1Z6vYiQxY8W73FXuMWzaTQAg9rzbumnOr` (FBX: `1kcJNPDGro3s9Eoa9Z4PCf6jCAIPJp_c-`) | Sword `1koacN3RFCwR0VlR3RvJKclqxdd33r0XF`, Sword_Big `16HOeHZZqxqRAIcwbJW3uAFxchiSUjYqF`, Claymore `1CuZEwhlFi0fXO581QiU3RIywE4di4KNf`, Axe `18NnMqrNty-hepNGECyFnFKTNvhNqCcdX`, Axe_Double `1vD-p5RNHmLJVvtFcYcHsTyRFz158eejw`, Dagger `1Z9nuxtwCWOFb1t1Ng4w5mKb_LQwWNsfw`, Spear `1UKAPF9C3A0tFpWC127P3O9J0RVwhHrMN`, Bow_Wooden `1F3SZ5fz7moeQ6j1aaGL-69jwZo-oVLJT`, Arrow `16Hno8FoXy9t94GIXID4MD88jqyaoGb9F`, Shield_Heater `1ieQ2eY-1b5ZhJovZkwlh0MzR3jQj1spY`, Shield_Round `1FLjyaAbHlwaOKtotvkaasXivq17QvEPN` |
| Animated Monster Pack | `102H-oyUM8SGYi1mW0GOgkjEu7xSdaORA` (FBX: `15-rfTmeo1IPNxNs0e6GVqmS_eDItDE3j`) | Skeleton `1xeIknroMtNA6xukcUPpw_AH-gomwVUav`, Dragon `1dM_UUyuQSi73q1ez3NmVBfZZC7Q1kTek`, Bat `1LEs9n1c7HJHZ6xeGPwXmGgRC1I3zWMix` |
| Ultimate Monsters | `18m4KpzpEzhC9wl7jzr6dUc0N8Jozr79C` (Big/FBX: `1JvPqInPVfD3HW-iHpSQJtVAM-XIjB9Aa`, Flying/FBX: `1cYeOI4uWqhHv0qCXMAe6weUSgR6HO1Yi`, Blob/FBX: `1NUUFKG39rPmrv14IoHPYkFcnRQte40x-`) | Big/Demon `1sTTMBk576jvY59k6kI4zNim3elEWqC3g`, Big/BlueDemon `1rOmRJtqgkahKqSZrwiuKbgC_O46AnAaf`, Flying/Demon `11g0xpUxF3NAayc5uNaPFtBIvjWK5n48H`, Flying/Dragon_Evolved `1_ColYvoIS7vj1N4qWrLuTmZmVK_lsu7j` |
| Ultimate Animated Animals | `1uJ3N5HfB7jKTseJUNQr3N4YaN0UuEtHk` (FBX: `13cS1y5LTM8h6dNMQut7aO7zAxxdMgZA2`) | Wolf `128WmNfZthQqZ1OZpinWCAJ8IgvjNcoSm` |
| Ultimate RPG Pack | `1IhdL3W5XdJrjf_axqAnKvEybMaC3FwF-` | (props, characters) |
| Modular Dungeon / Medieval Dungeon / Modular Ruins | `1CvofJVYHf00StUTsIWtOuyVxeV0CN3UI` / `13MoeF0Uy9PHqXXi6gh63tV2PLRD6YXJv` / `1ETp2ldaHaP0BkS4FBmkT-g9Yf88T_cIX` | — |

Each Quaternius folder has a `License.txt` (CC0). Keep a copy next to the imported assets.

## How the art hooks into the code

Placeholder art goes in with **data or Blueprint settings only**; no C++ per asset. Every hook is
cosmetic (it never changes a rule) and every asset reference is soft: empty means "nothing", which
is the default everywhere. Players use the mannequin (`GetMesh()`, hidden from its owner). Monsters
and minions show a capsule-sized cylinder (`ACrossguardAICharacter::PlaceholderMesh`) until a mesh
is set. Asset paths are Unreal object paths: `/Game/Art/Weapons/SM_Sword.SM_Sword`.

### Data fields (regenerate tables with the exporters, reimport in the editor)

| Data | Fields | Effect |
| --- | --- | --- |
| `Data/Items/Weapons.json` ([schema](item-data-schema.md#weapon-art-optional)) | `mesh`, `attachSocket` (default `hand_r` / `hand_l`), `relativeTransform` | The drawn set's weapons and shield show in the hands (see Held weapons below). |
| `Data/Items/Items.json`, Ammo and Throwable ([schema](item-data-schema.md#projectile-art-optional)) | `projectileMesh`, `trailVfx`, `impactVfx`, `impactSound` | Arrows (the bow's `ammo` row) and thrown items show a mesh and trail, and play an impact. Over the projectile Blueprint's own. |
| `Data/Mobs/Mobs.json` ([schema](mob-data-schema.md#art-optional)) | `mesh`, `animBlueprint`, `meshScale`, `meshOffset`, `meshYaw` | The monster's body replaces the cylinder on every machine. One spawner can mix looks. |

### Events (Blueprint: override in `BP_CrossguardCharacter` / a monster Blueprint)

All on `ACrossguardCharacter` (players, monsters and minions alike), run on every machine that shows
the character, with a native C++ delegate of the same name plus `Native` for tests.

| Event | When | Plays by itself (character defaults, `Crossguard\|Art`) |
| --- | --- | --- |
| `OnAttackStarted(Cue)` | A melee swing, shot, throw or skill cast starts (players: the owner plays its prediction at once). `Cue`: `Kind` (Melee / Ranged / Throw / Cast), `bHasWeapon`, `WeaponType`, `Slot`, `AttackIndex` (counts up: `% 3` picks `MM_Attack_01-03`), `Duration` (seconds to the next attack; 0 for casts), `SkillId`. | `SwingSound`; the placeholder swing (below). |
| `OnAttackHit(HitLocation, Target)` | This character's direct hit landed (not damage over time). | `HitSound` at the hit. |
| `OnDamageTaken(Amount, Source, HitLocation, bBlocked)` | It took damage (server's damage path), or a guard stopped a hit whole (`Amount` 0, `bBlocked`). `bBlocked` also when a guard let part through. Hit react, blood or sparks, damage numbers. | `HitVfx` at the hit. |
| `OnWeaponClank(ImpactPoint)` | Weapons met (block, parry, clash). Native: `OnWeaponClankNative` (character, point). | `BlockSound` and `ClankVfx` at the point. |
| (death) | The ragdoll starts. | `DeathSound`. |
| `ACrossguardProjectile::OnProjectileImpact(Location)` | A projectile hit or exploded. | `Visual.ImpactVfx`, `Visual.ImpactSound`. |

### Where to put art

| Thing | Class | Where to put art |
| --- | --- | --- |
| Player body (others see it) | `ACrossguardCharacter` → `Content/BP_CrossguardCharacter` | `Mesh` = `SKM_Manny_Simple` + an Anim Blueprint. The ragdoll on death already uses this mesh (`Ragdoll` profile, Physics Asset `PA_Mannequin`). The body container copies it (`ACrossguardLootContainer::SetBodyMesh`). Play attack / hit-react montages from the events above. |
| First-person view | `FirstPersonCamera` (`UCameraComponent`, protected, BlueprintReadOnly) | In the BP: on the camera turn on First Person Field Of View (e.g. 70) and First Person Scale (e.g. 0.6). Add arms as a Skeletal Mesh Component with the component tag **`FirstPersonArms`** (held weapons then attach to its hand sockets) and **First Person Primitive Type = First Person**. Keep `Mesh` as the **World Space Representation** for shadows. The body is `SetOwnerNoSee(true)` in C++. |
| Held weapons and shields | `UCrossguardWeaponVisualsComponent` (`WeaponVisuals` on the character) | Data: `mesh` per weapon row. Two copies per hand: first person (only its player sees it) on the `FirstPersonArms` hand socket, or without arms on the camera at `FirstPersonCameraOffset` (main hand) / `FirstPersonCameraOffsetOffHand` (the first feel test); third person (everyone else) on the body's hand socket. Shown while the set is drawn and in hand; hidden while drawing, holstered, or holding an item. Try a mesh without the table: `MeshOverrides` (weapon id → mesh) on the component. `OnWeaponMeshesChanged` fires after each rebuild (set First Person Primitive Type on `GetWeaponMesh(Slot, true)` there). |
| Placeholder swing | `UCrossguardWeaponVisualsComponent::bPlaceholderSwing` (on) | Until animations: an attack turns the first-person weapon down through `PlaceholderSwingDegrees` (70) and back over ~80% of the attack interval (melee), or pulls it back `PlaceholderRecoil` cm (ranged). Turn it off once montages play. |
| Hit, swing, block, death sounds; hit and clank sparks | Character defaults, `Crossguard\|Art` | `SwingSound`, `HitSound`, `BlockSound` (Kenney `impactMetal_heavy_*`), `DeathSound`, `HitVfx`, `ClankVfx` (Niagara). |
| Player arrows / bolts | `UCrossguardWeaponAttackAbility::DefaultProjectileClass`, `ProjectileClasses[weapon id]` (class `ACrossguardProjectile`) | Data: the ammo row's `projectileMesh` / `trailVfx` / `impactVfx` / `impactSound`. Or a BP subclass of `CrossguardProjectile` with its `Visual` defaults set (mesh, trail, impact), set on the attack ability Blueprint (`WeaponAttackAbilityClass` on the character). A staff without ammo: the BP route. |
| Monster arrows | `ACrossguardAICharacter::ArrowClass` | A projectile BP with `Visual` set. |
| Fireball | `UCrossguardFireballAbility::ProjectileClass`, `ACrossguardAICharacter::FireballClass` (`ACrossguardSpellProjectile`) | BP subclass with `Visual.TrailVfx` (fire), `Visual.ImpactVfx` (explosion), `Visual.ImpactSound`. |
| Thrown items | `UCrossguardInventoryComponent::ThrownProjectileClass` (`ACrossguardThrownProjectile`), `DamageAreaClass` (`ACrossguardDamageArea::AreaMesh`) | Data: the throwable's projectile fields. The damage area: a BP with a mesh / Niagara. |
| Monsters | `ACrossguardMonster` | Data: the mob's `mesh` / `animBlueprint` / `meshScale` / `meshOffset` / `meshYaw`. Or a BP per look (`BP_Monster_Skeleton`) on the spawner's `MonsterClass` with `Mesh` + Anim BP and `PlaceholderMesh` hidden. The Mob Table's `Scale` still applies. |
| Minions (skeletons, tiger, demon) | `ACrossguardMinion`; summon abilities | `WarriorVisual` / `MageVisual` (Summon Skeletons) and `CompanionVisual` (Summon Tiger, Summon Greater Demon) on the summon ability Blueprints: mesh, Anim BP class, scale, offset, yaw. Or BP subclasses on `WarriorClass`, `MageClass`, `CompanionClass`. |
| Extraction portal | `ACrossguardExtractPortal` (`BeamMesh`, `PortalMesh`, `OnStateChanged`); character `PortalClass` | BP subclass: a beam mesh or Niagara beam while channelling, a swirl when open. |
| Chests and bodies | `ACrossguardLootContainer` | BP with a Static Mesh (KayKit chest / Poly Haven `treasure_chest`). |
| Doors | `ACrossguardDoor::DoorMesh` | Set the mesh in a BP (KayKit door / Poly Haven `large_castle_door`). It snaps open until animated. |
| Herb / ore nodes | `ACrossguardGatherNode::NodeMesh` (default sphere) | BP per kind: Poly Haven `nettle_plant`, `rock_moss_set_01` (tint per ore). |
| Wall skill | `ACrossguardWall::Mesh` (cube) | Optional stone wall mesh. |
| Dropped items | `ACrossguardWorldItem::Mesh` (cube) | Later: a per-item mesh. |
| UI | [ui.md](ui.md), "Replacing it" | Unchanged by this plan. |

### Gaps

The four code gaps of the first version of this plan are closed (2026-10-16): per-mob meshes from
data, attack-started events, weapon meshes from the weapon table, and hit / damage events. Left:

1. Assets load synchronously the first time they are used (a hitch on the first swing, hit or
   spawn). Fine for placeholders; real art should preload or load asynchronously.
2. The capsule is not fitted to a monster's mesh: size mobs with `scale`.
3. Monsters' held weapons are not shown (mobs have a weapon type, not a weapon item). A weapon mesh
   on a monster: a socket-attached Static Mesh in its Blueprint, or a later data field.
4. No footstep hook (anim notifies in the Anim Blueprint do that without code).
5. Monster animations without an Anim Blueprint: `animations` in Mobs.json (idle, move, attack, death by
   state; 2026-10-16). No blending, no hit reacts, no Physics Assets for monsters yet (no ragdoll).

## Sources (checked 2026-10-16)

- Quaternius: https://quaternius.com (pack pages list "CC0"; Drive folders above; itch https://quaternius.itch.io)
- KayKit: https://kaylousberg.itch.io/kaykit-skeletons , https://kaylousberg.itch.io/kaykit-character-animations , https://kaylousberg.itch.io/kaykit-dungeon-remastered (CC0)
- Kenney: https://kenney.nl/assets (CC0)
- Poly Haven: https://polyhaven.com , API https://api.polyhaven.com (CC0)
- UE First Person Rendering: https://dev.epicgames.com/documentation/unreal-engine/first-person-rendering
- UE 5.6 templates: https://www.unrealengine.com/news/updated-game-templates-for-unreal-engine-5-6available-now
- GASP for 5.8: https://www.unrealengine.com/tech-blog/download-the-latest-game-animation-sample-project-now-updated-for-ue-5-8
- Niagara Examples Pack: https://www.unrealengine.com/news/discover-over-50-free-niagara-systems-ready-to-use-in-unreal-engine-5-7
- Fab licences: https://dev.epicgames.com/documentation/fab/licenses-and-pricing-in-fab
- Fab limited-time free thread: https://forums.unrealengine.com/t/fab-limited-time-free-content/2738884
- Sonniss: https://gdc.sonniss.com , https://sonniss.com/gameaudiogdc
- Mixamo FAQ (community copy): https://community.adobe.com/t5/mixamo-discussions/mixamo-faq-licensing-royalties-ownership-eula-and-tos/m-p/13234775

Fab listing pages and fab.com search block scripted reads (HTTP 403). Fab items above were confirmed
through search results and Epic posts, not by opening the listing. Prices shown in Fab at the time
of claiming are what count.
