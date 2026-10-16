# Dedicated servers: plan

How to get from today's listen-server game to dedicated hub and match servers: a source build of
UE 5.8, the `CrossguardServer` target (Windows and Linux), a local cluster on one PC, and the code
that still assumes a listen server. Written for a Claude Code session on the owner's Windows PC
(task file: [tasks/setup-source-engine.md](tasks/setup-source-engine.md)) and for the owner.
Steps marked **OWNER** need a person (accounts, installers, UAC prompts).

Owner's choice ([design-decisions.md](design-decisions.md) "Online architecture"): dedicated match
servers many teams join, shared dedicated hub servers, **Nakama** as the backend, **Amazon GameLift
Servers** hosting (Anywhere for local testing); comparison in [backend-comparison.md](backend-comparison.md).
Code side: the "Dedicated architecture" commits, [hub-and-menu.md](hub-and-menu.md), [parties.md](parties.md).

Checked against the official pages on 2026-10-16; versions move, so re-check the linked page when a
step fails. Numbers marked *estimate* are not measured on the owner's PC.

## Summary

| Part | Who | Time (*estimate*) | Disk (*estimate*) |
| --- | --- | --- | --- |
| 1a. Link GitHub to Epic, accept the invite | **OWNER** | 10 min (+ email wait) | |
| 1b. Clone the 5.8 tag (shallow) | session | 10–40 min | 4–6 GB |
| 1c. `Setup.bat` (engine binaries, prerequisites) | session (**OWNER**: UAC prompt) | 20–60 min | +40–60 GB |
| 1d. Build the editor and tools | session | 1–3 h (8–16 cores) | +60–100 GB |
| 1e. First open of Crossguard with it (shaders, DDC) | session | 30–90 min | +10–30 GB |
| 2a. Win64 server build | session | 20–60 min first, minutes after | +15–30 GB |
| 2b. Linux toolchain + Linux server | **OWNER** installer, then session | 10 min + 30–90 min | +5 GB + 15–30 GB |
| 2c. Packaged (cooked) server | session | 15–60 min first | 1–5 GB per build |
| 3. Local cluster | session or owner | minutes | logs only |

**Free disk to plan for: 250 GB, 300 GB to be comfortable, on an SSD** (NVMe if there is a choice).
32 GB RAM recommended by Epic; with 16 GB lower the build's parallel jobs (see 1d).

## Part 1: UE 5.8 from source

### 1a. Access to the source (**OWNER**)

The engine source is a private repository of the EpicGames organization on GitHub. Access comes from
linking the accounts:

1. Sign in at **epicgames.com** → account settings → **Apps and Accounts** (the docs call it
   *Connections → Accounts*; <https://www.epicgames.com/account/connections>) → **GitHub** →
   **Connect**. Accept the Unreal Engine EULA if asked, then **Link Account** and **Authorize
   EpicGames** on GitHub.
2. GitHub emails an invitation to the **@EpicGames** organization: open it and click **Join
   @EpicGames** within **7 days**. Epic emails a confirmation when it worked.
3. Check: <https://github.com/EpicGames/UnrealEngine> opens while signed in to GitHub (a 404 means the
   invite is not accepted yet).
4. Git on the PC must authenticate as that GitHub account (Git Credential Manager signs in through the
   browser on the first clone; or `gh auth login`).

Source: [Downloading source code](https://dev.epicgames.com/documentation/unreal-engine/downloading-source-code-in-unreal-engine).

### 1b. Clone the exact 5.8 release

Use the tag of the same 5.8 hotfix the launcher engine has, so content and the derived data cache
match. The launcher's version: `C:\Program Files\Epic Games\UE_5.8\Engine\Build\Build.version`
(`MajorVersion.MinorVersion.PatchVersion`, e.g. 5.8.1).

```bat
git config --global core.longpaths true
git ls-remote --tags https://github.com/EpicGames/UnrealEngine.git "refs/tags/5.8*"
rem pick the tag that matches the launcher version, e.g. 5.8.1-release
git clone --depth 1 --branch 5.8.1-release --single-branch https://github.com/EpicGames/UnrealEngine.git D:\UE\UE_5.8_Source
```

- **Assumed**: the tags are named `<version>-release` (as for 5.0–5.7); take the name from the
  `ls-remote` output.
- Short path at a drive root (`D:\UE\...`): engine paths are long and Windows' 260-character limit
  still bites some tools.
- Shallow (`--depth 1`) is enough to build; *estimate* 2–4 GB download, 4–6 GB on disk. A later
  hotfix: clone the new tag beside it (or `git fetch --depth 1 origin tag 5.8.2-release` and check it
  out), then rerun 1c and 1d.
- Not in OneDrive or another synced folder. A Windows Defender exclusion for the engine and project
  folders speeds builds a lot (**OWNER** decision; security trade-off).

### 1c. Setup.bat and project files

```bat
cd /d D:\UE\UE_5.8_Source
Setup.bat
GenerateProjectFiles.bat
```

- `Setup.bat` downloads the engine's binary dependencies (*estimate* 15–25 GB download, 40–60 GB
  unpacked), installs the prerequisites (**OWNER**: accept the UAC prompt and the redistributables
  installer) and registers the engine with UnrealVersionSelector. Rerun after every pull; later runs
  are incremental.
- `GenerateProjectFiles.bat` writes `UE5.sln` (under a minute). Needed for the Visual Studio
  workflow and after installing the Linux toolchain; the Build.bat commands below do not need the
  `.sln`.

**Visual Studio.** Keep the Visual Studio that builds Crossguard today. Epic's 5.8 page lists **VS
2026 (18.0+)** as the recommended IDE and VS 2022 17.14+ as supported; the 5.8 release notes reportedly name VS 2022
17.14 / MSVC 14.44.35207 / Windows SDK 10.0.22621.0 as the build-farm toolchain (forum quote, not
verified on the release notes page). Required in the installer
([Setting up Visual Studio](https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine)):

| Workloads | Individual components |
| --- | --- |
| Desktop development with C++; Game development with C++; .NET desktop development | MSVC v14.38 or newer (14.44 / 14.50 recommended); Windows 10/11 SDK 10.0.22621.0 or newer (10.0.26100 recommended); C++ profiling tools; C++ AddressSanitizer; Unreal Engine installer; .NET 8 SDK if Setup reports it missing (UBT ships its own .NET) |

If UBT says "Visual Studio 2022 x64 must be installed" with only VS 2026 present, add VS 2022 17.14
(Build Tools are enough) or set the compiler explicitly (`-Compiler=VisualStudio2026` on Build.bat,
or `CompilerVersion` in `BuildConfiguration.xml`); a forum report for 5.8.1 had this.

### 1d. Build the editor and the tools

From the engine root (`D:\UE\UE_5.8_Source`):

```bat
Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -WaitMutex
Engine\Build\BatchFiles\Build.bat ShaderCompileWorker Win64 Development -WaitMutex
Engine\Build\BatchFiles\Build.bat UnrealPak Win64 Development -WaitMutex
Engine\Build\BatchFiles\Build.bat CrashReportClient Win64 Shipping -WaitMutex
Engine\Build\BatchFiles\Build.bat UnrealVersionSelector Win64 Shipping -WaitMutex
```

(Or in Visual Studio: `UE5.sln`, *Development Editor* / *Win64*, build the **UE5** target.) Epic
says 10–40 minutes on fast hardware; *estimate* 1–3 hours on an 8–16 core desktop. With 16 GB RAM
add `-MaxParallelActions=6` (each compile job needs 1.5–2 GB).

The first time Crossguard opens with this engine, it compiles shaders and fills the derived data
cache (*estimate* 30–90 minutes, 10–30 GB).

### 1e. Register the engine and keep the launcher engine

`Setup.bat` registers the source engine under `HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds` as a
GUID (`{1A2B...}` = `D:\UE\UE_5.8_Source`). If it is missing there, run
`Engine\Binaries\Win64\UnrealVersionSelector-Win64-Shipping.exe /register` from the engine root.
Both engines then exist side by side; the launcher engine is untouched.

**Two engines side by side (recommended).** The committed `Crossguard.uproject` keeps
`"EngineAssociation": "5.8"` (the launcher engine): the owner's daily editor and the Tools scripts
keep working exactly as now. Server builds use a second working copy of the repository with the
source engine, because a project folder holds one set of editor binaries: building `CrossguardEditor`
with the source engine replaces the launcher-built DLLs (the launcher editor then asks to rebuild).

```bat
cd /d C:\path\to\Crossguard
git fetch origin
git worktree add --detach D:\Crossguard-Server origin/foundation/core-rules
cd /d D:\Crossguard-Server
git lfs pull
setx UE_SOURCE_ENGINE_DIR D:\UE\UE_5.8_Source
```

- Update it: `git -C D:\Crossguard-Server fetch origin`, then
  `git -C D:\Crossguard-Server checkout --detach origin/foundation/core-rules` and `git lfs pull`.
  Nothing is ever committed from it (detached; commits come from the main folder).
- The scripts take the engine from `-EngineDir`, then `UE_SOURCE_ENGINE_DIR` (build_server only),
  then `UE_ENGINE_DIR`, then the `.uproject` association; the association does not need to change.
- Open its editor directly: `D:\UE\UE_5.8_Source\Engine\Binaries\Win64\UnrealEditor.exe
  D:\Crossguard-Server\Crossguard.uproject`.

**Switching the project in place (alternative).** Right-click `Crossguard.uproject` → *Switch
Unreal Engine version…* → pick the source build. This writes the GUID into `EngineAssociation`;
rebuild (`Tools\build_editor.bat`, the scripts follow the association). **Do not commit the GUID**:
it exists only on this PC. To switch back: *Switch Unreal Engine version…* → **5.8** (or set
`"EngineAssociation": "5.8"`), then `Tools\build_editor.bat` again (the editor DLLs must be rebuilt
for the launcher engine; the first open recompiles some shaders).

## Part 2: the server target

### 2a. Win64 server

`Source/CrossguardServer.Target.cs` exists (`Type = TargetType.Server`, Crossguard module). The
`.uproject` limits `PythonScriptPlugin`, `EditorScriptingUtilities` and `ModelingToolsEditorMode` to
Editor targets, and `Crossguard.Build.cs` adds `UnrealEd` only when `Target.bBuildEditor`, so the
server should not pull in editor modules.

```bat
Tools\build_server.bat                       rem Build.bat CrossguardServer Win64 Development
Tools\build_server.bat -Configuration Shipping
```

Output `Binaries\Win64\CrossguardServer.exe` (uncooked: runs only against a cooked build, see 2c).
The script stops with exit 4 when the engine is a launcher (installed) one
(`Engine\Build\InstalledBuild.txt` exists): installed engines cannot build Server targets. Logs in
`Saved\ServerBuild\` (`build.log`, `errors.txt`, compact like build_editor).

### 2b. Linux server (cloud hosting)

Most hosts run Linux containers. UE cross-compiles Linux from Windows:

1. **OWNER**: download and run the **v26 clang-20.1.8** cross-compile toolchain for UE 5.7–5.8:
   <https://cdn.unrealengine.com/CrossToolchain_Linux/v26_clang-20.1.8-rockylinux8.exe>
   (from [Linux development requirements](https://dev.epicgames.com/documentation/unreal-engine/linux-development-requirements-for-unreal-engine);
   ~1 GB download, *estimate* 3–5 GB installed). It sets `LINUX_MULTIARCH_ROOT`.
2. Open a **new** terminal (`echo %LINUX_MULTIARCH_ROOT%` shows the toolchain folder), then in the
   engine root `GenerateProjectFiles.bat` again so UBT sees the Linux platform.
3. `Tools\build_server.bat -Platform Linux` (exit 4 with a clear message if the toolchain is missing).
   *Estimate* 30–90 minutes the first time.

Recommended Linux for running it: Ubuntu 22.04 or Rocky Linux 8.

### 2c. Cooked, packaged server

```bat
Tools\build_server.bat -Package                       rem Win64 server  -> Saved\Packages\Server\WindowsServer\
Tools\build_server.bat -Package -Platform Linux       rem Linux server  -> Saved\Packages\Server\LinuxServer\
Tools\build_server.bat -Package -WithClient           rem + a Win64 client from the same engine -> ...\Windows\
```

What it runs: `CrossguardEditor` with the source engine (the cook loads the project's modules), then

```bat
RunUAT.bat BuildCookRun -project=<...>\Crossguard.uproject -noP4 -unattended -utf8output -nocompileeditor ^
  -server -serverplatform=Win64 -serverconfig=Development -noclient ^
  -build -cook -stage -pak -archive -archivedirectory=<...>\Saved\Packages\Server ^
  -map=/Game/Maps/L_Hub+/Game/Maps/Dev/L_DevSandbox+/Game/Maps/L_MainMenu
```

Exit 3 if the cook or packaging fails (log `Saved\ServerBuild\package.log`). A packaged server only
accepts clients from the same engine build and project version: for a packaged cluster use
`-WithClient` (or the source engine's editor), not the launcher editor.

Packaging settings to add when packaging for real (checklist item C11): the maps to cook in
`DefaultGame.ini` (`[/Script/UnrealEd.ProjectPackagingSettings] +MapsToCook=...`), and any asset loaded
only by path in `DirectoriesToAlwaysCook`.

### 2d. Docker image basics (Linux server)

On the PC: Docker Desktop with the WSL 2 backend (**OWNER** installs). Next to the packaged
`LinuxServer\` folder, a `Dockerfile`:

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
# Unreal refuses to run as root.
RUN useradd --create-home --shell /bin/bash ue
COPY --chown=ue:ue LinuxServer/ /home/ue/server/
USER ue
WORKDIR /home/ue/server
EXPOSE 7777/udp
ENTRYPOINT ["./CrossguardServer.sh"]
CMD ["/Game/Maps/Dev/L_DevSandbox", "-log", "-port=7777"]
```

```bat
docker build -t crossguard-server:dev Saved\Packages\Server
docker run --rm -p 7779:7777/udp crossguard-server:dev /Game/Maps/Dev/L_DevSandbox -log -port=7777 -AllowDevPlayerIds
```

A client from the same build connects with `open 127.0.0.1:7779`. Game traffic is **UDP**. GameLift
(the chosen host) runs either such a container (container fleets, image pushed to Amazon ECR) or the
plain `LinuxServer` folder on managed EC2 fleets; either way the server must talk to GameLift through
the GameLift Server SDK (checklist C15), and GameLift assigns the port per process. Not in the
repository yet: add the `Dockerfile` with the GameLift work.

## Part 3: running locally

### 3a. Local cluster script

`Tools\run_local_cluster.bat` starts a dedicated hub server, a dedicated match server and N clients
on this PC, waits for each server to listen, and tiles the client windows. `-Stop` closes everything
it started (`Saved\LocalCluster\pids.json`); logs per process in `Saved\LocalCluster\`.

| Mode | What runs | Needs |
| --- | --- | --- |
| `-Mode Editor` (default) | the editor binary, uncooked: `UnrealEditor.exe Crossguard.uproject <map> -server` / `<address> -game` | `CrossguardEditor` built; **works with the launcher engine** (a dedicated server from the editor binary needs no Server target) |
| `-Mode Packaged` | `Saved\Packages\Server\WindowsServer\CrossguardServer.exe`; clients from `-ClientBuildDir` (`build_server -Package -WithClient`) or the source engine's editor | Part 2c |

The command lines it uses (Editor mode; the packaged server takes the same arguments without the
project path):

```bat
rem hub server (port 7777); its raid launch goes to the local match server (dev matchmaking fake)
UnrealEditor.exe Crossguard.uproject /Game/Maps/L_Hub?Hub -server -log -port=7777 -AllowDevPlayerIds -RaidServer=127.0.0.1:7778
rem match server (port 7778); teams go back to the hub server
UnrealEditor.exe Crossguard.uproject /Game/Maps/Dev/L_DevSandbox -server -log -port=7778 -AllowDevPlayerIds -HubServer=127.0.0.1:7777
rem client i
UnrealEditor.exe Crossguard.uproject 127.0.0.1:7777?DevPlayerId=alice -game -log -windowed -ResX=960 -ResY=540 -WinX=0 -WinY=0 -CrossguardPlayerId=alice
```

Examples: `Tools\run_local_cluster.bat` (hub + match + 2 clients to the hub);
`-Clients 3 -ClientsTo Raid -NoHub` (one match server, three raiders); `-Clients 0` (servers only).
From a running client the console `open 127.0.0.1:7778?DevPlayerId=alice` joins another server.

- No `-DevListenHub` on either server: they behave as hosted ones (a match server never travels as
  a whole; each team goes back to `-HubServer=`). Until C6 is done a match server serves **one**
  match: restart the cluster for the next.
- **Assumed**: ports 7777 (hub) and 7778 (match), names alice, bob, … (placeholders, script options).
- Saves: both servers use the project's `Saved\Crossguard\Characters` (same folder), so a character
  carries across hub and match locally; in the cloud this needs the backend.
- Windows firewall asks the first time a server listens (**OWNER**: allow on private networks).

### 3b. Automation tests stay on PIE

Nothing changes for `Tools\run_editor_tests.bat`: the scenarios run in PIE, including dedicated PIE
servers (`Scenario.Clients(N, true)`, [editor-tests.md](editor-tests.md)), with the launcher engine
(or whichever engine the main folder uses). The Server target and the cluster are a separate check:
after code changes for dedicated servers, run the tests as usual, then `Tools\build_server.bat` (does
the server compile) and a cluster smoke run (3c).

### 3c. Smoke run (manual until scripted)

1. `Tools\run_local_cluster.bat -Clients 2`.
2. Both clients land in the hub; party up at the board (alice invites bob), launch at the portal:
   both clients travel to 127.0.0.1:7778.
3. Raid, extract both. Team return: both clients travel back to the hub server with their gear.
4. Check the server logs for `Error` / `Warning` lines (`Saved\LocalCluster\hub.log`, `match.log`).
5. `Tools\run_local_cluster.bat -Stop`.

## Part 4: code that assumes a listen server (checklist)

Scanned on 2026-10-16 after "Dedicated architecture 3/5" (`9d1b3d4`). That series is still
changing this code: **re-run the scan before acting** and drop the items it already fixed:

```bat
git grep -n -E "GetFirstPlayerController|IsLocalController|IsLocallyControlled|NM_ListenServer|IsServerOwnPlayer|\?listen|BuildHostUrl|pie-|ServerTravel|GetHubMapName|DevPlayerId" -- Source/Crossguard ":!Source/Crossguard/Private/Tests"
```

No code was changed for this plan. Priority: **H** blocks a working local cluster, **M** needed
before hosting, **L** cleanup.

| # | Pri | Where | Today | Change |
| --- | --- | --- | --- | --- |
| C1 | – | `ACrossguardCharacter::UpdateStealthVisibility` (`GetWorld()->GetFirstPlayerController()`) | **Done (series 5/5)**: no local viewer on a dedicated server (`NM_DedicatedServer`), and only a local first controller counts as the viewer; the server never hides capsules for a remote client. | Verify stealth on a listen host and a client. |
| C2 | – | `UCrossguardPartySubsystem::StartRaid` ClientTravel URL (`BuildMatchTravelUrl` / `RaidTravelUrl`) | **Done (series 5/5)**: `StartRaid` adds each traveller's `?DevPlayerId=<name>` in development builds (`ACrossguardGameMode::WithDevPlayerId`, also used by the team return); `PlanRaidTravel`'s URL stays the shared one. | Verify in the local cluster: the save follows to the match server. |
| C3 | – | Main menu Join (`ACrossguardMenuGameMode::BuildJoinUrl`, `UCrossguardMainMenuWidget`) and the cluster clients | **Done (series 5/5)**: the menu's Join adds `?DevPlayerId=` from `-CrossguardPlayerId=` in development builds (`ACrossguardMenuGameMode::WithLocalDevPlayerId`). The cluster's client URLs are the script's. | Cluster clients: pass `-CrossguardPlayerId=` and use Join, or add `?DevPlayerId=` to their URL. |
| C4 | – | `ACrossguardGameMode::ReturnToHub` (whole-server return) | **Done (series 3/5)**: refused outside dev listen mode; on a dedicated match server every team, the last one too, leaves on its own to `GetHubDestination` (backend stub, then `-HubServer=`, else the menu). | Verify in the smoke run. |
| C5 | – | `UpdateSpectators` (dead with nobody to watch) | **Done (series 3/5)**: `HasTripBack()` counts the hub server. | Verify. |
| C6 | – | Match server lifecycle (`ACrossguardGameMode`: `FinishedPlayerIds`, `HeldPlayers`, `SentToMenuIds`, `LeavingForHubIds`, `bHubReturnStarted`, looted containers, killed mobs) | **Done (series 5/5), Assumed (coordinator)**: one match per process. `ACrossguardGameMode::CheckMatchOver` marks the match over once every player has left (teams gone to the hub / menu) and fires `OnMatchOver` (the GameLift `ProcessEnding` hook); a dedicated server then exits cleanly after `MatchOverExitDelaySeconds` (**5 s, placeholder**). `MaxRaidDurationSeconds` (**3600 s, placeholder**) closes the raid: raiders still in it fail it (Assumed: as a death for the raid), held raiders are released, teams go back. Never in development listen mode, the editor, tests, or with `-KeepMatchServer`. | The local cluster: pass `-KeepMatchServer` or restart the match server after each match. Owner: confirm the time limit and its rule. |
| C7 | – | Team early return without a hub server | **Done (series 3/5)**: the leader-hosts-hub path is gone; no hub server → the menu with a message. | – |
| C8 | – | Menu Play (`BuildHostUrl`, `<HubMap>?listen?Hub`) | **Done (series 3/5)**: Play is refused outside dev listen mode. Left: Play asks the backend for a hub server. | With the backend. |
| C9 | L | Listen-host special cases that are simply false on a dedicated server: `IsLocalController() && NM_ListenServer` in `UpdateSpectators`, `bListenHostInParty`, `BuildHubReturnUrl(.., bListen)`, `IsServerOwnPlayer`, `Login`'s `bLoggingInLocalPlayer`, `pie-<n>` in `DevPlayerIdFor` | Correct on dedicated (they never fire). | Tag `// DEV-ONLY (listen server)` as the series does; no behaviour change. |
| C10 | M | `?Party=` on the match server (`InitNewPlayer` → `JoinTeam`) | The client's word decides the team. | Accept only parties / tickets the matchmaker announced (`?MatchTicket=`, series 2/5 started this). |
| C11 | M | Config | No `ServerDefaultMap` (a server started without a map opens `/Engine/Maps/Entry`); no `MapsToCook` / `DirectoriesToAlwaysCook`. | Set `[/Script/EngineSettings.GameMapsSettings] ServerDefaultMap=/Game/Maps/L_Hub` (Assumed) and the packaging lists; keep passing the map on the command line. |
| C12 | M | Saves (`UCrossguardLocalSaveBackend`, `Saved/Crossguard/Characters`) | Per machine. Fine for one PC (hub and match share the folder), not for hosted servers. | The backend ([backend-comparison.md](backend-comparison.md), [persistence.md](persistence.md)). |
| C13 | L | Net settings for big multi-team raids | Engine defaults (server tick 30, default client rates). | Tune `NetServerMaxTickRate`, `MaxClientRate`, relevancy / replication graph after a load test. |
| C14 | L | Server-only code paths (UI, cosmetics) | Checked: HUD and widgets only for local controllers (`ACrossguardHUD`, `CrossguardWidgetBase`), settings only for the local controller, cosmetics skip `NM_DedicatedServer`, debug HUD needs `IsLocallyControlled`, editor modules only with `bBuildEditor`. | Nothing; confirm with `build_server` and a clean server log in the smoke run. |
| C15 | M | GameLift Server SDK (not in the project) | Nothing reports to a host. | Add the Amazon GameLift Server SDK plugin for Unreal (built with the source engine; it needs OpenSSL from the engine): `InitSDK`, `ProcessReady` with the port, `OnStartGameSession` (players / match tickets from Nakama), `ActivateGameSession`, `AcceptPlayerSession` at login (with C10), `ProcessEnding`. Hub servers the same, long-lived. Test with GameLift Anywhere on the PC. |

Already fine for dedicated servers: everything gameplay is server-authoritative
([anti-cheat.md](anti-cheat.md)); `bUseSeamlessTravel`; raid launch `ServerTravel` and the whole-server return only in dev listen
mode (`IsDevListenMode`: PIE or `-DevListenHub`, never Shipping; series 1/5–3/5); the team return to a hub server (`-HubServer=`).

## Part 5: scripts

| Script | What | Exit codes |
| --- | --- | --- |
| `Tools\build_server.bat` (`.ps1`) | Builds `CrossguardServer` (`-Platform Win64\|Linux`, `-Configuration`), or packages it (`-Package`, `-WithClient`, `-ArchiveDir`, `-Maps`). Engine: `-EngineDir`, `UE_SOURCE_ENGINE_DIR`, `UE_ENGINE_DIR`, association. Refuses launcher engines and a missing Linux toolchain. | 0 ok, 2 build failed, 3 package failed, 4 set-up |
| `Tools\run_local_cluster.bat` (`.ps1`) | Hub + match server + N clients (part 3a); `-Stop`. | 0 started / stopped, 1 a server did not listen, 4 set-up |

Both dot-source `Tools\ue_common.ps1` (engine lookup, compact errors; it gained `Invoke-CgTargetBuild`,
`Test-CgInstalledEngine`, `Get-CgEngineVersion`). Written without a Windows test run: the first PC
session runs them and fixes what breaks.
