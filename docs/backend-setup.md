# Backend setup (Nakama)

The online backend is **Nakama** (self-hosted, open source) with PostgreSQL
([design-decisions.md](design-decisions.md), "Online architecture"; research:
[backend-comparison.md](backend-comparison.md)). This page covers running it on the dev PC, the keys,
configuring dedicated servers, and looking at saves. Steps 1 and 2 of the plan are built: the backend
in Docker with our Go module, and the game's save backend and player identity on Nakama.

## What is where

| Part | Where |
| --- | --- |
| Docker set-up: Nakama **3.41.0** + PostgreSQL **16.15** | `Backend/docker-compose.yml` |
| Nakama config (non-secret defaults) | `Backend/nakama/local.yml` |
| Keys (template; the real `Backend/.env` is gitignored) | `Backend/.env.example` |
| Nakama image with our module (built by compose) | `Backend/nakama/Dockerfile` |
| Go runtime module: RPCs, auth policy, tests | `Backend/nakama/modules` (`main.go`, `saves.go`, `session.go`, `auth.go`, `*_test.go`) |
| Start / stop / wipe | `Tools\run_local_backend.bat` (Windows), `Tools/run_local_backend.sh` |
| End-to-end check against the running backend | `Tools/backend_smoke.py` |
| Game: REST client, subsystem, save backend, login option | `Source/Crossguard/.../Game/CrossguardNakamaClient`, `CrossguardNakamaSubsystem`, `CrossguardNakamaSaveBackend`, `CrossguardLocalPlayer` |
| Game tests (pure helpers) | `Crossguard.Nakama.*` (`Private/Tests/CrossguardNakamaTests.cpp`) |

Versions are pinned together: the Go plugin only loads into the Nakama build it was compiled
against. `Backend/nakama/modules/go.mod` requires **nakama-common v1.48.0** and Go **1.27.1**, the
versions in Nakama 3.41.0's own `go.mod`; the plugin is built by `heroiclabs/nakama-pluginbuilder:3.41.0`.
To upgrade: change `NAKAMA_VERSION` in the Dockerfile and `docker-compose.yml`, and nakama-common in
`go.mod` (to the version Nakama's `go.mod` lists for that tag), then `go mod tidy`.

PostgreSQL rather than CockroachDB: Nakama supports both; its official compose files ship both, and
the production plan (one VM + managed PostgreSQL) is PostgreSQL.

## Running it on the dev PC

Prerequisite: **Docker Desktop** (WSL 2 backend), running. Go is not needed (the image builds the
plugin); only `Tools/run_checks.sh` uses Go, to run the module's unit tests.

```
Tools\run_local_backend.bat            (start; first run builds the image, ~2 min)
Tools\run_local_backend.bat -Logs      (follow the Nakama log)
Tools\run_local_backend.bat -Down      (stop; data kept)
Tools\run_local_backend.bat -Reset     (stop and DELETE the local database: every account and save)
python Tools\backend_smoke.py          (16 checks: login, verify, save load/write/conflict, refusals)
```

The first run creates `Backend\.env` from `.env.example` with random keys. It prints:

- Game API: `http://127.0.0.1:7350` (clients and dedicated servers; open on the LAN).
- Console: `http://127.0.0.1:7351` (this PC only), user `NAKAMA_CONSOLE_USER`, password
  `NAKAMA_CONSOLE_PASSWORD` from `Backend\.env`.
- The dedicated-server flags, with the HTTP key.

By hand: `docker compose -f Backend/docker-compose.yml --env-file Backend/.env up -d --build`.
Only the plugin: `make -C Backend plugin` (pluginbuilder image). Module tests: `make -C Backend test`
or `cd Backend/nakama/modules && go test ./...`.

## Keys (`Backend/.env`)

| Key | Who uses it | Secret? |
| --- | --- | --- |
| `NAKAMA_SERVER_KEY` | Game clients, to log in. Same value as `ServerKey` in `Config/DefaultGame.ini` (`[/Script/Crossguard.CrossguardNakamaSubsystem]`, default `crossguard-dev`) | No (ships in the game) |
| `NAKAMA_HTTP_KEY` | Dedicated servers, for the server-to-server RPCs | **Yes**: never in a client build, never in `DefaultGame.ini` |
| `NAKAMA_SESSION_ENCRYPTION_KEY`, `NAKAMA_SESSION_REFRESH_KEY` | Nakama signs session tokens; `crossguard_verify_session` checks them | **Yes**; changing them logs everyone out |
| `NAKAMA_CONSOLE_USER` / `_PASSWORD` / `_SIGNING_KEY` | Developer console | **Yes** |
| `POSTGRES_PASSWORD` | Nakama to its database (not exposed outside Docker) | Yes |
| `STEAM_APP_ID`, `STEAM_PUBLISHER_KEY` | Steam login (Nakama checks tickets with the Steam Web API) | Publisher key: **yes** |
| `CROSSGUARD_DEV_AUTH` | `1`: device-id login allowed (development). **`0` on any shared server**: a device id is just a string the client picks | — |

Nakama does not read environment variables in its YAML, so `docker-compose.yml` passes the keys as
command-line flags (no `sh -x`, which would print them).

## Auth policy (Go module, `auth.go`)

- **Steam** login: on (once `STEAM_APP_ID` / `STEAM_PUBLISHER_KEY` are set).
- **Device id** login (and linking): only with `CROSSGUARD_DEV_AUTH=1`; refused (403) otherwise.
- Every other method (email, custom, Apple, Google, Facebook, Game Center, runtime providers): refused.
- Clients cannot write or delete objects in `characters`, `characters_quarantine`,
  `characters_conflicts` (refused whole, 403). They may read their own `characters` object.

## RPCs (server-to-server only)

Called by dedicated servers with the HTTP key as HTTP Basic auth (`Authorization: Basic
base64("<key>:")`) on `POST /v2/rpc/<id>?unwrap` (raw JSON in and out). A call with a player's session
is refused (403): the context then carries a user id.

| RPC | In | Out | Errors |
| --- | --- | --- | --- |
| `crossguard_save_load` | `user_id`, `character_id` | `{"found", "version", "record"}` (version `"*"` and record `null` when there is no save; `record` last) | 400 bad ids, 404 unknown user, 500 storage |
| `crossguard_save_write` | `user_id`, `character_id`, `record` (the save as a JSON object), `expected_version` (from the load or the last write; `"*"` = new character) | `{"version"}` (the new one) | 400 shape (not an object, no integer `version`, `playerId` ≠ `user_id`, over 8 MB, no `expected_version`), **409 version conflict** (record kept in `characters_conflicts`) |
| `crossguard_save_quarantine` | `user_id`, `character_id`, `reason` | `{"quarantined", "key"}` | copies the stored save to `characters_quarantine` (nothing deleted) |
| `crossguard_verify_session` | `token` | `{"user_id", "username", "steam_id", "expires_at"}` | 401 bad signature / expired / unknown account, 403 banned |

Storage: collection `characters`, key = character id (`main` for now: one character per account),
owner = the Nakama user id, `permission_read` 1 (owner), `permission_write` 0 (server only).
`characters_quarantine` and `characters_conflicts` are server-only (read 0, write 0), keyed
`<character id>-<unix nanos>`.

## Dedicated servers

```
<server> <map> -server -port=7777 -SaveBackend=Nakama -NakamaUrl=http://127.0.0.1:7350 -NakamaHttpKey=<NAKAMA_HTTP_KEY>
```

- `-SaveBackend=Nakama` (or `SaveBackend=Nakama` under `[/Script/Crossguard.CrossguardSaveSubsystem]`)
  picks `UCrossguardNakamaSaveBackend`. Without a URL or key it logs an error and saves to local files.
  Default: `Local` (PIE, tests, development).
- The HTTP key can come from the environment variable `CROSSGUARD_NAKAMA_HTTP_KEY` instead (better
  on hosted servers: other processes can read a command line). `-NakamaUrl=` defaults to `NakamaUrl`
  in `DefaultGame.ini`.
- With the key set, **remote players must log in with a verified Nakama session**: the client's
  login URL carries `?NakamaToken=<session token>` (added by `UCrossguardLocalPlayer` to every server
  login), `ACrossguardGameMode::PreLoginAsync` verifies it with `crossguard_verify_session` before the
  login goes on, and the Nakama user id becomes the player id (`PlayerIdOf`). Missing or bad token:
  "Log in first..." / "Your login could not be verified". Development servers started with
  `-AllowDevPlayerIds` also accept players without a token (development ids; such players are not
  saved by the Nakama backend: their id is not a Nakama user).
- Logs: `Character saves: Nakama at ...` at start; `Nakama: ...` warnings and errors per call. Saves
  Nakama did not take are kept in `Saved/Crossguard/NakamaUnsaved/<user id>.json` (log line "not
  stored in Nakama ... kept in ...").

Local test with the cluster script: start the backend, then the servers with the flags above (add
them to `Tools\run_local_cluster.ps1`'s server command lines by hand for now), log in from each
client's menu (**Log in**), then join. Each client of one PC needs its own `-CrossguardPlayerId=`
(the device id is `crossguard-dev-<name>`), or `-NakamaDeviceId=`.

## Clients

The main menu's **Log in** (above Play) calls `UCrossguardNakamaSubsystem::LoginWithDevice`: device
id `-NakamaDeviceId=`, else `crossguard-dev-<-CrossguardPlayerId= or the PC's login id>`. The account
line shows "Logged in as <name>". The token lasts `session.token_expiry_sec` (2 h); refreshing is a
TODO (log in again). **Steam login is a TODO**: it needs the Steam online subsystem for a Web API
ticket ([steam-setup.md](steam-setup.md)), then `POST /v2/account/authenticate/steam`.

Later option: the official **nakama-unreal** SDK (v2.11.x supports UE 5.8) once realtime sockets
are needed (parties, matchmaker, chat, notifications). The REST client here covers saves and login
without a plugin.

## Looking at saves in the console

Console `http://127.0.0.1:7351` → **Storage**: filter collection `characters` (or by user id). Each
object is one character (`key` `main`), owned by the player's user id; the value is the save JSON
([persistence.md](persistence.md), "Format"; key order differs: PostgreSQL stores JSONB). Refused
saves: `characters_quarantine`; writes that lost a version race: `characters_conflicts`. **Accounts**
shows users (device ids, Steam ids). Editing a save by hand in the console changes its version, so a
server holding the player then gets a conflict on its next write (by design).

## Known limits (next steps)

- **Hand-off between servers**: versions stop a stale write, they do not order the hub and raid
  servers. If a server loads a save before the previous server's last write lands (a travel normally
  takes longer than the write), the loading server's later writes are refused (kept in
  `characters_conflicts` and on its disk; the player keeps playing but is not saved). The backend
  skips writes that change only `savedAt`, so the logout after a travel does not cause this. Fix
  later: per-player save leases in the module (the server holding the player owns writes), granted
  with the match ticket.
- Session revocation (logout, single-session) is not seen by `crossguard_verify_session` (it checks
  signature, expiry, account and ban); tokens are short-lived.
- **Step 3**: parties and matchmaking on Nakama ([parties.md](parties.md),
  [backend-comparison.md](backend-comparison.md) "What to build first"): Nakama parties replace the
  in-server party (invites, leader, inbox via notifications), `PartyStartRaid` becomes a Nakama
  matchmaker ticket with the party id, match servers accept only the backend's tickets, the hub
  server picker lives in the module. Then GameLift Anywhere (step 4) and guilds as Nakama groups
  (step 5).
