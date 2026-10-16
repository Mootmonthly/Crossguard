# Main menu, hub and the raid portal

How a player gets from the title screen into a raid and back. Code: `ACrossguardMenuGameMode` /
`ACrossguardMenuHUD` (`Game/CrossguardMenuGameMode.h`), `UCrossguardMainMenuWidget` (UI),
`UCrossguardSettingsSave`, `ACrossguardHubMarker`, `ACrossguardRaidPortal` and
`ACrossguardPartyBoard` (Interaction). Maps are made by `Tools/editor_setup.py` (`-CreateMenu`,
`-CreateHub`; [pc-session.md](pc-session.md)). Tests: `Crossguard.Menu.Urls`,
`Crossguard.Scenario.Hub.MainMenuBinds`, `.HubMarker`, `.RaidPortal`
([editor-tests.md](editor-tests.md)).

## Flow

```
L_MainMenu --Play--> L_Hub (listen server) --raid portal / Start raid--> raid map --extract--> (spectate; hub trip not built)
     ^   \--Join <address>--> someone's hub                                  |
     +-------------------- dead for the raid, no teammate left --------------+
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
4. **After the raid**: a player dead for the raid with no teammate left goes to the menu:
   `ACrossguardGameMode::ReturnToMenu` → `ClientTravel("<MenuMapName>?MenuReason=RaidOver")`; the
   menu shows "Your raid is over.". `MenuMapName` defaults to `/Game/Maps/L_MainMenu`; empty or a map
   that does not exist falls back to the engine's `ClientReturnToMainMenuWithTextReason` (the Game
   Default Map). The listen-server host is never sent away (it would end the match for everyone).
   **Extract → hub is not built**: an extracted player spectates / stays (ruled placeholder in
   [design-decisions.md](design-decisions.md), Extraction). With a listen-server hub the hub no
   longer exists during the raid (the host travelled), so the trip back needs either the whole server
   travelling back to L_Hub when the raid ends, or dedicated hub servers; owner decision.

## Placeholders and assumptions

- `RaidMapName` = `L_DevSandbox`; `HubMapName` = `L_Hub`; `MenuMapName` = `L_MainMenu`.
- Play always hosts a listen server (solo play is a listen server with one player).
- Settings ranges and steps; settings are per machine, saved only by "Save settings".
- Hub layout (plaza size, positions) and the portal / board looks (engine shapes).
- `-CreateMenu` sets `GameDefaultMap` (packaged game starts on the menu); the editor startup map is
  left alone (the owner playtests in `L_Test` / `test+new`).
- No message on a failed Join yet; no "back to menu" button in the hub (Esc closes windows only).
