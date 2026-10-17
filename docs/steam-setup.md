# Steam setup (accounts and login)

Steam is the login ([design-decisions.md](design-decisions.md), "Online architecture"): the game gets a
Steam session ticket from the Steam online subsystem and logs in to Nakama with it; Nakama checks the
ticket with Steam and links the account to the Steam id. The player id the servers use is then the
Nakama user id (verified at every server login), so saves, parties and guilds follow the Steam account.
Development builds without Steam log in with a device id instead ([backend-setup.md](backend-setup.md),
"Clients").

What is built (unbuilt in Unreal; triage "Batch 5" GN1-GN3): the menu's **Log in** uses Steam when the
Steam online subsystem is loaded and its user logged in (`UCrossguardNakamaSubsystem::Login`), the
ticket goes to `POST /v2/account/authenticate/steam?create=true&sync=false`, a ticket Steam does not know
yet is tried again (3 tries, 2 s apart); the session is refreshed before it expires. The backend's
Docker setup passes `STEAM_APP_ID` / `STEAM_PUBLISHER_KEY` to Nakama, and its load log says whether
Steam login is on.

Steps marked **OWNER** need the owner (Steam accounts, keys, the Steam client, the editor). They change
how the game starts (Standalone and packaged builds then talk to Steam), so they are not applied yet. Check
each setting against the UE 5.8 Online Subsystem Steam docs: names move between engine versions.

## 1. Plugin (OWNER)

In the editor: Edit → Plugins, enable **Online Subsystem Steam** (it pulls in Steam Sockets). Restart the
editor. This adds it to `Crossguard.uproject`; commit that. (`OnlineSubsystem` itself is already listed:
the game module depends on it for the login.)

## 2. Config (OWNER)

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

Assumed (to check on the PC): our dedicated servers keep the IP net driver (they are reached by
address, `-PublicAddress=`); if the Steam sockets driver gets in the way of `127.0.0.1:<port>` joins,
drop the `NetDriverDefinitions` line: the login does not need it.

## 3. The backend's Steam keys (OWNER)

Nakama checks tickets with Steam's partner Web API (`ISteamUserAuth/AuthenticateUserTicket` on
`partner.steam-api.com`), which needs:

- `STEAM_APP_ID`: **480** (Spacewar) for testing, your own app id before release.
- `STEAM_PUBLISHER_KEY`: a **publisher** Web API key (Steamworks → Users & Permissions → Manage Groups →
  your group → Create WebAPI Key). SECRET: only in `Backend\.env`, never in the repo or the game.

Put both in `Backend\.env`, then `Tools\run_local_backend.bat` (restarts Nakama with them). The Nakama
log line `Crossguard module loaded (... Steam login on, app 480 (Spacewar: testing only) ...)` confirms
it. **Unsure**: Steam may refuse a publisher key for app 480 (it belongs to Valve, not to your
publisher group): then Nakama answers 401 "Could not authenticate Steam profile" for a valid Steam
client, and testing needs your own app id (Steam Direct). Without keys Nakama answers "Steam authentication
is not configured" (the smoke test checks that).

## 4. Testing (OWNER)

- PIE uses the NULL online subsystem: the menu logs in with a device id (development builds), as
  before. Saves, parties and guilds work with that account.
- Steam login: run **Standalone Game** (or a packaged build) with the Steam client running and logged in
  (the overlay, Shift+Tab, shows Steam is there). Main menu → **Log in**: "Logging in with Steam...",
  then "Logged in as <name>". The game log: `Nakama: logged in as ... with Steam`. In the Nakama console
  (Accounts) the account shows the Steam id.
- Two real players need two Steam accounts on two machines.
- Shipping builds have no device login: without Steam the menu says "Steam is not running".

## 5. Before release (OWNER)

- Your own Steam app id from Steamworks (the Steam Direct fee), replacing 480 in `DefaultEngine.ini`
  (`SteamDevAppId`, and the `steam_appid.txt` the packaged build needs) and `STEAM_APP_ID`.
- `CROSSGUARD_DEV_AUTH=0` on every shared backend (device login off).
- Purchases are verified by our own server call (not built).
