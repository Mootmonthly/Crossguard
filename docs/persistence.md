# Persistence (character saves)

How characters are saved between sessions. Rulings and placeholders: [design-decisions.md](design-decisions.md),
"Saving". Everything below works behind a backend interface: local files (the default, for
development, PIE and tests) or **Nakama** on dedicated servers (`-SaveBackend=Nakama`; section "Nakama
backend", set-up in [backend-setup.md](backend-setup.md)).

| Part | Where |
| --- | --- |
| Save record, JSON format, migration, validation | `Source/CrossguardRules/Public/CrossguardRules/Save.h` (`Crossguard::FCharacterSave`), unit tested |
| JSON reader / writer (no dependencies) | `Source/CrossguardRules/Public/CrossguardRules/Json.h` (`Crossguard::FJsonNode`) |
| Backend interface | `Source/Crossguard/Public/Game/CrossguardSaveBackend.h` (`ICrossguardSaveBackend`) |
| Local file backend (default) | `UCrossguardLocalSaveBackend`: `Saved/Crossguard/Characters/<PlayerId>.json` |
| Nakama backend (dedicated servers) | `UCrossguardNakamaSaveBackend`; RPCs in `Backend/nakama/modules/saves.go` |
| Load, apply, save, debounce | `UCrossguardSaveSubsystem` (game instance subsystem, server only) |
| Tests | Rules: `Tests/CrossguardRules` (`TestJson`, `TestSave*`). Game: `Crossguard.Scenario.Save.*` ([editor-tests.md](editor-tests.md)) |

## What is saved

One record per character, keyed by player id:

- banked XP (the level follows from it), attribute allocation, respec tokens;
- Skill Library and the equipped skills (the loadout between raids);
- banked gold;
- bought stash tabs and every stash item: category, id, rarity, stack count, affixes, size, square
  and rotation;
- the gear carried between raids: inventory, Backpack and Quiver contents (grid and square), worn
  weapons (hand and weapon set), worn armor, worn Backpack / Quiver;
- a quarantine: items and skills set aside on an earlier load (see Validation).

Never saved: raid XP, anything picked up during a raid, the hotbar, the active weapon set, team and
party. **A save made during a raid lists no carried gear** (`"inRaid": true`): the gear is at risk
until the player extracts, so a server crash, or a raid that ends while its player is disconnected,
loses it like a failed extraction. A player whose own client crashes can reconnect (see Reconnecting).

## When

| Moment | Hook |
| --- | --- |
| Load | `ACrossguardPlayerState::OnSetUniqueId` → `PlayerJoined` (the record waits) |
| Apply | `ACrossguardGameMode::HandleStartingNewPlayer` → `PlayerReady`, after the pawn is possessed and before a raid begins; then the result is saved at once |
| Raid starts | `ACrossguardPlayerState::BeginRaid` → `SaveNow` (no carried gear from now on) |
| Raid ends (extraction or failure) | `ACrossguardPlayerState::EndRaid` → `SaveNow` |
| Logout | `ACrossguardGameMode::Logout` → `PlayerLeaving` (save, then forget the player). Not for a raider held for a reconnect: no save, the record is kept |
| Changes between raids | Progression, stash, inventory, equipment and loadout changes → `MarkDirty`; written after `SaveDebounceSeconds` (2 s); several changes share one write |
| Raid launch, return to the hub | Every traveller `SaveNow`, then `FlushPendingSaves`, just before the travel (`UCrossguardPartySubsystem::StartRaid`, `ACrossguardGameMode::ReturnToHub`; [hub-and-menu.md](hub-and-menu.md)) |
| Map change, shutdown | Pending writes are flushed (`FWorldDelegates::OnWorldCleanup`, `Deinitialize`) |

The record is built when a change happens (the pawn is surely there then); only the write waits. A
player with no pawn (logging out, spectating) keeps the carried gear of the last record. Extraction
saves with the pawn still holding the gear, then the character leaves the world
(`ACrossguardGameMode::HandleExtraction`), so nothing it could touch afterwards reaches the save.

Seamless travel (hub → raid map on the same server, and back to the hub when the raid is over)
replaces the player state and the pawn: the subsystem keeps the latest record per player id and
applies it to the new player state and pawn, so carried gear comes along (on the way back: what the
player extracted with; a player dead for the raid has none). Logging out forgets the record (another server may change the save before
the player comes back).

Applying goes through the components' server functions: `ACrossguardPlayerState::RestoreSavedProgression`,
`UCrossguardStashComponent::RestoreTabs`, `UCrossguardInventoryComponent::TakeEverything` /
`RestoreFromBody` (worn gear back on, the rest wherever it fits; what does not fit goes to the stash,
else the quarantine) and `UCrossguardLoadoutComponent::RequestLoadout` (checks the library and Skill
Capacity again). Carried items are placed again by the inventory, so their squares may differ from
the save.

## Reconnecting

Ruled (design-decisions.md, "Reconnecting"): a player whose client crashes or who logs out mid-raid
can log in again and continue, as long as their character has not died for the raid.

| Step | Where |
| --- | --- |
| Connection closes | The net driver destroys the controller. `ACrossguardPlayerController::PawnLeavingGame` (instead of destroying the pawn) asks `ACrossguardGameMode::HoldForReconnect`: on a raid map, a character still in the raid (alive or downed, not extracted, raid not over) is unpossessed and left where it is, still linked to its player state; the game mode keeps both under the player id (`HeldPlayers`). `CleanupPlayerState` then lets go of the player state without destroying it. |
| Logout | For a held player: no `PlayerLeaving` (no save; the save entry, its record and watcher stay), the team slot stays taken (`FTeamRegistry` key kept), the party is untouched (a raid never changes parties). |
| Away | The character can be hit, killed and looted. It is still the player's: team, player-kill XP for its killer, `HandlePermanentDeath` finds it. Its player state stays in `GameState->PlayerArray` without an owner. |
| Death while away | `HandlePermanentDeath` → `ReleaseHeldPlayer`: the body is unlinked from the player state (the failure's save takes no gear from it), `EndRaid(false)` (raid XP lost, saved), `PlayerLeaving`, team slot freed, player state destroyed; the id joins `FinishedPlayerIds`. |
| Login again | `InitNewPlayer`: same player id (online id, or development id) held → `RestoreHeldPlayer`: the held player state replaces the fresh one on the new controller (as the engine's `AGameMode::FindInactivePlayer` does), then the engine's `InitNewPlayer` registers it again; same team key. `HandleStartingNewPlayer` → `ResumeHeldCharacter`: possesses the same character (no `RestartPlayer`, no new raid, no save applied), Health kept. |
| Refused logins | A player id in `FinishedPlayerIds` (extracted or dead for this raid, also while away): "Your raid is over." A player id already playing here (also a reconnect before the server has noticed the old connection drop; it times out after the net driver's connection timeout): "You are already in this match." |
| Raid end / shutdown while away | Nothing runs: the stored record is the raid start's (`inRaid`, no carried gear), which loads as a failed extraction. |

Only the player id matches a reconnect, never the name: another account cannot take over. The id must
be stable: Steam ids are; `session-<n>` ids are new on every login, so without Steam only the
development ids below can reconnect. `PlayerCanRestart` still refuses a respawn to a player whose raid
here is over. The game mode's Player Controller Class must be `ACrossguardPlayerController` (the C++
default; a Blueprint game mode that sets its own must use a child of it).

## Player id

`UCrossguardGuildSubsystem::PlayerIdOf`: the **Nakama user id** verified by the server at login
(`ACrossguardPlayerState::NakamaUserId`; [anti-cheat.md](anti-cheat.md) "Player identity"), else the
online id (a Steam id with Steam), else in development builds the server-set
`ACrossguardPlayerState::DevPlayerId` (below), else `session-<PlayerId>`.
An id from the **Null** online subsystem (`DESKTOP-…-<guid>`, what PIE hands out without Steam) does
not count as an online id (`IsOnlineId`): it is new on every Play, which is why the first smoke
test on the PC (2026-10-16) wrote a fresh file each time instead of loading the last one.
**Session ids are not stable**: they count from the server's start, so after a restart another
player can get the same id and load someone else's character. Fine for development; set
`bSaveSessionIds=False` once Steam is in ([steam-setup.md](steam-setup.md)). The id is the server's
own (from the login), never a client's word (except the opt-in development ids below). A file whose
`playerId` does not match the id it was loaded for is refused.

**Development ids** (compiled out of Shipping; `ACrossguardGameMode::DevPlayerIdFor`, set at login
before the save loads, carried across seamless travel, never over an online id):

- The server's own player (standalone, PIE, listen-server host): `dev-<name>` from the server's
  `-CrossguardPlayerId=<name>` command line, else in PIE `pie-<PIE instance>`. So "stash an item, stop
  PIE, play again" finds the same save.
- A remote client: `dev-<name>` from its login URL option `?DevPlayerId=<name>` (e.g.
  `open 127.0.0.1?DevPlayerId=alice`), only if the server was started with `-AllowDevPlayerIds` (and
  during automation tests). It is the client's word: never on a real server. PIE clients in one
  process send no option, so they keep `session-<n>` ids.
- Names keep letters, digits, `-` and `_` (32 at most). A login claiming an id already playing is
  refused. The engine has set its unique id by then, so the save subsystem hears `PlayerJoined` and
  `PlayerLeaving` for it; both ignore a player state whose id another, still-present player state
  holds (told from seamless travel, which copies the engine's `PlayerId` to the new state), so the
  online player keeps their entry and goes on saving.

## Format

UTF-8 JSON, one document per character. Example (abridged):

```json
{
  "version": 1,
  "playerId": "76561198000000001",
  "savedAt": 1760000000,
  "progression": {
    "totalXp": 500,
    "level": 8,
    "attributes": {"str": 20, "agi": 15, "int": 10},
    "respecTokens": 2,
    "skillLibrary": ["fireball", "heal"],
    "equippedSkills": ["heal"]
  },
  "gold": 4321,
  "stash": {
    "purchasedTabs": 1,
    "tabs": [
      {"items": [
        {"category": "weapon", "id": "sword_one_handed", "rarity": "rare", "count": 1, "size": [1, 3],
         "x": 3, "y": 4, "rotated": true, "affixes": [{"id": "strength", "value": 2}, {"id": "action_speed", "value": 0.03}]}
      ]},
      {"items": []}
    ]
  },
  "carried": {
    "inRaid": false,
    "items": [
      {"category": "weapon", "id": "axe_one_handed", "rarity": "common", "count": 1, "size": [1, 3], "x": 0, "y": 0, "rotated": false, "worn": "mainHand", "set": 0},
      {"category": "container", "id": "quiver", "rarity": "common", "count": 1, "containerKind": "quiver", "size": [2, 2], "x": 0, "y": 0, "rotated": false, "worn": "container"},
      {"category": "ammo", "id": "arrow", "rarity": "rare", "count": 15, "size": [1, 3], "x": 2, "y": 0, "rotated": false, "grid": "quiver"}
    ]
  },
  "quarantine": {"items": [], "skills": []}
}
```

- Enum values are names, not numbers: categories `weapon armor ammo skillGem container material
  consumable throwable currency trinket tool other`; rarities `common` … `mystic`; `worn`
  `mainHand offHand armor container`; `grid` / `containerKind` `main backpack quiver`.
- `id`: weapon id, armor id, item-table id, or for Skill Gems the skill id. Backpacks and Quivers
  have no item id and are saved as `backpack` / `quiver`.
- `level` is written for people and tools and ignored on load (it follows from `totalXp`).
- `size` is the item's unrotated size when saved; loading takes the current item data instead.

## Versions and migration

`Crossguard::CurrentSaveVersion` (1) is written into every save. When the format changes:

1. Raise `CurrentSaveVersion`.
2. Add a `FSaveMigrationStep` to `DefaultSaveMigrations()` (`Save.cpp`) with `FromVersion` = the old
   version. It rewrites the parsed JSON (`FJsonNode`) in place into the next version's shape;
   returning false refuses the save.
3. Add a rules unit test that loads a document of the old version (see `TestSaveMigration`).

Loading runs every step from the file's version up. A file newer than the game, or older with no
path, is refused (`UnsupportedVersion`). Saves are always written in the current version.

## Validation on load

Two steps, both in the rules library:

**Refused** (`ParseCharacterSave`): not JSON, cut off, a missing or mistyped field, an integer out
of its range, an unknown category / rarity / slot name, duplicate keys, nesting deeper than 16,
more entries than `FSaveLimits` allows (far above any real character), over 8 MB, a newer version,
or another player's id. Nothing of a refused save is applied: the player keeps what they have (a
fresh character when joining), the file is renamed to `<id>.corrupt-<time>-<tag>.json` and kept for
support, and the next save writes the live state. Logged as a warning "Save for ... refused (...)".

**Repaired** (`ValidateCharacterSave`), each logged as "Save for ... repaired: <Issue>: ...":

| Problem | Repair |
| --- | --- |
| Negative XP, respec tokens | 0 |
| Gold below 0 or above `int32` | Clamped |
| Allocation negative or over the level's points | Every point unassigned (placeholder) |
| Bought tabs outside 0..3 | Clamped; the stash gets exactly 1 + bought tabs |
| Empty or duplicate skill; equipped skill not learned | Dropped |
| Skill the catalog does not know | Quarantined |
| Item with count < 1 or no id; affix with no id or a non-finite value | Dropped |
| Item the data tables do not know | Quarantined |
| Stack above the item's stack size | Clamped (placeholder) |
| Stash item overlapping another, outside its tab, or in a tab never bought | Moved to the first free square of any tab; no room: quarantined |
| Two items worn in one slot, a weapon set out of range, the wrong kind of item worn | Carried loose |
| Saved during a raid (`inRaid`) | Carried gear dropped (there is none in a save the game wrote), as a failed extraction |

The quarantine is written back on every save. On every load its entries are tried again: once the
game knows them (a data table restored) and there is room, they return to the stash. It holds at
most 2048 items (oldest dropped first).

## Plugging in a real backend

1. Write a class implementing `ICrossguardSaveBackend` (a `UObject`, like `UCrossguardLocalSaveBackend`):
   - `LoadCharacter(PlayerId, Callback)`: fetch the document; answer `Found` + document, `NotFound`
     (new character), `Corrupt` (stored but unusable) or `Failed` (could not ask; the player is then
     not saved this session, so a good save is never overwritten by a fresh one).
   - `SaveCharacter(PlayerId, Document, Callback)`: store it, replacing the old one, in call order per
     player id.
   - `QuarantineCharacter(PlayerId, Reason)`: keep the refused document aside; never delete it.
   - Callbacks on the game thread (`AsyncTask(ENamedThreads::GameThread, ...)` from HTTP threads).
2. At startup, before players join: `UCrossguardSaveSubsystem::Get(World)->SetBackend(NewObject<UMyBackend>(...))`
   (for example in the game instance's `Init`).
3. The document is opaque to the backend: store it as a string/blob keyed by player id. Querying
   fields (leaderboards) can read the JSON; writes go through the game only.
4. With several servers the backend must stop two servers saving one character at once (the hub and
   a raid server): per-player leases or a version check on write. The local backend assumes one
   server; the Nakama backend checks versions (below).
5. An asynchronous backend implements `FlushWrites(TimeoutSeconds)`: the subsystem calls it at
   shutdown (`Deinitialize`, after the debounced saves), and it must block until the last writes are
   answered, keeping what did not make it.

Settings (`Config/DefaultGame.ini`, `[/Script/Crossguard.CrossguardSaveSubsystem]`): `bSavingEnabled`
(default true), `bSaveSessionIds` (true), `SaveDebounceSeconds` (2.0), `SaveBackend` (`Local`;
`-SaveBackend=` overrides), `ShutdownFlushSeconds` (10.0).

## Nakama backend

`UCrossguardNakamaSaveBackend`, on a dedicated server started with `-SaveBackend=Nakama
-NakamaUrl=<url> -NakamaHttpKey=<key>` (or the key in `CROSSGUARD_NAKAMA_HTTP_KEY`). Set-up, keys and
the console: [backend-setup.md](backend-setup.md).

| Topic | How |
| --- | --- |
| Where | Nakama storage object `characters/<character id>` owned by the player's Nakama user id. Character id `main` (placeholder: one character per account until character select). Value: the save document above (stored as JSONB: key order and spacing change, content does not) |
| Who writes | Only game servers, through the server-to-server RPCs `crossguard_save_load` / `crossguard_save_write` / `crossguard_save_quarantine` (HTTP key). Clients may read their own object, never write it (`permission_write` 0 and a before-hook that refuses client writes to the collection) |
| Player id | Must be a Nakama user id (a verified `?NakamaToken=` login). Any other id loads as `Failed` (the player is not saved this session) |
| Versions | The load returns the storage version (`"*"` when there is no save); every write sends it as `expected_version` and gets the new one. A write from a server whose version is stale is refused (HTTP 409); the refused record is kept in `characters_conflicts`. That server then stops writing the player until they load again. This replaces "two servers overwrite each other" with "the stale server cannot write" |
| Unchanged saves | A document equal to the newest one written (or being written) except for `savedAt` is not written again: a travel's pre-travel save plus the logout save would otherwise move the version under the server the player just joined |
| Order | One write per player in flight; saves made meanwhile merge into the next write (the newest document carries them). A load for a player with writes pending waits for them |
| Failures | No answer, 5xx, 429: retried after 1, 2, 4, 8, 16 s (loads: 2 retries). Then, and on any other refusal: the document is kept in `Saved/Crossguard/NakamaUnsaved/<user id>.json` and logged as an error ("not stored in Nakama ... kept in ..."). A load that fails answers `Failed` |
| Refused saves | `QuarantineCharacter` copies the stored document to `characters_quarantine` (server-only); the next write replaces the original |
| Shutdown | `FlushWrites`: waiting writes go at once, the server waits up to `ShutdownFlushSeconds` for answers, anything unanswered is kept on disk too |
| Size | 8 MB per record (same cap as the loader; Nakama `socket.max_request_size_bytes` raised to 9 MB) |

Not built: per-player save leases (the hand-off risk in [backend-setup.md](backend-setup.md) "Known
limits"), scenario tests against a live Nakama (the pure helpers are tested: `Crossguard.Nakama.*`;
the RPCs end to end: `Tools/backend_smoke.py`).

## Tests

Saving is off while automation tests run (`GIsAutomationTesting`), so no test reads or writes real
saves. A save test calls `EnableForTesting(<temp folder>)` (a fresh local backend there, session ids
allowed) and `DisableForTesting()` at the end, then deletes the folder. Normal PIE saves to
`Saved/Crossguard/Characters/` like a server.

The "stash an item, stop, play again" smoke is scripted as `Crossguard.Scenario.Save.Smoke1StashAndStop`
and `Smoke2PlayAgain` (two PIE sessions, so the second one's fresh game instance loads `pie-0.json`
from disk; see [editor-tests.md](editor-tests.md)). The by-hand version still works the same way.
