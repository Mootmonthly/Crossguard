# Task: multi-client and visual scenario tests (local PC session)

Run this in a Claude Code session on the owner's Windows PC (environment **Local**, project folder =
the Crossguard clone). Read `CLAUDE.md` and `docs/pc-session.md` first. Follow the working rules:
commit and push to `foundation/core-rules` after each step that builds and passes; flag every
assumption; short plain reports.

Goal: cut the owner's hands-on testing down to feel, look and real internet play by automating the
rest of `docs/next-build-checklist.md`. Today the scenario framework (`Source/Crossguard/Private/Tests/CrossguardScenario.h/.cpp`,
guide in `docs/editor-tests.md`) runs one standalone, server-side PIE with no rendering
(`-nullrhi`). Coverage table: `docs/editor-tests.md` → "Checklist coverage" (rows marked `part` /
`manual`).

Start with `git pull`, `Tools\build_editor.bat`, `Tools\run_editor_tests.bat -SkipBuild` to confirm
103/103 green before changing anything. Keep it green after every step.

## Step 1: multi-client mode

Add a networked option to `FScenario`, e.g. `Scenario.Clients(N)` (listen server + N−1 clients, or
dedicated server + N clients) using PIE in one process: `ULevelEditorPlaySettings`
`SetPlayNetMode(PIE_ListenServer / PIE_Client)`, `SetPlayNumberOfClients`, `SetRunUnderOneProcess(true)`.
Find the 5.8 API in `LevelEditorPlaySettings.h` / `PlayInEditorDataTypes.h`.

- Context helpers to reach each world: `C.ServerWorld()`, `C.ClientWorld(i)`, and to find the client
  copy of a server actor (match by `GetNetGUID` via the net driver, or by a replicated name/id).
- Wait helpers that poll a client-side condition (replication is async), e.g.
  `WaitUntilOnClient(i, ...)`.
- Players: each PIE client already has a real `ULocalPlayer` and controller. Use those instead of
  `SpawnPlayer` in networked scenarios. Keep the standalone path exactly as it is.
- Document the API in `docs/editor-tests.md`. Add an "engine calls" note for anything uncertain.

Then write networked tests for the rows that need a real second client:
- `Equipment.Wielding.*` tags arrive on the other client after an equip (checklist 6).
- A death ragdolls on every client: body physics simulating / torn off on the client copy (checklist 6).
- Stealth: the enemy client loses the character (actor gone or channel closed). Measure how long
  that takes; this is the possible ~5 s `RelevantTimeout` leak in `docs/anti-cheat.md`. The ally
  client keeps it. It reappears cleanly on the enemy client when stealth ends. If the leak is real,
  report the measured time and the two fixes from `docs/anti-cheat.md`, and do not pick one without
  the owner.
- Concurrent loot from two real clients' RPCs never duplicates items (checklist 6).
- Hunter's Tracking marks exist on the tracker's client only.

## Step 2: visual mode

Add a rendering run alongside the headless one: a `-Visual` switch on `Tools\run_editor_tests.ps1`
that runs without `-nullrhi` at a fixed resolution, plus a filter (e.g. `Crossguard.Visual.*`).

- Add `C.Screenshot(Name)` (`FScreenshotRequest` / `HighResShot`, or the automation screenshot
  API), saved under `Saved\EditorTests\Screenshots\<Test>\<Name>.png`.
- Visual tests capture key moments: a stealthed player from an enemy's and an ally's view, a
  ragdolled body, a gather node before and after depletion, a chest, a monster mid-attack, the
  Dragonification form.
- After a run, open the PNGs yourself and report anything broken: missing meshes, a black screen,
  wrong visibility. Do not judge art quality; that is the owner's call.
- Visual tests are opt-in, not part of the default run.

## Step 3: close the `part` rows

For each `part` row in the coverage table, add the missing checks where they are automatable:
- fireball self-splash within 2 m;
- Flight descend (Left Ctrl);
- gather interrupted by attacking and by damage;
- skeletons following the caster;
- the tiger leap and its +7 / +10 Magic riders;
- the Greater Demon raising Eldritch Shield and hitting with riders;
- the spawner's weighted mix and 3–5 count, statistically, with a seed or many spawns;
- the mithril dragon casting its summons;
- `skeleton_soldier` mirror damage and 1 s stun on a parried hit;
- the monster tests at the real 15 m / 30 m / 40 m numbers.

Use a NavMesh where pathing matters. A test map with `NavMeshBoundsVolume` built headless, or
runtime nav generation, are both fine; document which.

Fix framework and test bugs freely. Fix a game bug only when `docs/design-decisions.md` makes the
intended behaviour clear; otherwise list it for the owner. Never weaken or skip a test to get green
without saying so in the report.

## Done when

- The default run and the networked run are green; the visual run produces screenshots you have
  reviewed.
- The `docs/editor-tests.md` coverage table is updated, so every row is `auto`, or `manual` with a
  reason that is truly human-only (feel, art, internet play).
- Report:
  - pass / fail / skip counts per mode;
  - anything weakened and why;
  - game bugs found;
  - what still needs the owner.
