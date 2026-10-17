# Guild Rules

Rules for guilds, implemented engine-free in `Source/CrossguardRules/Public/CrossguardRules/Guild.h`
(`FGuildRegistry`) with the numbers in `FGuildConfig` (`Config.h`). Tests: `TestGuild` in
`Tests/CrossguardRules/TestMain.cpp`.

## Ruling

From `docs/design-decisions.md` ("Inventory, death and Skill Gems"): a guild bank is a shared stash
per guild; **every member deposits; only officers withdraw, manage or redistribute.**

Everything else below is an **assumption until ruled otherwise** (listed in full at the end).

## Data

- Player ids and guild ids are opaque strings chosen by the caller (a Steam id, a backend id).
- A guild (`FGuild`) is plain data: id, name, members (player id + rank), pending invites, the bank
  (gold, item grid, items) and the bank log. `FGuildRegistry::All()` exposes every guild for saving;
  `Restore()` loads one back and checks it is consistent.
- The library reads no clocks. Time-dependent operations take `Now` (seconds on any clock the
  caller keeps, used consistently).
- Every operation returns an `EGuildError` (`None` on success) and changes nothing on error.

## Ranks and permissions

Ranks: **Leader** (exactly one), **Officer**, **Member**.

| Action | Member | Officer | Leader |
| --- | --- | --- | --- |
| Deposit gold / items | yes | yes | yes |
| Withdraw gold / items to self | no | yes | yes |
| Redistribute gold / items to a member | no | yes | yes |
| Rearrange the bank grid ("manage") | no | yes | yes |
| Invite | no | yes | yes |
| Kick | no | Members only | anyone but themself |
| Promote Member → Officer, demote Officer → Member | no | no | yes |
| Transfer leadership | no | no | yes |
| Leave | yes | yes | only as the last member |

## Membership

- **Create**: the name is trimmed of leading and trailing spaces, then must be 3–24 characters of
  ASCII letters, digits, spaces and `-`. Names are unique ignoring case (`Iron Wolves` blocks
  `iron wolves`); the trimmed name is stored as typed. The founder becomes Leader. A player can be
  in only one guild, so a player already in a guild cannot found another.
- **Invite**: a Leader or Officer invites a player who is in no guild. The invite expires
  `InviteExpirySeconds` (300) after it is sent (`Now >= ExpiresAt` is expired). Re-inviting a
  player with an unexpired invite is refused (`AlreadyInvited`); an expired one is replaced with a
  fresh one. A full guild cannot invite.
- **Accept / decline**: the invitee accepts (joins as Member) or declines. Accepting fails if the
  invite expired, the player is meanwhile in a guild, or the guild is full. An expired invite is
  not removed by a failed accept; `PruneExpiredInvites(Now)` drops expired invites, and declining
  removes an invite whether expired or not. A player can hold invites from several guilds at once.
- **Kick**: the Leader kicks anyone but themself; an Officer kicks Members only.
- **Promote / demote** (Member ↔ Officer): Leader only.
- **Transfer leadership**: the Leader makes another member Leader; the old Leader becomes Officer.
- **Leave**: anyone may leave, except the Leader while other members remain (`LeaderMustTransfer`).
  When the last member leaves, the guild disbands: it is removed, its name is free again, pending
  invites are dropped, and the bank's gold and items are returned in the result for the game to
  give to that player.
- Size cap: `MaxMembers` (50), Leader included.

## Bank

- **Gold**: a non-negative `int64` balance. Deposits must be positive and may not push the balance
  past `INT64_MAX`. Withdrawals and redistributions must be positive and no more than the balance
  (`NotEnoughGold`).
- **Items**: an item grid using the inventory grid rules (`FInventoryGrid`, `Inventory.h`), the
  size of one stash tab (10 × 30). Each bank item has a slot id assigned by the bank, an item id,
  rarity, count, unrotated size and an opaque `Payload` the game round-trips (affixes etc.).
  - Deposit at a given spot (must be free and inside the grid), or with no spot at the first free
    one, scanning rows top to bottom, left to right, trying unrotated then rotated (`NoRoom` if
    none).
  - Withdraw and redistribute take whole stacks.
  - Officers and the Leader can move or rotate items in the bank.
- **Redistribute**: an Officer or Leader gives bank gold or a bank item to a named member of the
  guild. The library removes it from the bank and returns what to hand over; where it lands
  (inventory, stash, mail) is the game's job. Redistributing to oneself counts as a withdrawal.
- The library does not know the player's own gold or inventory. On `None` the caller takes a
  deposit from the player, or gives them what was withdrawn or redistributed.
- **Log**: every deposit, withdrawal and redistribution adds an entry {player, action, item id +
  rarity + count or gold amount, recipient for redistributions, time}. Only the newest
  `BankLogSize` (100) entries are kept, oldest first. Moving items in the bank is not logged.

## Assumptions

1. Ranks are exactly Leader (one), Officer and Member.
2. Names: 3–24 characters after trimming leading/trailing spaces; ASCII letters, digits, spaces
   and `-` only; unique ignoring case; inner spaces are kept as typed (`A  B` and `A B` differ).
3. One guild per player.
4. `MaxMembers` = 50, Leader included. Pending invites do not reserve a place.
5. Only Leaders and Officers invite. Invites expire after `InviteExpirySeconds` = 300 s. A
   pending, unexpired invite cannot be re-sent; an expired one is replaced.
6. A player may hold invites from several guilds; accepting one makes the rest fail.
7. Kick: Leader kicks anyone but themself; Officer kicks Members only.
8. Promote/demote (Member ↔ Officer) and transfer leadership are Leader only; on transfer the old
   Leader becomes Officer.
9. The Leader must transfer leadership before leaving unless they are the last member; the last
   member leaving disbands the guild and receives the bank's contents.
10. "Manage" the bank means moving and rotating items in the bank grid (Officer and Leader).
11. The bank grid is one stash tab (`StashTabWidth` × `StashTabHeight`, 10 × 30). A restored
    guild keeps its saved grid size.
12. Items deposit and withdraw as whole stacks; stacks are never merged in the bank.
13. Redistributing to oneself is a withdrawal (same permission, logged as a withdrawal).
14. The bank log keeps the newest `BankLogSize` = 100 entries and records deposits, withdrawals
    and redistributions only (not moves). Entries also record rarity and, for redistributions, the
    recipient.
15. Gold amounts are positive whole numbers; the balance never passes `INT64_MAX`.
16. `Restore` does not enforce `MaxMembers`, so lowering the cap never breaks a saved guild.

## In the game

- `UCrossguardGuildSubsystem` (game instance subsystem, server) owns the `FGuildRegistry` and the
  game's copy of each bank stack (affixes included). It survives map travel. Two modes: **in memory**
  (development, PIE, tests: guilds are gone when the process stops) and **on Nakama** (dedicated
  servers with the backend on; below).
- Players are known by `PlayerIdOf`: their online id (a Steam id once the Steam online subsystem is
  on), else a per-session stand-in. Guilds that should outlive a session need the real ids.
- `UCrossguardGuildComponent` on each player state: guild name, id and rank replicate to everyone
  (nameplates); roster, invites and bank only to the owner. Requests run on the server; refusals
  come back as `ECrossguardGuildResult` (`OnGuildRefused`).
- Game rulings on top of the library (assumptions):
  - The bank is used **between raids only** (game mode `bIsRaid` off), like the stash. Membership
    works anywhere.
  - Gold moves between the bank and the player's banked gold; items between the bank and the
    character's inventory (whole stacks; not the item in hand).
  - Redistributing needs the recipient on the same server; gold lands in their banked gold, an item
    in their inventory (refused if it does not fit). Withdrawals likewise check for room first.
  - The last member leaving takes the bank: between raids only (if the bank holds anything), and
    refused if its items or gold would not fit.
  - Banked gold is an int32 per player: withdrawals and gifts that would overflow it are refused.
- The placeholder guild window (U; [ui.md](ui.md)) drives the same requests.
- Console commands for testing: see `ACrossguardCharacter` (`GuildCreate`, `GuildInviteNearest`,
  `GuildAccept`, `GuildInfo`, `GuildGold`, `GuildDeposit`, `GuildWithdraw`, `GuildGive`, `GuildRank`,
  `GuildKick`, `GuildLeave`).

## On Nakama (backend)

With the backend on (`-SaveBackend=Nakama` plus the HTTP key; [backend-setup.md](backend-setup.md))
guilds live in Nakama, so a guild is the same on every hub shard and survives travel and restarts.
Same pattern as parties ([parties.md](parties.md), "On Nakama"):

- **The rules run in Nakama**: `Backend/nakama/modules/guild_rules.go` is a Go copy of `FGuildRegistry`
  (same checks, same order, same `EGuildError` names). **Keep both in step**: a change to `Guild.cpp` is a
  change to `guild_rules.go`, and `guild_rules_test.go` mirrors `TestGuild` case for case (a case added
  to one is added to the other).
- **The hub server is authoritative towards players**: it checks the game's own rules (between raids,
  the item is in the inventory and not in hand, the recipient is on this server, room, banked gold) and
  calls `crossguard_guild` with the HTTP key. The answer updates its **mirror** (`FGuildRegistry::Mirror`,
  the guilds of the players on this server), refreshed when a player arrives and every **10 s**
  (placeholder). Requests return at once ("sent"); refusals come back later through `OnGuildRefused`. The
  guild window is unchanged.
- **Storage** (Nakama, server-only writes): `guilds/<id>` (the whole guild: roster, invites, bank, the
  newest 100 log entries), `guild_names/<lowercase name>` (unique names ignoring case),
  `guild_members/current` and `guild_invites/inbox` per player (one guild per player; the player's open
  invites), `guild_bank_log/<id>.<seq>` (**append-only**: every deposit, withdrawal and redistribution,
  never rewritten or deleted, kept after a disband) and `guild_requests/<request id>` (bank operations
  already applied). Every change is one transaction (`MultiUpdate`) checked against the versions it read;
  a concurrent change makes it run again.
- **The bank is atomic with the player**: a deposit, withdrawal, gift or the last member's share carries
  the save of the player whose gold or inventory changes (built by the hub with the change made, its
  version and save lease). Nakama writes the guild, the log entry and that save **together or not at
  all**: nothing can be duplicated or lost between the bank and the player. Two officers withdrawing the
  same gold or stack at once: one gets it, the other gets `NotEnoughGold` / `UnknownItem`. A retried
  request (no answer) carries the same request id and is never applied twice. A stale save version or a
  lost lease refuses it (nothing written).
- On the hub the move shows at once; if Nakama does not write it (refusal, no answer) the player is put
  back and saved again. Saves made while the operation is on its way wait for it. One bank operation per
  player at a time (`Busy` otherwise).
- **Nakama groups**: each guild is also a Nakama group with the same id (closed, at most 50), members and
  ranks mirrored after every change (Leader = superadmin, Officer = admin, Member = member), for the
  console, client reads and a later guild chat. Groups cannot hold the rules (one guild per player,
  exactly one Leader, invite expiry, names unique ignoring case, a transaction with the bank and the
  save), so storage decides and the group follows; a failed group update is repaired on the guild's
  next change. Clients may list groups, never create, join, leave or change them.
- Game results added for the backend: `BackendUnavailable` (Nakama did not answer or refused; nothing
  changed), `BankChanged` (the last member's leave: the bank was not what this hub saw; try again),
  `Busy`.
- Players need a Nakama login; others are refused (`NoPlayer`). Display names of members on other
  servers are their user ids until seen here.

## Not covered

Guild chat (the Nakama group is ready for it), the bank log in the UI (Nakama keeps it:
`crossguard_guild_log`), bank tabs beyond one, moving bank items from the game (the rules and the backend
have it: `move_item`), per-rank withdrawal limits, and guild names' profanity filtering.
