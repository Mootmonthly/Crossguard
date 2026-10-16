# Main menu, hub and the raid portal

How a player gets from the title screen into a raid and back. Code: `ACrossguardMenuGameMode` /
`ACrossguardMenuHUD` (`Game/CrossguardMenuGameMode.h`), `UCrossguardMainMenuWidget` (UI),
`UCrossguardSettingsSave`, `ACrossguardHubMarker`, `ACrossguardRaidPortal` and
`ACrossguardPartyBoard` (Interaction). Maps are made by `Tools/editor_setup.py` (`-CreateMenu`,
`-CreateHub`; [pc-session.md](pc-session.md)). Tests: `Crossguard.Menu.Urls`,
`Crossguard.Scenario.Hub.MainMenuBinds`, `.HubMarker`, `.RaidPortal`,
`Crossguard.Scenario.HubReturn.*`, `Crossguard.Net.SeamlessTravelHubToRaid` (there and back)
([editor-tests.md](editor-tests.md)).

## Flow

```
L_MainMenu --Play--> L_Hub (listen server) --raid portal / Start raid--> raid map --extract--> spectate
     ^   \--Join       ^                                                   |                      |
     |                 +--------- nobody left in the raid: the whole server travels back ---------+
     +---------------- dead for the raid, no teammate left ----------------+
```

1. **Menu** (`/Game/Maps/L_MainMenu`, the project's Game Default Map once `-CreateMenu` ran):
   `ACrossguardMenuGameMode` spawns no pawn; its HUD shows the main menu, UI-only input, cursor on.
   - **Play**: hosts the hub, `GEngine->SetClientTravel("<HubMapName>?listen?Hub")`
     (`ACrossguardMenuGameMode::BuildHostUrl`). `HubMapName` defaults to `/Game/Maps/L_Hub`
     (game mode property, `-HubMap=<path>` overrides). Refused with a status line if the map is missing.
   - **Join**: the address field (`192.168.1.20`, `host:7777`), trimmed; spaces, `?`, `#` and slashes
     refused (`BuildJoinUrl`). A failed connection drops back to the default map (the menu) by the
     engine's own travel-failure path; the menu does not show why yet.
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
     marker in the world's levels). L_Hub is therefore a hub opened directly, from Play, or by a
     dedicated server. The marker must be in the persistent level (the script makes a
     non-World-Partition map).
   - **Party board** (`ACrossguardPartyBoard`, F): opens the party window (same as P) through
     `ACrossguardPlayerController::ClientOpenPartyPanel`.
   - **Raid portal** (`ACrossguardRaidPortal`, F, 3 m range): calls `UCrossguardPartySubsystem::StartRaid`
     for the user, the same launch as the party window's Start raid: the leader or a solo player,
     hub only, to the game mode's `RaidMapName` / `RaidServerAddress` ([parties.md](parties.md)).
     Anyone else is refused: `OnPartyRefused` (the party window) and a HUD line
     (`ClientShowMessage`: "Only your party's leader can start the raid."). Server-side only.
3. **Raid**: `RaidMapName` defaults to `/Game/Maps/Dev/L_DevSandbox` (**placeholder**: it has monsters,
   chests and gather nodes) until a real raid map exists. The hub's ServerTravel is absolute, so
   `?Hub` does not carry over and the sandbox plays as a raid (`bIsRaid` on).
4. **After the raid**: ruled (owner): a player dead for the raid with no teammate left to watch is
   **not** sent to the menu any more: they keep spectating (the camera goes back to their body) and
   travel back to the hub with their team (6 below, or the whole server: 5). The menu
   (`ACrossguardGameMode::ReturnToMenu` → `ClientTravel("<MenuMapName>?MenuReason=RaidOver")`, "Your
   raid is over.") stays for when there is no hub (`HubMapName` empty), a team's trip back has nowhere
   to go, and later explicit menu requests. `MenuMapName` defaults to `/Game/Maps/L_MainMenu`; empty
   or a map that does not exist falls back to the engine's `ClientReturnToMainMenuWithTextReason`
   (the Game Default Map). The listen-server host is never sent away (it would end the match for
   everyone). Players sent to the menu are gone: they do not come back with the server.
5. **Back to the hub** (ruled, owner: option 1, the whole server goes back; dedicated hub servers
   are option 2, later: [roadmap.md](roadmap.md) Milestone 5). An extracted player spectates a
   teammate still in the raid, or stays where they left. After every extraction, death for the raid,
   release of a held player, logout, reconnect and new raider, `ACrossguardGameMode::UpdateHubReturn`
   counts who is still in the raid:
   - **Someone connected still raids** (alive or downed, or reconnecting): nothing happens; a running
     countdown is called off ("Return to the hub called off: a raider is back.").
   - **Nobody is left**: a countdown of `HubReturnCountdownSeconds` (**10 s, placeholder**); every
     player's HUD message line shows "Returning to the hub in N s" (`ClientShowMessage`, once a
     second).
   - **Only raiders away** (disconnected, held for a reconnect: [persistence.md](persistence.md),
     "Reconnecting"): they still count as in the raid, but only for `HeldPlayerReturnGraceSeconds`
     (**120 s, placeholder**), shown the same way. A reconnect calls it off; a held character dying
     meanwhile brings it down to the short countdown.
   - Nobody connected at all (a solo raider crashed, everyone else went to the menu): no countdown;
     the held character waits for its reconnect as before.
   At the end (`ReturnToHub`): (1) raiders still away are released as if they died while away
   (`ReleaseHeldPlayer`: raid failed, raid XP and carried gear lost, saved, no way back into that
   raid: the existing Assumed rule); their spectators come along instead of going to the menu;
   (2) party members not on the server (gone to the menu, released, logged out) leave their parties,
   as logging out of the hub does ([parties.md](parties.md)); (3) every player's save is written
   (`SaveNow`, then `FlushPendingSaves`): extracted players' record holds the gear they carried out,
   the dead's none; (4) `UWorld::ServerTravel("<HubMapName>?listen?Hub", absolute)` (`?listen` only on
   a listen server; `BuildHubReturnUrl`), seamless like the launch. `HubMapName` defaults to
   `/Game/Maps/L_Hub`; `-HubMap=<path>` overrides it; empty turns the trip off (players stay as
   before). A hub map that does not exist logs an error and nobody travels.
   In the hub: `?Hub` and the hub marker turn `bIsRaid` off; seamless travel brings the player states
   (party key, team, banked XP, gold, stash) and controllers; the new player states start with no
   finished raid (`HasFinishedRaid` is not copied), so `PlayerCanRestart` spawns everyone; the save
   subsystem puts each player's latest record (gear carried out) on the new pawn
   (`PlayerJoined` / `PlayerReady`); parties are untouched (the subsystem lives with the game
   instance), and the leader can launch the next raid.
   The raid launch saves its travellers the same way before its `ServerTravel` / `ClientTravel`.
   With teams still raiding, a finished team does not wait: 6.
6. **A team goes back early** (ruled, owner): a team (one party key = one team; solo = a team of
   one) whose members have all extracted or died for the raid goes back to the hub without waiting
   for the other teams ("end of match" for them). `UpdateHubReturn` counts per team too:
   - **Nobody left in the raid at all** (that team was the last, or the server is that team): the
     whole-server return of 5, unchanged; any team countdown is dropped (they go together).
   - **Other teams still raid**: the finished team gets its own `HubReturnCountdownSeconds` (10 s,
     the held-teammate grace if a teammate is away), shown on **that team's** HUD only
     ("Returning to the hub in N s"). A teammate reconnecting calls it off. The other teams see
     nothing and play on.
   - **The team holds the server's own player** (listen-server host; standalone): it cannot leave
     early (Assumed: that would close the raid for everyone). Its HUD says so once; it goes with
     the whole server when the last team is done.
   At the end (`ReturnTeamToHub`): (1) its members are marked as leaving for the hub
   (`IsLeavingForHub`): they no longer count for the raid, and their connections closing is a
   deliberate travel, never a hold for a reconnect (`HoldForReconnect` skips them); teammates still
   away fail the raid as at the whole-server return; (2) each member is saved (`SaveNow`,
   `FlushPendingSaves`); extracted pawns are gone already, the dead's bodies stay lootable;
   (3) the plan, per member (`GetTeamHubReturnPlan`, `FCrossguardTeamHubTravel`), then (4) each
   client travels (`ACrossguardPlayerController::ClientTravelToHub`); the raid server stays:
   - **`HubServerAddress` set** (`-HubServer=<address>`; a future dedicated hub, option 2): every
     member travels there at once, `<address>?Party=<key>`.
   - **Otherwise (listen-server era)**: the leader (the party's leader if this server knows the
     party, else the longest-standing member here) opens the hub on its own machine as a listen
     server, `<HubMapName>?listen?Hub?Party=<key>` (`BuildTeamHubHostUrl`); the others connect to
     `<leader ip>:<TeamHubListenPort>?Party=<key>` after `TeamHubJoinDelaySeconds` (**5 s,
     placeholder**; port **7777, placeholder**). The leader's IP is the one this server sees on its
     connection (`APlayerController::GetPlayerNetworkAddress`), sent in the client RPC; the member's
     HUD shows "Joining your team's hub in N s". A solo player just opens the hub (no `?Party=`).
     Development ids travel along as `?DevPlayerId=<name>`.
   - No leader address (should not happen) or no hub map to host: that member / the team goes to
     the menu.
   On the leader's hub the party comes back from the key (`UCrossguardPartySubsystem::
   JoinReturningParty`, [parties.md](parties.md) "Back from the raid early"): the leader leads, the
   others join by key, all on one team.
   **Known limitation until dedicated hubs (option 2):** the members must reach the leader's
   machine: the leader's router must forward the port (7777, UDP) to it and its firewall allow the
   game; a leader behind NAT without that, or two players behind the same NAT seen from outside
   (the raid server sees their public IP), cannot be joined: the members' join fails and they land
   on the menu. Saves are per machine with the local file backend ([persistence.md](persistence.md)):
   on the leader's hub the members' characters come from the leader's disk, not the raid server's,
   until a shared save backend exists. A member still on the raid server (its join delay) when the
   last team finishes is taken along by the whole-server travel instead.

## Placeholders and assumptions

- `RaidMapName` = `L_DevSandbox`; `HubMapName` = `L_Hub`; `MenuMapName` = `L_MainMenu`.
- Play always hosts a listen server (solo play is a listen server with one player).
- Settings ranges and steps; settings are per machine, saved only by "Save settings".
- Hub layout (plaza size, positions) and the portal / board looks (engine shapes).
- `-CreateMenu` sets `GameDefaultMap` (packaged game starts on the menu); the editor startup map is
  left alone (the owner playtests in `L_Test` / `test+new`).
- No message on a failed Join yet; no "back to menu" button in the hub (Esc closes windows only).
- Back to the hub: 10 s countdown, 120 s grace for raiders away, both on the game mode. A raider away
  when the server leaves fails their extraction (Assumed rule). Party members who are not on the
  server at the return leave their parties (Assumed, as a hub logout). A dead player spectating a
  held teammate who is released at the return comes along instead of going to the menu (Assumed).
- A separate raid server (`RaidServerAddress`, clients travelled there) would also travel to its own
  `HubMapName` and become a hub; set `HubMapName` empty there until option 2 (dedicated hubs) exists.
- A team going back early: 10 s countdown per team (the same `HubReturnCountdownSeconds`), members
  join the leader's hub after 5 s (`TeamHubJoinDelaySeconds`) on port 7777 (`TeamHubListenPort`),
  all placeholders. Assumed: the team holding the listen-server host (or standalone player) cannot
  leave early; the leader is the party's leader when the raid server knows the party, else the
  longest-standing member; a team countdown running when the last team finishes is dropped for the
  whole-server countdown (restarts at 10 s).
