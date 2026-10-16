# Task: build, test and fix batch 3, then start placeholder art (local PC session)

Run in a Claude Code session on the owner's Windows PC, environment **Local**, in the Crossguard
folder. Read `CLAUDE.md` and `docs/pc-session.md` first. Working rules: commit and push to
`foundation/core-rules` after each green step, flag every assumption, keep reports short and plain.

The last green run was 143 headless, 8 net and 7 visual. Nothing in this batch has been compiled.
The guessed engine calls are in `docs/first-compile-triage.md` under "Batch 3 (unbuilt)", in tables
ST, SV, AP, HM, AH, HR, TR and Review.

| Area | Docs |
| --- | --- |
| Readable rarity colours; attributes/respec window (K); party window with invite inbox (P); team list with a disconnected marker | `ui.md` |
| Stash chest and vendor NPC with their screens | `ui.md` |
| Main menu (`L_MainMenu`), hub (`L_Hub`), hub marker, raid portal, party board, settings (sensitivity, FOV, volume) | `hub-and-menu.md` |
| Real hub → raid → hub seamless travel test; return to the hub when everyone is done; team early return; dead players ride back | `hub-and-menu.md`, `parties.md` |
| Art hooks: weapon meshes from data, attack/damage/impact events, placeholder swing, projectile visuals, per-mob meshes, sound slots; Niagara module | `art-placeholder-plan.md` |
| Greater Demon reach 9 m | `design-decisions.md` |

## Steps

1. **Build.** Run `git pull`, then `Tools\build_editor.bat`. Fix compile errors using the triage
   rows. Keep fixes minimal and don't change game rules. Commit.

2. **Set up.** Run `Tools\editor_setup.bat -SkipBuild -CreateHub -CreateMenu -AddHubActors`.
   - Open `L_Hub` once and run Build Paths for the nav mesh. Commit the maps.
   - `-CreateMenu` makes `L_MainMenu` the game's default map. The editor startup map stays as it is.

3. **Test.** Run `Tools\run_editor_tests.bat -SkipBuild`, then the networked and `-Visual` runs.
   Triage as usual. Pay particular attention to:
   - `Net.SeamlessTravelHubToRaid` (now there and back, via `ExpectServerTravel`) and `HubReturn.*`;
   - `Scenario.UI.PanelsBind`, `UI.StashAndVendor`, `Hub.*` and `Art.*`;
   - `Crossguard.UI.RarityContrast`;
   - in the visual frames: stash, vendor, attributes, party and team list. Check the text is
     readable and the windows fit at 1280×720.

   Also check RV8: the saved FOV (90) is now applied to the camera. If the Blueprint camera used a
   different FOV, update the reference screenshots or the default.

4. **Play the full loop in PIE** with 2 players, if you can script it; otherwise list it for the owner:
   1. Menu → Play → hub.
   2. Form a party at the board, then stash and vendor.
   3. Use the portal to go to the raid. Extract.
   4. Countdown, then back to the hub with your gear.

5. **Placeholder art: starter import** (`docs/art-placeholder-plan.md`, "Direct-download list").
   - Download what a script can fetch: the Kenney sound and particle packs, the Poly Haven props,
     and the Quaternius packs if Google Drive allows. Put them under `Content/ThirdParty/<Pack>/`
     and import them with an Unreal Python script. Keep that script in `Tools/`, for example
     `Tools/import_art.py`.
   - Hook the art in through data and Blueprint settings only, as "How the art hooks into the
     code" describes:
     - weapon `mesh` fields in `Data/Items/Weapons.json` (then re-export and reimport);
     - mob `mesh` and `animBlueprint` fields;
     - projectile visuals;
     - the character's sound slots.
   - Keep licences in `Content/ThirdParty/LICENSES.md`.
   - List what still needs the owner: the itch.io KayKit downloads, Fab's Niagara Examples, and any
     Quaternius pack Drive refused.

6. **Report:**
   - build fixes made, with their triage rows;
   - pass / fail / skip counts per run;
   - anything weakened, and why;
   - game bugs found;
   - art imported and hooked up, and what the owner must download;
   - what still needs the owner.
