# Task: next PC session: build everything, run the tests, start the online stack, then placeholder art
n**Status 2026-10-16: steps 1-5 done; report: [pc-session-report-2026-10-16.md](pc-session-report-2026-10-16.md).**

This file replaces `build-batch-3.md`, whose steps are included here. Run it in a Claude Code session
on the owner's Windows PC, environment **Local**, in the Crossguard folder.

Read these first:
- `CLAUDE.md`
- `docs/pc-session.md`
- `docs/design-decisions.md`, section "Online architecture"

Working rules:
- commit and push to `foundation/core-rules` after each green step;
- flag every assumption;
- keep reports short and plain.

The last green run was 143 headless, 8 net and 7 visual. Everything since was written in the cloud
and never compiled. The guessed engine calls are in `docs/first-compile-triage.md`, under
"Batch 3 (unbuilt)" and "Batch 4 (unbuilt)".

## What's new

| Area | Docs |
| --- | --- |
| Batch 3: rarity contrast, respec window (K), party window and invite inbox (P), team list with disconnected marker, stash and vendor screens, main menu and hub maps, raid portal, party board, settings, art hooks, Demon reach 9 m | `ui.md`, `hub-and-menu.md`, `art-placeholder-plan.md` |
| Architecture: dedicated match and hub servers. One-PC hosting is dev mode only (`IsDevListenMode`). The matchmaking interface is async. Hub destination order. One match per server process (`-KeepMatchServer` keeps it up). Raid time limit. | `hub-and-menu.md`, `dedicated-server-plan.md` |
| Backend (Nakama): Docker stack, session check at login (`PreLoginAsync`), Nakama save backend with leases, parties stored in Nakama, matchmaking queue and server registry, match tickets, hub shard pick, menu Log in | `backend-setup.md`, `persistence.md`, `parties.md` |

## Steps

1. **Build.** `git pull`, then `Tools\build_editor.bat`. Fix compile errors with the triage rows.
   Keep fixes minimal and don't change game rules. Commit.

2. **Set up the maps.** Run `Tools\editor_setup.bat -SkipBuild -CreateHub -CreateMenu -AddHubActors`.
   Open `L_Hub` once and run Build Paths. Commit.

3. **Test.** Run `Tools\run_editor_tests.bat -SkipBuild`, then the networked and `-Visual` runs.
   Triage as usual. Pay particular attention to:
   - `Net.SeamlessTravelHubToRaid`, `HubReturn.*`
   - `Scenario.UI.PanelsBind`, `UI.StashAndVendor`, `Hub.*`, `Art.*`
   - `Crossguard.UI.RarityContrast`
   - `Crossguard.Nakama.*`, and the party raid-launch scenario
   - the visual frames: readable text, windows fit at 1280×720

   Also check RV8: the saved FOV of 90 is now applied to the camera.

4. **Local online stack.** Needs Docker Desktop (**OWNER** installs it if it's missing).
   1. Run `Tools\run_local_backend.bat`.
   2. Run `python Tools\backend_smoke.py`. It should pass 72/72.
   3. Run `Tools\run_local_cluster.bat -Backend`. This starts a hub server, a match server and two
      clients from the editor binary; no source engine is needed.
   4. Debug the PowerShell scripts as needed. They have never run on Windows.
   5. Play the loop with both clients: log in from the menu, then Play to join the hub. Form a party,
      then use the raid portal (matchmaking, then the match server). Extract, then come back to the hub.
   6. Check the saves in the Nakama console at 127.0.0.1:7351.
   7. Work through `docs/next-build-checklist.md` sections 5b and 7.

5. **Placeholder art: starter import.** Follow `docs/art-placeholder-plan.md`, "Direct-download list".
   1. Download what a script can fetch: Kenney, Poly Haven, and Quaternius if Google Drive allows.
      Put it under `Content/ThirdParty/<Pack>/`.
   2. Import it with a new `Tools/import_art.py` (Unreal Python).
   3. Hook it up through data and Blueprint settings only:
      - weapon `mesh` fields in `Data/Items/Weapons.json` (then re-export and reimport);
      - mob `mesh` and `animBlueprint`;
      - projectile visuals;
      - the character's sound slots.
   4. Record the licences in `Content/ThirdParty/LICENSES.md`.
   5. List what still needs the owner: the itch.io KayKit packs, Fab's Niagara Examples, and any
      Quaternius pack Drive refused.

6. **Optional, if time allows and the owner agrees: the source engine.** Start
   `docs/tasks/setup-source-engine.md`. It begins with **OWNER** steps: link GitHub to the Epic
   account and accept the invite. The full build takes hours and about 250 GB.

7. **Report:**
   - build fixes made, with their triage rows;
   - pass / fail / skip counts per run;
   - backend smoke result;
   - what worked in the cluster loop;
   - anything weakened, and why;
   - game bugs found;
   - art imported and what the owner must download;
   - what still needs the owner.
