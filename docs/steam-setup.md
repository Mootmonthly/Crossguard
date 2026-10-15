# Steam setup (accounts)

Guilds, saved progression and anything that must remember a player across sessions need a lasting
player id. With Steam, that id is the player's Steam id: `APlayerState::GetUniqueId()` returns it,
and `UCrossguardGuildSubsystem::PlayerIdOf` already uses it. Nothing in the C++ needs to change.
Trading does not need any of this.

These steps are **not applied yet**: they change how the game logs in (PIE keeps working, but
Standalone and packaged builds then need Steam running). Do them when you are ready; check each
setting against the UE 5.8 Online Subsystem Steam docs, since names move between engine versions.

## 1. Plugins

In the editor: Edit → Plugins, enable **Online Subsystem Steam** (it pulls in Online Subsystem and
Steam Sockets). Restart the editor. This adds them to `Crossguard.uproject`.

## 2. Config

Add to `Config/DefaultEngine.ini`:

```ini
[/Script/Engine.GameEngine]
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")

[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
; 480 is Valve's public test app (Spacewar), for development only.
SteamDevAppId=480
bInitServerOnClient=true

[/Script/OnlineSubsystemSteam.SteamNetDriver]
NetConnectionClassName="OnlineSubsystemSteam.SteamNetConnection"
```

## 3. Testing

- PIE still uses the NULL online subsystem: players get stand-in ids (`session-<n>`), fine for
  testing guilds within one session.
- To see real Steam ids: run **Standalone Game** (or a packaged build) with the Steam client
  running and logged in; the Steam overlay (Shift+Tab) shows it worked. Two real players need two
  Steam accounts on two machines.

## 4. Before release

- Your own Steam app id from Steamworks (the Steam Direct fee), replacing 480.
- A backend to save guilds and progression keyed by Steam id (roadmap: "Saving progression").
- Dedicated servers authenticate players with Steam sessions; matchmaking passes `?Party=`.
