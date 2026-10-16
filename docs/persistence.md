# Persistence (character saves)

How characters are saved between sessions. Rulings and placeholders: [design-decisions.md](design-decisions.md),
"Saving". The backend (a database, platform cloud, ...) is not chosen yet; everything below works
behind an interface so that choice changes one class.

| Part | Where |
| --- | --- |
| Save record, JSON format, migration, validation | `Source/CrossguardRules/Public/CrossguardRules/Save.h` (`Crossguard::FCharacterSave`), unit tested |
| JSON reader / writer (no dependencies) | `Source/CrossguardRules/Public/CrossguardRules/Json.h` (`Crossguard::FJsonNode`) |
| Backend interface | `Source/Crossguard/Public/Game/CrossguardSaveBackend.h` (`ICrossguardSaveBackend`) |
| Local file backend (default) | `UCrossguardLocalSaveBackend`: `Saved/Crossguard/Characters/<PlayerId>.json` |
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
until the player extracts, so a crash or a disconnect in a raid loses it like a failed extraction.

## When

| Moment | Hook |
| --- | --- |
| Load | `ACrossguardPlayerState::OnSetUniqueId` → `PlayerJoined` (the record waits) |
| Apply | `ACrossguardGameMode::HandleStartingNewPlayer` → `PlayerReady`, after the pawn is possessed and before a raid begins; then the result is saved at once |
| Raid starts | `ACrossguardPlayerState::BeginRaid` → `SaveNow` (no carried gear from now on) |
| Raid ends (extraction or failure) | `ACrossguardPlayerState::EndRaid` → `SaveNow` |
| Logout | `ACrossguardGameMode::Logout` → `PlayerLeaving` (save, then forget the player) |
| Changes between raids | Progression, stash, inventory, equipment and loadout changes → `MarkDirty`; written after `SaveDebounceSeconds` (2 s); several changes share one write |
| Map change, shutdown | Pending writes are flushed (`FWorldDelegates::OnWorldCleanup`, `Deinitialize`) |

The record is built when a change happens (the pawn is surely there then); only the write waits. A
player with no pawn (logging out, spectating) keeps the carried gear of the last record. Extraction
saves with the pawn still holding the gear, then the character leaves the world
(`ACrossguardGameMode::HandleExtraction`), so nothing it could touch afterwards reaches the save.

Seamless travel (hub → raid map on the same server) replaces the player state and the pawn: the
subsystem keeps the latest record per player id and applies it to the new player state and pawn, so
carried gear comes along. Logging out forgets the record (another server may change the save before
the player comes back).

Applying goes through the components' server functions: `ACrossguardPlayerState::RestoreSavedProgression`,
`UCrossguardStashComponent::RestoreTabs`, `UCrossguardInventoryComponent::TakeEverything` /
`RestoreFromBody` (worn gear back on, the rest wherever it fits; what does not fit goes to the stash,
else the quarantine) and `UCrossguardLoadoutComponent::RequestLoadout` (checks the library and Skill
Capacity again). Carried items are placed again by the inventory, so their squares may differ from
the save.

## Player id

`UCrossguardGuildSubsystem::PlayerIdOf`: the online id (a Steam id with Steam), else
`session-<PlayerId>`. **Session ids are not stable**: they count from the server's start, so after a
restart another player can get the same id and load someone else's character. Fine for development;
set `bSaveSessionIds=False` once Steam is in ([steam-setup.md](steam-setup.md)). The id is the
server's own (from the login), never a client's word. A file whose `playerId` does not match the id
it was loaded for is refused.

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
   a raid server): per-player leases or a version check on write. Not built: the local backend
   assumes one server.

Settings (`Config/DefaultGame.ini`, `[/Script/Crossguard.CrossguardSaveSubsystem]`): `bSavingEnabled`
(default true), `bSaveSessionIds` (true), `SaveDebounceSeconds` (2.0).

## Tests

Saving is off while automation tests run (`GIsAutomationTesting`), so no test reads or writes real
saves. A save test calls `EnableForTesting(<temp folder>)` (a fresh local backend there, session ids
allowed) and `DisableForTesting()` at the end, then deletes the folder. Normal PIE saves to
`Saved/Crossguard/Characters/` like a server.
