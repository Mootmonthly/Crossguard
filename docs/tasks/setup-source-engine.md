# Task: UE 5.8 source engine, dedicated server build, local cluster (local PC session)

Run in a Claude Code session on the owner's Windows PC, environment **Local**. Read `CLAUDE.md`,
`docs/pc-session.md` and `docs/dedicated-server-plan.md` (the plan; this file is the order of work).
Working rules: flag every assumption, keep reports short, commit and push only repository changes to
`foundation/core-rules` (never `git add -A`). Steps marked **OWNER** need the owner at the PC; ask,
then wait.

Long steps (clone, Setup.bat, engine build, Linux build) run for an hour or more: run them in the
background, check their logs, and tell the owner what is running and roughly how long.

## Steps

0. **Check the PC.** Free space on the target drive (`Get-PSDrive`): need **250 GB** (300 GB
   comfortable, SSD). RAM (16 GB → `-MaxParallelActions=6`). Visual Studio version and workloads
   (plan 1c). Launcher engine version (`...\UE_5.8\Engine\Build\Build.version`). Report and stop
   if space is short.

1. **OWNER: GitHub access.** epicgames.com → Apps and Accounts → GitHub → Connect; accept the
   **@EpicGames** invite email within 7 days (plan 1a). Then check with
   `git ls-remote --tags https://github.com/EpicGames/UnrealEngine.git "refs/tags/5.8*"` (Git asks
   the owner to sign in to GitHub in the browser the first time).

2. **Clone** the tag matching the launcher's 5.8 version, shallow, to a short path
   (`D:\UE\UE_5.8_Source`; ask the owner which drive). Plan 1b.

3. **Setup.bat, GenerateProjectFiles.bat** in the engine root. **OWNER**: accept the UAC prompt and
   the prerequisites installer. Plan 1c.

4. **Build the engine** (UnrealEditor, ShaderCompileWorker, UnrealPak, CrashReportClient,
   UnrealVersionSelector; plan 1d). Check it is registered under
   `HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds` (plan 1e).

5. **Build copy of the project** (plan 1e "Two engines side by side"): `git worktree add --detach
   D:\Crossguard-Server origin/foundation/core-rules`, `git lfs pull` there, and
   `setx UE_SOURCE_ENGINE_DIR D:\UE\UE_5.8_Source`. Do not change `EngineAssociation` in the main
   folder; the owner keeps the launcher editor. (If the owner prefers switching in place, follow
   plan 1e "Switching the project in place" and never commit the GUID.)

6. **Win64 server.** In `D:\Crossguard-Server`: `Tools\build_server.bat`. Fix compile errors in the
   main folder (commit there, push, update the build copy), not in the build copy. Then
   `Tools\build_server.bat -Package -WithClient`.

7. **Local cluster.**
   - Editor mode from the main folder (launcher engine): `Tools\run_local_cluster.bat`, then the
     smoke run of plan 3c. **OWNER**: allow the firewall prompt (private networks).
   - Packaged mode from the build copy: `Tools\run_local_cluster.bat -Mode Packaged
     -ClientBuildDir Saved\Packages\Server`.
   - Fix what breaks in the two scripts (they were written without a Windows run). Report which
     checklist items (plan part 4) showed up.

8. **Linux.** **OWNER**: run the v26 clang-20.1.8 toolchain installer (plan 2b). New terminal,
   `GenerateProjectFiles.bat` in the engine, then `Tools\build_server.bat -Platform Linux` and
   `-Package -Platform Linux`. Docker (plan 2d) only if the owner has Docker Desktop installed or
   wants it (**OWNER** installs).

9. **Editor tests** in the main folder as usual (`Tools\run_editor_tests.bat -SkipBuild`): they keep
   using PIE and the launcher engine; nothing should change.

10. **Report**: times and disk actually used per step (update the *estimate* figures in the plan),
    what failed and how it was fixed, which checklist items reproduced. Do not start the code
    changes of the checklist in this task: they go into their own task after the owner's go.

## Done when

- `Tools\build_server.bat` (Win64) passes with the source engine and refuses the launcher engine.
- A packaged Win64 server runs; the local cluster (editor mode) completes the smoke run as far as
  the checklist allows.
- Linux server builds (if the owner did step 8).
- The launcher editor still opens the main folder without a rebuild prompt.
