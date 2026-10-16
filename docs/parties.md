# Parties and the raid launch

In-game parties in the hub, and how a party leaves the hub for a raid. Rules are engine-free in
`Source/CrossguardRules/Public/CrossguardRules/Party.h` (`FPartyRegistry`, numbers in `FPartyConfig`
in `Config.h`), unit tested in `TestParty` (`Tests/CrossguardRules/TestMain.cpp`). The game side is
`UCrossguardPartySubsystem` (server, game instance) and `UCrossguardPartyComponent` (on every
`ACrossguardPlayerState`).

## Ruling

From [design-decisions.md](design-decisions.md), "Teams and friendly fire": players enter a match
solo or in a premade team of **up to 8**; the party key travels as `?Party=<key>` and the game mode
puts everyone with the same key on one team. Friendly fire is always on; the team only decides who
counts as an ally.

Also ruled (owner): invites expire after **180 s**; only the leader invites, kicks and promotes;
membership changes and the raid launch happen only in the hub; a solo player may launch alone;
players can delete invites from their inbox (one at a time or all). Everything else below is a
**placeholder or an assumption** until ruled (listed at the end).

## Rules

- **One party per player.** A party has 1–8 members (`MaxMembers`, from the match's
  `MaxTeamSize`) in join order and exactly one leader.
- **Invite**: only the leader invites, and only players in no party. A player in no party who
  invites someone founds a party with themself as leader (only if the invite would succeed: a
  refused invite founds nothing). Invites expire after `InviteExpirySeconds` (**180 s,
  ruled**); an unexpired invite to the same player is refused (`AlreadyInvited`), an expired
  one is replaced. A full party cannot invite; pending invites do not reserve a place.
- **Accept / decline**: the invitee joins as a member. Accepting drops their invites from other
  parties. Fails if the invite expired, the player is meanwhile in a party, or the party is full. A
  failed accept keeps the invite (it expires on its own); declining removes it.
- **Invite inbox**: a player sees every open invite to them (one per inviting party) and can delete
  any one (`Decline`, by party) or all of them (`DeclineAll`), expired or not. Declining changes no
  party.
- **Leave**: anyone. A leader leaving hands the lead to the longest-standing member. The last
  member leaving disbands the party (its pending invites go with it).
- **Kick**: leader only, any other member.
- **Promote**: leader only; the target becomes leader (the old leader stays as a member).
- **Raid launch**: the leader, or a player in no party (who goes alone).
- Every operation returns an `EPartyError` (`None` on success) and changes nothing on error.

## In the game

- `UCrossguardPartySubsystem` (game instance subsystem, server) owns the `FPartyRegistry`. It
  survives map travel on that server, but lives **in memory only**: parties are gone when the server
  stops. Players are known by `UCrossguardGuildSubsystem::PlayerIdOf` (online id, else a
  per-session stand-in), the same ids as guilds.
- **Party id = `?Party=` key = team.** A new party gets a fresh GUID (32 hex digits, URL-safe). After
  every change the subsystem calls `ACrossguardGameMode::SetPlayerParty` for each affected player:
  the player state's `PartyKey` becomes the party id and the player moves onto that key's team in
  the game mode's `FTeamRegistry`, exactly as if they had logged in with `?Party=<id>`. So members
  are **allies in the hub at once** (party skills, spectating, kill XP), and the key is already on
  every player state when the raid launch travels. Leaving or being kicked puts the player on a team
  of their own.
- **Hub only** (ruled): invite, accept, leave, kick and the raid launch are
  refused with `OnlyInHub` while the game mode's `bIsRaid` is on, because the party is the raid
  team. Decline, decline all and promote work anywhere (they change no team).
- **Leaving the server** (logout from the hub) leaves the party; the rest get a new leader if need
  be, and the player's open invites are dropped. In a raid nothing changes on logout: a raider who
  disconnects mid-raid stays in the party and keeps their team slot while their character waits for
  a reconnect ([persistence.md](persistence.md), "Reconnecting"); after reconnecting they are on the
  same team. Even if their character dies while away, the party is unchanged (a raid never changes
  parties); only their team slot on the raid map is freed. A player who
  logs in without `?Party=` to a server whose subsystem still has them in a party (same process,
  e.g. after a non-seamless travel) gets that party's team.
- **What replicates**: everything on `UCrossguardPartyComponent` (party id, leader id, members with
  names and a leader flag, open invites, `IsLeader`) is **owner only**. A member sees the party
  through their own component; nobody else sees any of it, not even the party id (it is the raid's
  join key). Who is allied shows to everyone through the public team id, as before.
- Refusals come back as `ECrossguardPartyResult` (the rules' errors, then `OnlyInHub`, `NoPlayer`,
  `NoRaidDestination`, `HostInParty`, `TravelFailed`) through `OnPartyRefused`, and are logged as
  "Party request from <player> refused: <result>".

### API for a party panel (the P window, [ui.md](ui.md))

On `UCrossguardPartyComponent` (`ACrossguardPlayerState::GetParty()`), local player's own:

| Read | |
| --- | --- |
| `IsInParty()`, `IsLeader()`, `GetPartyId()`, `GetLeaderId()` | |
| `GetMembers()` | `FCrossguardPartyMemberView`: `PlayerId`, `DisplayName`, `bLeader`; join order. |
| `GetInvites()` | The invite inbox. `FCrossguardPartyInviteView`: `PartyId`, `InvitedBy` (name), `InvitedById`, `MemberCount`, `SecondsLeft` (when the server wrote the view). Refreshed on every change to it; an invite can expire while listed (accepting it is refused with `InviteExpired` and it drops off). |
| `GetInviteSecondsLeft(PartyId)` | Time left on an invite now, counted on this machine from when the view arrived (off by the network delay). |
| `OnPartyChanged` | Fires on the server and the owning client after every change. |
| `OnPartyRefused(ECrossguardPartyResult)` | A request was refused. |

Requests: `RequestInvite(APlayerState*)`, `RequestAccept(PartyId)`, `RequestDecline(PartyId)` (deletes
one invite), `RequestDeclineAll()` (empties the inbox; `NoInvite` if it was empty), `RequestLeave()`, `RequestKick(PlayerId)`, `RequestPromote(PlayerId)`, `RequestStartRaid()`.

### Console commands (also with the panel)

On `ACrossguardCharacter` (console, Output Log): `PartyInvite <player name>`, `PartyAccept` (your
first invite), `PartyInvites` (the inbox, numbered), `PartyDecline <number, inviter's name or party
id>` (empty: the first), `PartyDeclineAll`, `PartyLeave`, `PartyKick <name or id>`, `PartyPromote <name or id>`,
`PartyInfo` (party, members, invites, and the names of everyone on the server), `PartyStartRaid`.

## Raid launch

Owner's architecture: raids run on central dedicated match servers that many teams join; the hub on
dedicated hub servers; the backend (Nakama, [design-decisions.md](design-decisions.md) "Online
architecture") handles matchmaking. A player's PC never hosts a raid.

`PartyStartRaid` → `UCrossguardPartySubsystem::StartRaid`: leader (or solo), hub only. What it does
is decided by `PlanRaidTravel` (the same checks, no travel; the tests use it) with
`Crossguard::PickRaidTravel`:

1. It asks matchmaking (`UCrossguardMatchmakingSubsystem::GetMatchmaking()`, an
   `ICrossguardMatchmaking`, `Game/CrossguardMatchmaking.h`) for a match server for the party
   (`FCrossguardMatchRequest`: party key, player ids). Implementations:
   - `FCrossguardNoMatchmaking`: the backend stub, always "not available" (the Shipping default
     until the backend module exists and registers its own with `SetMatchmaking`).
   - `FCrossguardDevMatchmaking` (development builds, never Shipping; the default there): a fake
     assignment, the hub game mode's `RaidServerAddress` (`-RaidServer=<address>`), no ticket.
   **Assumed**: the request is synchronous for now; the backend's will answer later (a queue).
2. The plan:

| Situation | What happens |
| --- | --- |
| DEV-ONLY: development listen mode (`Crossguard::IsDevListenMode`: `-DevListenHub` or PIE, never Shipping), every player on this server is in the party (or the launcher is alone on it) and `RaidMapName` (`-RaidMap=`) is set | **ServerTravel**: the server loads `<RaidMapName>?Party=<id>` (plus `?listen` on a listen server). Seamless travel (`bUseSeamlessTravel`) brings the player states and their `PartyKey`; the raid map's game mode puts the party on one team again (`HandleSeamlessTravelPlayer`) and begins the raid. |
| Otherwise, matchmaking assigned a match server | **ClientTravel**: each member's client travels to `<address>?MatchTicket=<ticket>` (`UCrossguardMatchmakingSubsystem::BuildMatchTravelUrl`, `TRAVEL_Absolute`); the match server will check the ticket with the backend and take the team from it (not built). A development assignment travels with `<address>?Party=<id>` instead (no option for a solo player): the match server reads `?Party=` at login (`InitNewPlayer`). DEV-ONLY: refused (`HostInParty`) if the listen server's own player is in the party. |
| Neither | Refused: `NoRaidDestination` ("No raid server available right now"). |

Each traveller's save is written first (`SaveNow`). `?Party=` URLs are built by
`Crossguard::BuildRaidTravelUrl` (`UCrossguardPartySubsystem::RaidTravelUrl`), which refuses keys
that are not URL-safe; a ticket must be letters, digits, `-` and `_` (256 at most).

### Development mode: listen server (single PC)

DEV-ONLY: needs `-DevListenHub` (or PIE); never in Shipping.

1. PC 1 hosts the hub: the main menu's Play, or `open /Game/Maps/L_Hub?listen` (L_Hub holds an
   `ACrossguardHubMarker`, [hub-and-menu.md](hub-and-menu.md)); the leader can use the hub's raid
   portal instead of `PartyStartRaid`. Any other map opened with `?Hub` also plays as the hub
   (`ACrossguardGameMode::HubOption` turns `bIsRaid` off): start the game with
   `-RaidMap=/Game/L_Test` (or set `RaidMapName` on the game mode) and `open /Game/L_Test?listen?Hub`.
2. PC 2 joins (`open <PC 1 address>`).
3. PC 1 `PartyInvite <PC 2's name>` (`PartyInfo` lists names), PC 2 `PartyAccept`. Both are allies.
4. PC 1 (leader) `PartyStartRaid`: the whole server is the party, so the listen server travels to the
   raid map (an absolute travel: `?Hub` does not carry over, so it is a raid) and PC 2 follows; both
   on one team. Each traveller's save is written first (`SaveNow`).
5. Both extract (or die for the raid and stay spectating): once nobody is left in the raid, after
   10 s the listen server travels back to the hub (`HubMapName`, `?listen?Hub`) and PC 2 follows, still
   in the same party, the leader free to launch again ([hub-and-menu.md](hub-and-menu.md), "Back to
   the hub").

Seamless travel in PIE needs `net.AllowPIESeamlessTravel 1`; without it PIE falls back to a
non-seamless travel (player states are re-created; the subsystem's fallback above still finds the
party for each login, if the online ids stay the same). Test on two PCs or standalone processes.

Automated: `Crossguard.Net.SeamlessTravelHubToRaid` ([editor-tests.md](editor-tests.md), unbuilt)
plays this flow in one process: a listen server and a client in one party in the hub launch the raid
to `/Game/Maps/Dev/L_DevSandbox` with `net.AllowPIESeamlessTravel 1` (set for the test only), and
checks on the raid map that both arrived with the same engine PlayerId (seamless, not a fresh login),
player id, `DevPlayerId`, party (key, view, one team), banked XP, level, gold, weapon in hand and
carried items (through the save subsystem's record), that their raid began, and that the save entry
followed the new player state without a duplicate-login refusal. Then both extract and the server
travels back to `/Game/Maps/L_Hub` (if it exists): same party, gear carried out, free to launch
again. The ClientTravel path and a real two-machine travel stay manual.

Dedicated servers on one PC (development builds): `Tools\run_local_cluster.bat` starts a dedicated
hub with `-RaidServer=` (development matchmaking) and a dedicated match server with `-HubServer=`; the
party's clients travel to the match server and the hub stays.

### Back from the raid

A team that finishes goes back to the hub on its own while other teams play on (ruled, owner;
[hub-and-menu.md](hub-and-menu.md) "A team goes back"; `ACrossguardGameMode::ReturnTeamToHub`). The
team is the party: one `?Party=` key (a solo player is a team of one, no key). Each member's client
travels to the hub destination (`GetHubDestination`: the backend's hub server, else
`HubServerAddress` / `-HubServer=`) with `?Party=<key>`; the hub server puts them on one team from
the key (`InitNewPlayer`). The party itself is not rebuilt there: that needs the backend's party
roster (not built). Joining a party by key (the old leader-hosted hub) is removed.

## Assumptions and placeholders

Ruled (no longer placeholders): only the leader invites, kicks and promotes; invites expire after
180 s (`InviteExpirySeconds`); membership changes and the raid launch only in the hub (game mode
`bIsRaid` off); a solo player may launch a raid alone; invites can be deleted from the inbox.

Still assumed:

1. Inviting while in no party founds the party.
2. Pending invites do not reserve a place; accepting drops the player's other invites.
3. A leader leaving hands the lead to the longest-standing member; the last member leaving
   disbands the party.
4. Decline and promote work anywhere, in a raid too.
5. Logging out of the hub leaves the party; logging out of a raid does not. When the server goes
   back to the hub after a raid (`ACrossguardGameMode::ReturnToHub`), every party member who is not
   on the server any more (sent to the menu, released while away, logged out) leaves their party
   then (`UCrossguardPartySubsystem::DropAbsentMembers`); the rest keep their party in the hub.
6. Raid launch: ClientTravel to the match server matchmaking assigns (stub until the backend
   exists; development builds fake it with `RaidServerAddress` / `-RaidServer=` and `?Party=`, never
   in Shipping); in development listen mode only, ServerTravel when the whole server is the party and
   `RaidMapName` is set; refused when the listen host is in a party that is not the whole server.
7. Seamless travel is on for the game mode (needed to carry `PartyKey` through a ServerTravel).
8. Party ids are 32-hex-digit GUIDs; the party view (id included) replicates to its owner only.
9. `?Hub` on a server's map URL, or an `ACrossguardHubMarker` placed in the map (L_Hub), makes that
   map the hub (`bIsRaid` off). `RaidMapName` defaults to `/Game/Maps/Dev/L_DevSandbox` (placeholder).
10. A team arriving on a hub server gets its `?Party=` key's team but not its party (until the
    backend keeps parties). In development mode, teams that went back on their own leave their
    parties on the server's registry when the rest of the server returns (`DropAbsentMembers`).

## Not covered

Persistence (parties live in hub memory), coming back from a raid as a party on a dedicated hub
server (only the development whole-server return keeps parties), the backend's matchmaker and match
server allocation, match servers checking the ticket (and ignoring `?Party=` outside development), party chat, a ready
check, and invite by friends list (Steam). A member held for a reconnect shows as "(disconnected)" in the panel (`ACrossguardPlayerState::IsDisconnected`).
