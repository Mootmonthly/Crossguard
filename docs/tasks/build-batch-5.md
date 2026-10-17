# Task: build and test batch 5 (local PC session)

Run this in a Claude Code session on the owner's Windows PC (environment **Local**, Crossguard folder).
Read these first: `CLAUDE.md`, `docs/pc-session.md`, and the last report
`docs/tasks/pc-session-report-2026-10-16.md`.

Working rules:
- Commit and push to `foundation/core-rules` after each green step.
- Flag every assumption.
- Keep reports short and plain.

The last green run was 169/169 headless+net, 8/8 visual (plus `Visual.MobArt`), and backend smoke 70 ok.
None of the work below has been compiled. The guessed engine calls are in `docs/first-compile-triage.md`
"Batch 5 (unbuilt)": rows AT1–AT10 (art) and GN1–GN14 (guilds, Steam, sessions).

| Area | Docs |
| --- | --- |
| Guilds on Nakama: guild storage, a group mirror, and an atomic bank with an append-only log; the guild window is unchanged | `guild-rules.md` "On Nakama", `backend-setup.md` |
| Steam login (menu Log in), session refresh | `steam-setup.md` |
| Monster ragdolls (`deathMode`), projectile mesh rotation and scale (arrow mesh), `CgReloadArtData` | `art-placeholder-plan.md` |

## Steps

1. **Build and set up.**
   1. Run `git pull`, then `Tools\build_editor.bat`. Fix compile errors with the triage rows. Commit.
   2. Run `Tools\editor_setup.bat -SkipBuild`; the item and mob tables changed.
2. **Tests.** Run `Tools\run_editor_tests.bat -SkipBuild`, then the networked and visual runs. Look closest at:
   - `Crossguard.Nakama.GuildState`, `GuildBankWrite`, `Session`
   - `Net.MonsterDeathRagdoll`
   - `Art.ProjectileMeshRotation`, `Art.ReloadArtData`
3. **Backend.**
   1. Run `Tools\run_local_backend.bat`, then `python Tools\backend_smoke.py`. Expect about 120 ok.
   2. Run `Tools\run_local_cluster.bat -Backend`.
   3. Work through `docs/next-build-checklist.md` section 8: guild create, invite, ranks, and bank deposit and withdraw across a raid trip.
4. **Art follow-ups** (editor work; script it where possible):
   - Make Physics Assets for the KayKit skeleton and the Quaternius orc and dragon meshes. Then check monster ragdolls in PIE.
   - Check the arrow flies tip-first. If it flies backwards, set `projectileRotation` yaw to 90.
   - Show the owner the `CgReloadArtData` tuning loop for weapon angles and monster sizes.
5. **Steam (OWNER steps first, `steam-setup.md`).** Do this only if the owner has done the OWNER steps.
   1. Enable the Steam online subsystem and add its config.
   2. Put `STEAM_APP_ID=480` and the publisher Web API key in `Backend\.env`.
   3. Test Log in in Standalone with Steam running.

   If Steam refuses the publisher key for app 480, report it: the owner then needs their own Steam app id.
6. **Report.** Write `docs/tasks/pc-session-report-<date>.md` covering:
   - build fixes, with their triage rows;
   - pass/fail/skip counts per run;
   - the smoke test result;
   - what worked in the guild-over-raid check;
   - the ragdoll and arrow results;
   - anything weakened, and why;
   - game bugs found;
   - what still needs the owner.
