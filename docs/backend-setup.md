# Backend setup (Nakama)

The online backend is **Nakama** (self-hosted, open source) with PostgreSQL
([design-decisions.md](design-decisions.md), "Online architecture"; research:
[backend-comparison.md](backend-comparison.md)). This page covers running it on the dev PC, the keys,
configuring dedicated servers, and looking at saves. Steps 1-3 of the plan are built: the backend in
Docker with our Go module; the game's save backend and player identity on Nakama; and (step 3) save
leases, parties, the game server registry, matchmaking with match tickets and the hub picker. The Go
module and every RPC run here end to end (`Tools/backend_smoke.py`); the Unreal side is unbuilt
([first-compile-triage.md](first-compile-triage.md) "Batch 4").

## What is where

| Part | Where |
| --- | --- |
| Docker set-up: Nakama **3.41.0** + PostgreSQL **16.15** | `Backend/docker-compose.yml` |
| Nakama config (non-secret defaults) | `Backend/nakama/local.yml` |
| Keys (template; the real `Backend/.env` is gitignored) | `Backend/.env.example` |
| Nakama image with our module (built by compose) | `Backend/nakama/Dockerfile` |
| Go runtime module: RPCs, auth policy, tests | `Backend/nakama/modules`: `main.go` (registration), `saves.go`, `leases.go`, `session.go`, `auth.go`, `parties.go`, `fleet.go` (server registry, `Allocator`), `matchmaking.go` (queue, tickets), `config.go` (tunables), `*_test.go` |
| Start / stop / wipe | `Tools\run_local_backend.bat` (Windows), `Tools/run_local_backend.sh` |
| End-to-end check against the running backend | `Tools/backend_smoke.py` |
| Game: REST client, subsystem (backend switch, server id, registry, tickets), save backend (leases), login option | `Source/Crossguard/.../Game/CrossguardNakamaClient`, `CrossguardNakamaSubsystem`, `CrossguardNakamaSaveBackend`, `CrossguardLocalPlayer` |
| Game: matchmaking on Nakama, parties on Nakama | `Game/CrossguardNakamaMatchmaking` (`ICrossguardMatchmaking`), `UCrossguardPartySubsystem` (backend mode) |
| Game tests (pure helpers) | `Crossguard.Nakama.*` (`Private/Tests/CrossguardNakamaTests.cpp`: helpers, save payloads, leases, matchmaking answers, party mirror) |

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
python Tools\backend_smoke.py          (72 checks: login, saves, lease hand-off, parties, matchmaking, tickets, hub pick)
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

## RPCs (server-to-server)

Called by dedicated servers with the HTTP key as HTTP Basic auth (`Authorization: Basic
base64("<key>:")`) on `POST /v2/rpc/<id>?unwrap` (raw JSON in and out). A call with a player's session
is refused (403): the context then carries a user id. The one exception, `crossguard_hub_find_mine`,
is the client's (its session; the menu's Play).

| RPC | In | Out | Errors |
| --- | --- | --- | --- |
| `crossguard_save_load` | `user_id`, `character_id` | `{"found", "version", "record"}` (version `"*"` and record `null` when there is no save; `record` last) | 400 bad ids, 404 unknown user, 500 storage |
| `crossguard_save_write` | `user_id`, `character_id`, `record` (the save as a JSON object), `expected_version` (from the load or the last write; `"*"` = new character), **`lease_token`** | `{"version"}` (the new one) | 400 shape (not an object, no integer `version`, `playerId` ≠ `user_id`, over 8 MB, no `expected_version`, no `lease_token`), **400 code 9 "save lease not held"** (lost lease: stop writing), **409 version conflict** (record kept in `characters_conflicts`) |
| `crossguard_lease_acquire` | `user_id`, `character_id` (`main`), `server_id`, `ttl_sec` (10-600, default 60) | `{"lease_token", "expires_at_ms", "save_version", "took_over_from"}` | **409** held by another server (retry); the holder asking again keeps its token |
| `crossguard_lease_renew` | + `lease_token` | `{"expires_at_ms"}` | 400 code 9: lost |
| `crossguard_lease_release` | + `lease_token` | `{"released"}` (false: not the holder) | — |
| `crossguard_party` | `op` (`invite`, `accept`, `decline`, `decline_all`, `leave`, `kick`, `promote`, `logout`), `actor_id`, `target_id`, `party_id`, `server_id` (the hub) | `{"result", "party_id", "disbanded", "now_ms", "players": {id: {"party_id", "invites"}}, "parties": {id: {"id", "leader_id", "members", "hub_server_id"}}}` | rule refusals are answers (`result` = an `EPartyError` name), 400 bad ids |
| `crossguard_party_get` | `user_ids` (≤ 256) | the same state for them | — |
| `crossguard_server_register` | `server_id`, `kind` (`hub` / `match`), `address` (`host:port`), `capacity` (0: 48 match / 64 hub) | `{"heartbeat_sec", "capacity", "ticket_ttl_sec"}` | 400 |
| `crossguard_server_heartbeat` | `server_id`, `players` | `{"known"}` (false: register again) | — |
| `crossguard_server_deregister` | `server_id` | `{"ok"}` | — |
| `crossguard_match_request` | `party_id` (`""` solo), `user_ids` (1-8; a party's must all be members), `server_id` (the hub) | `{"request_id", "state", "waited_sec", ...}` (below) | 400, 404 unknown user, 409 already queued |
| `crossguard_match_status` | `request_id` | `{"request_id", "state" (`queued` / `assigned` / `cancelled`), "waited_sec", "match_id", "server_id", "address", "tickets": {user id: ticket}}` | 404 unknown / abandoned |
| `crossguard_match_cancel` | `request_id` | `{"cancelled"}` | — |
| `crossguard_match_over` | `server_id`, `match_id` | `{"ok"}` (the match server is free again) | — |
| `crossguard_ticket_verify` | `ticket`, `server_id`, `user_id` (the verified session's) | `{"user_id", "party_id", "match_id", "expires_at_ms"}` | 401 unknown or expired, 403 another server's or player's |
| `crossguard_hub_find` | `user_id`, `party_id` | `{"server_id", "address"}` | 404 no hub with room |
| `crossguard_hub_find_mine` (client) | `{}` with the player's session | `{"server_id", "address"}` | 401 no session, 404 |
| `crossguard_save_quarantine` | `user_id`, `character_id`, `reason` | `{"quarantined", "key"}` | copies the stored save to `characters_quarantine` (nothing deleted) |
| `crossguard_verify_session` | `token` | `{"user_id", "username", "steam_id", "expires_at"}` | 401 bad signature / expired / unknown account, 403 banned |

Storage: collection `characters`, key = character id (`main` for now: one character per account),
owner = the Nakama user id, `permission_read` 1 (owner), `permission_write` 0 (server only).
`characters_quarantine` and `characters_conflicts` are server-only (read 0, write 0), keyed
`<character id>-<unix nanos>`. Step 3 adds `save_leases/<character id>` (owned by the player,
server-only), `parties/<party id>` (system-owned, server-only), `party_members/current` and
`party_invites/inbox` (owned by the player; they may read their own) and `match_tickets/<ticket>`
(system-owned, server-only). Clients cannot write or delete any of them (before-hook, `auth.go`).

## Step 3: leases, parties, matchmaking (how it works)

**Backend switch.** A dedicated server uses all of this when it has the HTTP key **and**
`-SaveBackend=Nakama` (`UCrossguardNakamaSubsystem::IsBackendEnabled`). Without it everything stays
in the process (development, PIE, tests): in-memory parties, the development matchmaking
(`-RaidServer=`), `?Party=` on match servers. Each process has a server id: `-ServerId=<id>`, else
`<computer>-<pid>-<random>`.

**Save leases** (`leases.go`, `UCrossguardNakamaSaveBackend`). One server owns a player's save at a
time. A load first acquires the lease (`crossguard_lease_acquire`); held by another server (the one
the player is leaving, still writing) it retries every 1 s for up to 75 s (a crashed server's lease
expires after 60 s and is taken over). Writes carry the token; the save and a version-checked rewrite
of the lease are one transaction (`MultiUpdate`), so a server whose lease was taken cannot write even
a write already on its way. Renewed every 20 s; released after the player's last write when they
leave (travel, logout) and at shutdown. A lost lease stops writes for that player (kept on disk, like a
version conflict). Hub → raid: the hub flushes and releases on the travel; the raid server's acquire
waits for it.

**Parties** (`parties.go`, `UCrossguardPartySubsystem`; [parties.md](parties.md) "On Nakama"). The hub
server stays the player's authority (hub-only rules, player states) and calls `crossguard_party`;
Nakama holds the records with the same rules as `FPartyRegistry`, so parties survive hub → raid → hub
and are the same on every hub shard. Each hub keeps a mirror of its players' parties (refreshed on
arrival and every 5 s). Members on no game server (no live save lease) for 120 s leave their party.

**Server registry and allocation** (`fleet.go`). Dedicated servers register at `StartPlay`
(`kind` hub or match, `-PublicAddress=<host:port>` else `127.0.0.1:<port>`, capacity: 48 per raid,
`-RaidCapacity=`, 64 per hub, `-HubCapacity=`), report players every 10 s and deregister on exit; three
missed heartbeats drop them. The registry is in Nakama's memory (open-source Nakama is one node): after
a Nakama restart, heartbeats answer `known: false` and servers register again. Match servers are
handed out by an `Allocator`: today the **local fleet** (a free registered match server, one match per
process; freed by `crossguard_match_over`, deregistering, or nobody arriving within 180 s). GameLift
(Anywhere locally, then managed fleets) plugs in as another `Allocator`.

**Matchmaking** (`matchmaking.go`, `FCrossguardNakamaMatchmaking`). The raid launch queues the party
(`crossguard_match_request`) and polls `crossguard_match_status` every 2 s (gives up after 300 s). A
match forms when **2 teams** are queued or the oldest has waited **30 s** (placeholders,
`CROSSGUARD_MATCH_MIN_TEAMS`, `CROSSGUARD_MATCH_MAX_WAIT_SEC`); the allocator reserves a match
server and the queue fills it first come, first served, whole parties only, up to its capacity; no
backfill into a running match (placeholder). Every player gets a **match ticket** (random, stored in
`match_tickets`, bound to player, party, server and match, valid 120 s until used, then for the raid's
length for reconnects). Clients travel with `?MatchTicket=`; the match server checks it at
`PreLoginAsync` (`crossguard_ticket_verify`) and takes the team from the ticket's party; `?Party=` is
ignored there. Nakama's built-in matchmaker is not used: it matches realtime socket sessions, and our
servers talk to Nakama over HTTP with the server key.

**Hub picker.** A team leaving a match asks `crossguard_hub_find` (the party's hub, the one it
launched from, if registered with room; else the hub with the most room). The menu's **Play** (logged
in, not development mode) asks `crossguard_hub_find_mine` and joins that hub.

## Dedicated servers

```
<server> <map> -server -port=7777 -SaveBackend=Nakama -NakamaUrl=http://127.0.0.1:7350 -NakamaHttpKey=<NAKAMA_HTTP_KEY>
         [-ServerId=hub-1] [-PublicAddress=203.0.113.5:7777] [-RaidCapacity=48 | -HubCapacity=64]
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

### Local cluster with the backend (one PC)

```
Tools\run_local_backend.bat                 (Docker Desktop running; once)
Tools\run_local_cluster.bat -Backend        (hub :7777, match :7778, 2 clients on the main menu)
```

`-Backend` starts both servers with `-SaveBackend=Nakama -NakamaUrl=... -ServerId=local-hub /
local-match -PublicAddress=127.0.0.1:<port>`, the HTTP key in the `CROSSGUARD_NAKAMA_HTTP_KEY`
environment variable (read from `Backend\.env`, not on the command line), no `-RaidServer=` and no
`-AllowDevPlayerIds`. The clients open on the main menu (each its own `-CrossguardPlayerId=`, so its own
device account): **Log in**, then **Play** (the backend names the hub). Party up (P), then **Start
raid** / the raid portal: "Looking for a raid..."; with two teams queued (or after
`CROSSGUARD_MATCH_MAX_WAIT_SEC`, set it to 5 in `Backend\.env` for one party, then
`run_local_backend.bat` again) everyone travels to the match server with a ticket. Back from the raid
the team lands on its hub with its party. What to look for in the logs: `Nakama: registered as hub
server local-hub`, `Matchmaking: ... assigned to 127.0.0.1:7778`, `save lease of ... not free yet`
(the hand-off; then the load), no `version conflict`, no `lease lost`.

## Clients

The main menu's **Log in** (above Play) calls `UCrossguardNakamaSubsystem::LoginWithDevice`: device
id `-NakamaDeviceId=`, else `crossguard-dev-<-CrossguardPlayerId= or the PC's login id>`. The account
line shows "Logged in as <name>". The token lasts `session.token_expiry_sec` (2 h); refreshing is a
TODO (log in again). **Steam login is a TODO**: it needs the Steam online subsystem for a Web API
ticket ([steam-setup.md](steam-setup.md)), then `POST /v2/account/authenticate/steam`.

Later option: the official **nakama-unreal** SDK (v2.11.x supports UE 5.8), or UE's WebSockets
module, once realtime sockets are needed (chat, live notifications to clients). Parties and matchmaking
do not need them: the hub servers drive both over HTTP. The REST client here covers it all without a
plugin.

## Looking at saves in the console

Console `http://127.0.0.1:7351` → **Storage**: filter collection `characters` (or by user id). Each
object is one character (`key` `main`), owned by the player's user id; the value is the save JSON
([persistence.md](persistence.md), "Format"; key order differs: PostgreSQL stores JSONB). Refused
saves: `characters_quarantine`; writes that lost a version race: `characters_conflicts`. **Accounts**
shows users (device ids, Steam ids). Editing a save by hand in the console changes its version, so a
server holding the player then gets a conflict on its next write (by design).

## Known limits (next steps)

- **Single Nakama node**: the server registry and the match queue live in the Nakama process's
  memory. A Nakama restart forgets them: servers register again on their next heartbeat, queued
  parties must start their search again (the hub's poll gets 404 and refuses the launch). Tickets,
  leases and parties are in the database and survive. Heroic Cloud / several nodes would need the
  registry and queue in storage.
- **Leases while a server hangs**: a server that stops renewing (frozen, not crashed) loses the lease
  after 60 s; its later writes are refused and kept on its disk.
- **Party mirror lag**: a change made on another hub shard reaches this hub's mirror on the next refresh
  (5 s) or the next request; the raid launch's leader check uses the mirror.
- **Stale queue entries**: a launch the hub stops polling (hub crash) is dropped after 30 s.
- **Display names** of party members on other servers are their user ids until they are seen here.
- Session revocation (logout, single-session) is not seen by `crossguard_verify_session` (it checks
  signature, expiry, account and ban); tokens are short-lived.
- **Next** (step 4): GameLift Anywhere as an `Allocator` (and `ICrossguardServerHost` in the game:
  ready, accept player session, game session end); then guilds as Nakama groups (step 5).
