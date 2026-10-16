# Main menu, hub and the raid portal

How a player gets from the title screen into a raid and back. Code: `ACrossguardMenuGameMode` /
`ACrossguardMenuHUD` (`Game/CrossguardMenuGameMode.h`), `UCrossguardMainMenuWidget` (UI),
`UCrossguardSettingsSave`, `ACrossguardHubMarker`, `ACrossguardRaidPortal` and
`ACrossguardPartyBoard` (Interaction). Maps are made by `Tools/editor_setup.py` (`-CreateMenu`,
`-CreateHub`; [pc-session.md](pc-session.md)). Tests: `Crossguard.Menu.Urls`,
`Crossguard.Scenario.Hub.MainMenuBinds`, `.HubMarker`, `.RaidPortal`,
`Crossguard.Scenario.HubReturn.*`, `Crossguard.Net.SeamlessTravelHubToRaid` (there and back)
([editor-tests.md](editor-tests.md)).

## Architecture (owner decision)

Crossguard is a multi-team PvP extraction game. Raids run on central **dedicated match servers** that
many teams join; the shared social hub runs on **dedicated hub servers**; a backend service (Nakama:
[design-decisions.md](design-decisions.md) "Online architecture") handles accounts, saves, parties and
matchmaking. A player's PC never hosts a raid or a hub in the real game. The listen-server flow below
("Development mode") exists only to develop on one PC.

```
L_MainMenu --(backend: not built)--> hub server --raid portal / Start raid: matchmaking--> match server
                                         ^                                                     |
                                         +---- a finished team's clients travel back ----------+
                                               (other teams keep raiding; no hub server: menu)
```

## Flow

1. **Menu** (`/Game/Maps/L_MainMenu`, the project's Game Default Map once `-CreateMenu` ran):
   `ACrossguardMenuGameMode` spawns no pawn; its HUD shows the main menu, UI-only input, cursor on.
   - **Play**: DEV-ONLY (listen server): hosts the hub, `GEngine->SetClientTravel("<HubMapName>?listen?Hub")`
     (`ACrossguardMenuGameMode::BuildHostUrl`; `HubMapName` defaults to `/Game/Maps/L_Hub`,
     `-HubMap=<path>` overrides). Only in development mode (`Crossguard::IsDevListenMode`); elsewhere
     refused with "Online play is not available yet (no hub server)." until the backend sends players
     to a hub server. Refused with a status line if the map is missing.
   - **Join**: the address field (`192.168.1.20`, `host:7777`), trimmed; spaces, `?`, `#` and slashes
     refused (`BuildJoinUrl`). A development tool (a listen hub or a dedicated server on the LAN). A
     failed connection drops back to the menu by the engine's travel-failure path, without a reason yet.
   - **Settings** (placeholder): mouse sensitivity (0.1–5, step 0.1), field of view (60–120°, step 5),
     master volume (0–100 %, step 10 %). Saved in a local `USaveGame` slot `CrossguardSettings`
     (this machine, not the character save). `ACrossguardPlayerController` applies them to its local
     player: look input × sensitivity (`AddYawInput` / `AddPitchInput`), the pawn's first camera
     component's FOV (on possess), the main audio device's primary volume.
   - **Quit**.
   - **Character select: not built.** Saves are per player id, one character each
     ([persistence.md](persistence.md)); it comes when saves are per account with several characters.
2. **Hub** (`/Game/Maps/L_Hub`, `-CreateHub`): a 60 m plaza with the sandbox's lighting, four player
   starts, nav bounds, a stash chest and vendor NPC (the dev sandbox's `place_hub_actors`), a party
   board, a raid portal and an `ACrossguardHubMarker`. Game mode override `BP_CrossguardGameMode`.
   - **Hub marker**: a map cannot carry URL options, so a placed `ACrossguardHubMarker` makes it a hub:
     `ACrossguardGameMode::InitGame` turns `bIsRaid` off when `IsHubMap` (the `?Hub` option **or** a
     marker in the world's levels). L_Hub is therefore a hub opened directly, from Play (development), or by
     a dedicated hub server. The marker must be in the persistent level (the script makes a
     non-World-Partition map).
   - **Party board** (`ACrossguardPartyBoard`, F): opens the party window (same as P) through
     `ACrossguardPlayerController::ClientOpenPartyPanel`.
   - **Raid portal** (`ACrossguardRaidPortal`, F, 3 m range): calls `UCrossguardPartySubsystem::StartRaid`
     for the user, the same launch as the party window's Start raid: the leader or a solo player,
     hub only, to the match server matchmaking assigns (development: `-RaidServer=`, or in dev listen
     mode the whole server to `RaidMapName`; [parties.md](parties.md) "Raid launch").
     Anyone else is refused: `OnPartyRefused` (the party window) and a HUD line
     (`ClientShowMessage`: "Only your party's leader can start the raid."). Server-side only.
3. **Raid**: the launch asks matchmaking (`ICrossguardMatchmaking`) for a match server and each
   member's client travels there with the backend's ticket (`?MatchTicket=`); a stub ("not
   available") until the backend exists; development builds fake it with `RaidServerAddress` /
   `-RaidServer=` and `?Party=` ([parties.md](parties.md) "Raid launch"). `RaidMapName` (dev listen
   mode's whole-server launch) defaults to `/Game/Maps/Dev/L_DevSandbox` (**placeholder**) until a
   real raid map exists. The launch is absolute, so `?Hub` does not carry over and the map plays as a
   raid (`bIsRaid` on).
4. **After the raid**: ruled (owner): a player dead for the raid with no teammate left to watch is
   **not** sent to the menu: they keep spectating (the camera goes back to their body) and leave
   with their team (5). The menu (`ACrossguardGameMode::ReturnToMenu` →
   `ClientTravel("<MenuMapName>?MenuReason=<key>")`) is for when there is nowhere to go: "Your raid is
   over. No hub server is available right now." (`NoHubReason`, a dedicated or Shipping server that
   knows no hub server) or "Your raid is over." (`RaidOverReason`, development mode without a hub map),
   and later explicit menu requests. `MenuMapName` defaults to `/Game/Maps/L_MainMenu`; empty or a
   map that does not exist falls back to the engine's `ClientReturnToMainMenuWithTextReason` (the Game
   Default Map). The listen-server host is never sent away. Players sent to the menu are gone.
5. **A team goes back** (ruled, owner): a team (one party key = one team; solo = a team of one) whose
   members have all extracted or died for the raid leaves the match server without waiting for the
   other teams ("end of match" for them); the others play on unaffected. After every extraction,
   death for the raid, release of a held player, logout, reconnect and new raider,
   `ACrossguardGameMode::UpdateHubReturn` counts per team:
   - **A teammate still raids** (alive or downed, or reconnecting): nothing; a running countdown is
     called off ("Return to the hub called off: a teammate is back.").
   - **The team is done**: its countdown, `HubReturnCountdownSeconds` (**10 s, placeholder**), shown on
     **that team's** HUD only ("Returning to the hub in N s", `ClientShowMessage` once a second).
   - **Only teammates away** (disconnected, held for a reconnect: [persistence.md](persistence.md),
     "Reconnecting"): `HeldPlayerReturnGraceSeconds` (**120 s, placeholder**) instead; a reconnect
     calls it off.
   - Nobody of the team connected (a solo raider crashed): no countdown; the held character waits.
   At the end (`ReturnTeamToHub`): (1) its members are marked as leaving (`IsLeavingForHub`): they no
   longer count for the raid, and their connections closing is a deliberate travel, never a hold for
   a reconnect (`HoldForReconnect` skips them); teammates still away fail the raid (released as a
   death while away: raid XP and carried gear lost, saved); (2) each member is saved (`SaveNow`,
   `FlushPendingSaves`): extracted players' record holds the gear they carried out, the dead's none;
   the dead's bodies stay lootable; (3) the plan per member (`GetTeamHubReturnPlan`,
   `FCrossguardTeamHubTravel`) from **`GetHubDestination(Player)`**, in order:
   1. the hub server the backend picks for the player (`ICrossguardMatchmaking::FindHubServer`; a
      stub, none, until the backend exists);
   2. the match server's `HubServerAddress` (`-HubServer=<address>`);
   3. DEV-ONLY (listen server): in development mode with a hub map, the whole server's return (6);
      the team does not leave on its own then;
   4. otherwise the main menu with the `NoHub` message;
   then (4) each client travels: `ClientTravel("<hub server>?Party=<key>", absolute)` (development ids
   travel along as `?DevPlayerId=<name>`), or `ReturnToMenu`. The match server stays for the teams
   still raiding. On the hub server the key gives the team (`InitNewPlayer`); the party itself comes
   back from the backend later (not built: [parties.md](parties.md) "Back from the raid").
   Once every team has left, the match is over (`IsMatchOver`, `OnMatchOver`) and a dedicated match
   server exits so a fresh process takes the next match (Assumed, one match per process; not in
   development mode, tests, or with `-KeepMatchServer`). A raid closes after `MaxRaidDurationSeconds`
   (**3600 s, placeholder**): raiders still in it fail it, and their teams go back
   ([dedicated-server-plan.md](dedicated-server-plan.md) C6).

## Development mode (single PC, listen server)

DEV-ONLY: `Crossguard::IsDevListenMode(World)` (`Game/CrossguardDevMode.h`) is true only in non-Shipping
builds with `-DevListenHub` on the command line or in Play-In-Editor (the automation tests too; tests
can force it per world with `SetDevListenModeOverrideForTests`). Every such path is tagged
`// DEV-ONLY (listen server)` in the code: the menu's Play hosting the hub, the whole-server raid
launch (`ServerTravel` to `RaidMapName`, `HostInParty`), the whole-server return below, and the host
staying put.

```
L_MainMenu --Play--> L_Hub (listen server) --raid portal / Start raid--> raid map --extract--> spectate
                       ^                                                                   |
                       +------- nobody left in the raid: the whole server travels back ----+
```

6. **The whole server goes back** (development only; was the owner's option 1 before the dedicated
   architecture): with no hub server configured, finished teams wait (their HUD says so once) and,
   once nobody is left in the raid, `UpdateHubReturn` runs one countdown for everyone (10 s; 120 s
   grace when only raiders away are left; a reconnect calls it off: "Return to the hub called off: a
   raider is back."). At the end (`ReturnToHub`, refused outside development mode): (1) raiders still
   away are released as if they died while away; their spectators come along; (2) party members not
   on the server (gone to the menu, released, logged out, left with their team) leave their parties
   ([parties.md](parties.md)); (3) every player's save is written; (4)
   `UWorld::ServerTravel("<HubMapName>?listen?Hub", absolute)` (`?listen` only on a listen server;
   `BuildHubReturnUrl`), seamless like the launch. Empty `HubMapName` turns the trip off (players stay;
   the dead with nobody to watch go to the menu). A hub map that does not exist logs an error.
   In the hub: `?Hub` and the hub marker turn `bIsRaid` off; seamless travel brings the player states
   (party key, team, banked XP, gold, stash) and controllers; the new player states start with no
   finished raid, so `PlayerCanRestart` spawns everyone; the save subsystem puts each player's latest
   record on the new pawn; parties are untouched (the subsystem lives with the game instance).
   A team holding the server's own player (listen host, standalone) never leaves on its own while
   others raid (Assumed: that would close the raid for everyone).

Dedicated servers on one PC (development builds): `Tools\run_local_cluster.bat` starts a dedicated hub
(`-RaidServer=`, development matchmaking) and a dedicated match server (`-HubServer=`, `-HubMap=`):
the real team-return path, no listen server ([dedicated-server-plan.md](dedicated-server-plan.md)).

## Placeholders and assumptions

- `RaidMapName` = `L_DevSandbox`; `HubMapName` = `L_Hub`; `MenuMapName` = `L_MainMenu`.
- Play hosts a listen server in development mode only; outside it Play is refused until the backend
  exists (Assumed: no offline / solo hosting in the real game).
- Settings ranges and steps; settings are per machine, saved only by "Save settings".
- Hub layout (plaza size, positions) and the portal / board looks (engine shapes).
- `-CreateMenu` sets `GameDefaultMap` (packaged game starts on the menu); the editor startup map is
  left alone (the owner playtests in `L_Test` / `test+new`).
- No message on a failed Join yet; no "back to menu" button in the hub (Esc closes windows only).
- Team return: 10 s countdown per team, 120 s grace for teammates away, both on the game mode. A
  teammate away when the team leaves fails their extraction (Assumed). A team arriving on a hub server
  gets its team but not its party (Assumed, until the backend keeps parties). The development
  matchmaking (`-RaidServer=`) is non-Shipping only (Assumed). Matchmaking is synchronous for now
  (Assumed; the backend's will be asynchronous).
- Development mode: party members not on the server at the whole-server return leave their parties;
  a dead player spectating a held teammate who is released at the return comes along (Assumed).
