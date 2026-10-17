# PC session report, 2026-10-16 (for the cloud sessions)

Task: [next-pc-session.md](next-pc-session.md), steps 1 to 5 done on the owner's Windows PC
(UE 5.8.3 launcher, Docker Desktop). Step 6 (source engine) not started. Everything is on
`foundation/core-rules`. Fix-by-fix detail: [first-compile-triage.md](../first-compile-triage.md),
"Batch 3 and 4 build results".

## Results

| Run | Result |
| --- | --- |
| Build (batch 3 + 4) | Green after 2 fixes: `ACrossguardGameMode` had lost its closing `};` (5d15df7); a `nullptr` / `MakeShared` ternary in `UCrossguardNakamaSaveBackend::Configure`. No triage row fired. |
| Headless tests | 166 / 166 (full run after the art); the new `Art.MobStateAnimations` passed in the `Crossguard.Scenario.Art` run (5 / 5) |
| Networked (`Crossguard.Net`) | 10 / 10, `SeamlessTravelHubToRaid` included |
| Visual | 8 / 8 earlier in the session (every window fits 1280 × 720); the new `Visual.MobArt` passes (looked at: lineup, action, dragon, weapons). The other 8 were not rerun after the art. |
| `backend_smoke.py` | 70 ok, 1 skip (expired ticket: needs `CROSSGUARD_MATCH_TICKET_TTL_SEC=10`), 0 fail. The task said 72. |
| Local cluster (`run_local_cluster.bat -Backend`) | The whole loop works with two clients: log in, hub, party, matchmaking, ticket, match server, extraction, team back on its hub with the party. Saves correct in Nakama (`inRaid` false, XP banked). No `version conflict` / `lease lost`. Checklist section 7: every row passed. |

## Test fixes (tests were wrong, not the game)

- HubReturn read a player state after extraction (unpossess clears it): crash.
- Nakama / party tests built GameInstance subsystems with the transient package as outer.
- Hub UI tests counted the owner's Debug inventory rubies on the PIE player.
- `Art.WeaponMeshes` assumed the dagger has no mesh (the wand now).
- RV8 checked: `UI.PanelsBind` tests that the saved FOV is on the camera.

## Bugs found playing the cluster (all fixed)

- `NakamaUrl=http://...` unquoted in `DefaultGame.ini` was read as `http:` (login: no response). Quoted.
- The menu's UI-only input outlived the travel to the hub (no movement, no keys). The HUD starts in game input.
- Players were named after the PC, so invites by name failed. Non-Shipping: `-CrossguardPlayerId=` is the name.
- `L_DevSandbox` had no extraction zone or point of interest (`editor_setup` RAID_ACTORS adds them).
- The party window showed `refused (AlreadySearching)`: readable lines now.
- The menu never said why a Join was refused: `UCrossguardConnectionErrorSubsystem` ("Connection failed: No match ticket: ...", "Log in first: ...").
- New: **F10** back to the main menu between raids (refused in a raid). Placeholder key.
- Also: UI text fields were oversized and pale; the attributes rules line overflowed.

Expected, not a bug: a hub restarted with the same `-ServerId=` takes its own leases back at once
(no 60 s wait).

## Art (placeholder, all wired through data / Blueprint settings)

Licences: `Content/ThirdParty/LICENSES.md`. Importer: `Tools/import_art.py` (Unreal Python; downloads
stay outside the repo). Choices are listed as "Assumed (placeholder)" in `design-decisions.md`, "Art hooks".

- Weapons: Quaternius Medieval Weapons (sword, big sword, dagger, axes, spear, heater shield, bow); KayKit staff. Wand none.
- Monsters: KayKit skeletons (soldier Warrior, archer Rogue, mage Mage) with KayKit animations; example brute Quaternius Orc; mithril dragon Quaternius Dragon_Evolved. Textured, facing right, animated.
- New code (owner's choice): monsters without an Anim Blueprint play `animations` from Mobs.json by state (idle, move, attack, death). `UCrossguardArtLibrary` sets "mesh proportions" on KayKit's shared skeleton.
- Effects: Epic Niagara Examples Pack (Fab, **UE-only licence**): only the 6 systems in use and their dependencies are committed (105 files, 87 MB); the rest of the 1.2 GB pack is in `.gitignore` and stays on the PC. Hits, block clank, arrow trail and impacts, thrown metal, bottle explosion.
- Sounds: Kenney (swing, hit, block, death, impacts).
- Also in: Poly Haven props (chest, door, nettle, rocks) for Blueprints later; Kenney leftovers.

Not done / untuned:
- Weapon angles in the hand (the sword points oddly): tune `relativeTransform` rotation in Weapons.json.
- No Physics Assets for monsters: death animation instead of a ragdoll.
- Fireball and other spell visuals: they need Blueprint subclasses of the projectile / ability (not data).
- Arrows have no mesh (Quaternius' arrow lies along Y; the projectile mesh has no rotation field).
- Not imported yet: KayKit Dungeon, Quaternius animals (wolf for the tiger summon) and Animated Monster Pack.

## Still needs the owner

- Look at the new art in a raid (cluster or PIE) and say what looks wrong.
- Optional step 6: the source engine (hours, ~250 GB; starts with OWNER steps).

## Notes for the next session

- The PC has no system Python: use `C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe -I`.
- Close the cluster before building or saving maps (it locks the DLL and the assets).
- The Build Paths step of L_Hub was done by the owner in the editor; a scripted nav build stays locked (`AsyncLoadLock`).

## Follow-ups after the owner's playtest (same day)

- Monsters walked sideways on clients: the body mesh's data yaw was lost to network smoothing. Fixed
  (`CacheInitialMeshOffset`); test `Net.MobMeshYawOnClient` fails without the fix.
- New characters started empty (the save overwrote the Debug loadout). Ruled: a **starter kit** for
  every new character (placeholder: Common one-handed sword and shield, 3 healing potions,
  3 bandages; game mode **Crossguard | Starter Kit**), given once before the first save.
- First-person weapons (no arms yet): tilted 45 degrees forward, at 60% size.
- Ruled: mobs walk 2.5 m/s and attack half as often (`design-decisions.md`, "Monster pace").
- Magic visuals: Fireball (spell projectiles) fire + explosion from `DefaultGame.ini`; the staff's
  bolt from Weapons.json `projectile` (new field); a wand mesh (KayKit staff at 0.17). The wand stays
  hitscan.
- Headless 169 / 169, net 11 / 11 after these.
