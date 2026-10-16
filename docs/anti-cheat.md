# Anti-cheat review

Every client-to-server entry point in `Source/Crossguard`, and what each one replicates out. The
server is authoritative: clients send requests (ids, slots, positions), and the server looks
everything else up and recomputes the outcome. Rejected requests return early with a log line
(no `WithValidation` kicks).

Status: **fixed** (a hole closed in this review), **ok** (checked, no change needed), **note**
(a known gap that is unlikely, by design or too large for a minimal fix; see Notes).

## Server RPCs

| Entry point | Risk checked | Status |
| --- | --- | --- |
| `ACrossguardCharacter::ServerInteract` (loot, pick up, door, lockpick) | The target comes from a server-side search within its `GetInteractRange`; dead refused; lockpick finish re-checks range, life and the lockpick. Each call walks every actor in the world, so spamming the reliable RPC was a server CPU sink. | **fixed**: limited to one per 0.1 s (`InteractMinInterval`); the local/authority path is not limited |
| `ACrossguardCharacter::ServerSummonPortal` | `CanSummonPortal`: in raid, unlock time, not in a point of interest, once per raid; dead refused. | ok |
| `UCrossguardInventoryComponent::ServerMoveItem` | Item must be the caller's; container must be equipped and accept it; the grid checks the position. A position near `INT_MAX` wrapped `At.X + Width` to a negative number, passed the bounds check and indexed far outside the grid (server crash). | **fixed** in `FInventoryGrid::CanPlace` (also covers the stash), with unit tests |
| `ServerEquipItem` / `ServerUnequipWeapon` / `ServerUnequipArmor` / `ServerUnequipContainer` | Dead refused; item must be in the inventory; slots and sets range-checked (`Set >= WeaponSetCount`, `Armor.IsValidIndex`; negative set means "active set" by design); invalid container enums map to "no container". | ok |
| `ServerUseItem` (learn Skill Gem) | Server looks up the item; `LearnSkillGem` refuses in raid, unknown and already learned. | ok |
| `ServerDropItem` | Dead refused; the spot is computed by the server (traced, never through walls). | ok |
| `ServerReadyItem` / `ServerStartUse` / `ServerStopUse` | Dead and stunned refused; healing comes from the item table; one channel at a time; items are consumed on use. | ok |
| `ServerAssignHotbarSlot` / `ServerUseHotbarSlot` | `Hotbar.IsValidIndex(Slot)`. | ok |
| `ServerCloseContainer` / `ServerTakeFromContainer` / `ServerPutInContainer` | Only the open container (opened through interact); `CanReach` (range, alive) on every request; it closes when the looter walks away. | ok |
| `UCrossguardStashComponent::ServerMoveInStash` / `ServerStore` / `ServerRetrieve` / `ServerUseStashItem` | Between raids only; `Tabs.IsValidIndex`; grid bounds (see `CanPlace` fix); ids are looked up. | ok (hub-wide, no stash actor range: note) |
| `ServerBuyStashTab` | `CanBuyStashTab` (cap, gold, in raid), `SpendGold` checked. | ok |
| `UCrossguardEquipmentComponent::ServerSwitchWeaponSet` | Set range-checked; dead and dragon form refused. | ok |
| `UCrossguardVendorComponent::ServerSell` | Between raids only; price computed from the server's item table; item removed before gold is added. Gold could wrap `int32` at the cap. | **fixed**: refused with `AtCap` if `CanReceiveGold` fails |
| `UCrossguardTradeComponent::ServerRequestTrade` | Players only, both alive, not busy, within `MaxDistanceCm`. | ok |
| `ServerRespondToTrade` / `ServerLock` / `ServerAccept` / `ServerCancel` | State machine checks; completion re-checks life, distance, unchanged offers, banked gold, room on both sides and gold overflow, then swaps all or nothing. | ok |
| `ServerSetOffer` | Ids must be the caller's (not the readied item); `SetOffer` caps stacks, refuses duplicates, negative gold, gold above banked, and gold in raids. The id lookups ran before the stack cap, on an array of any size. | **fixed**: arrays over `MaxOfferedStacks` refused first |
| `UCrossguardLoadoutComponent::ServerRequestLoadout` | `ValidateLoadout` checks the Skill Library, capacity, duplicates and bundle conflicts. Was accepted on a dead body, re-granting passives on it. | **fixed**: refused while `State.Dead` (raid timing is an open design question: note) |
| `ACrossguardPlayerState::ServerRequestAttributeAllocation` | Between raids; `ValidateAllocation` refuses negatives and too many points; lowering needs a respec token. `Str + Agi + Int` was summed in `int`, so `{INT_MAX, INT_MAX, 2}` wrapped to 0 and passed: unlimited attributes. | **fixed**: summed as `long long` in `ValidateAllocation`, with a unit test |
| `UCrossguardGuildComponent::ServerCreate` / `ServerInvite` / `ServerAcceptInvite` / `ServerDeclineInvite` / `ServerKick` / `ServerSetRank` / `ServerLeave` | Everything goes through `FGuildRegistry` (rank permissions, name rules, invite expiry); invalid rank bytes refused. | ok |
| `ServerMoveGold` / `ServerGiveGold` | Between raids; `MIN_int32` and non-positive amounts refused; `CanReceiveGold` before adding. | ok |
| `ServerDepositItem` / `ServerWithdrawItem` / `ServerGiveItem` | Between raids; the item must be in the inventory (not readied); room checked before the bank lets go. | ok |

## Abilities

| Entry point | Risk checked | Status |
| --- | --- | --- |
| Skills and weapon attacks (`UCrossguardGameplayAbility`, `LocalPredicted`) | No client target data anywhere: the server traces its own hits from `GetActorEyesViewPoint` and applies damage, healing and revives with authority. Dead, Muted, Silenced, Stunned and dragon form block activation; weapon requirements and costs are checked in `CanActivateAbility` / `CommitAbility` on the server. | ok |
| Aim direction | The control rotation comes from the client (as in every UE shooter); range and cone checks are server-side, so only aim assist is possible. | note |
| Passives (`UCrossguardPassiveAbility`) | `ServerOnly`. | ok |
| Revive (Benediction) | The server finds the body within range; `Revive` checks the revive window and death. | ok |

## Console commands (`UFUNCTION(Exec)` on `ACrossguardCharacter`)

| Entry point | Risk checked | Status |
| --- | --- | --- |
| `Trade*` (`TradeItems`, `TradeNearest`, `TradeRespond`, `TradeOffer`, `TradeLock`, `TradeAccept`, `TradeCancel`) | Client-side stand-ins for a trade window: they only call the request functions, so the server RPCs above validate everything. | ok (remove once the UI exists) |
| `Guild*` (`GuildCreate`, `GuildInviteNearest`, `GuildAccept`, `GuildLeave`, `GuildInfo`, `GuildRank`, `GuildKick`, `GuildGold`, `GuildDeposit`, `GuildWithdraw`, `GuildGive`) | Same: client wrappers around the guild RPCs. | ok (remove once the UI exists) |

## Debug options

| Option | Risk checked | Status |
| --- | --- | --- |
| `DebugMainHand`, `DebugOffHand`, `DebugArmor`, `DebugSkills`, `DebugInventory`, `bRollDebugAffixes` | Not replicated and only read on the server (`PossessedBy`), so a client cannot set them; but a Blueprint default left on would hand every player free gear and skills in a shipped game. | **fixed**: `AllowsDebugOptions()` is false for players in Shipping builds (AI keep them) |
| `bUseDebugProgression` | Same: would skip the player's real level and allocation. | **fixed**: ignored for players in Shipping |
| `bDebugLootTakesAll` | Same: would loot whole containers on open. | **fixed**: ignored for players in Shipping |
| `DebugReviveDelay` | Same: free self-revives. | **fixed**: ignored for players in Shipping |
| `bDebugHud` | Client-side on-screen text only. | ok |
| Game mode (`bIsRaid`, `PortalUnlockSeconds`) | `EditDefaultsOnly`, server-side. | ok |

## Replicated properties

| Property | Risk checked | Status |
| --- | --- | --- |
| Loot container `Items` | Not replicated; the looter's `OpenContainerItems` is `COND_OwnerOnly`. | ok |
| Inventory (`Items`, backpack, quiver, hotbar, readied item, open container) | `COND_OwnerOnly`. | ok |
| Stash `Tabs` | `COND_OwnerOnly`. | ok |
| Trade `View` (both offers) | `COND_OwnerOnly`: only the two parties get it. | ok |
| Guild `Members`, `Invites`, `BankGold`, `BankItems` | `COND_OwnerOnly`, filled only for members. `GuildId`, `GuildName` and `Rank` are public. | ok |
| Player state: XP, gold, allocation, respec tokens, Skill Library, `bInRaid` | `COND_OwnerOnly`. | ok |
| Attributes | Only `Health`, `MaxHealth` and `AttackShield` reach everyone (health bars); the rest are `COND_OwnerOnly`. | ok |
| Equipment `MainHand`, `OffHand`, `Armor` | Reach everyone (visible gear), including affix rolls. | note |
| Stealthed characters | Hidden on each machine by `UpdateStealthVisibility`. With `crossguard.StealthHidesFromEnemies` (default on, placeholder) `ACrossguardCharacter::IsNetRelevantFor` returns false for viewers outside the character's team while `State.Stealthed`, so enemy clients get no position (the actor despawns there and is re-created when stealth ends). Own player and allies (spectators too) still get it. On stealth start the server closes the enemy connections' channels at once (no `RelevantTimeout` leak; classic net driver only). Covered by `Crossguard.Net.StealthHidesFromEnemyClient`. | **fixed** (behind switch; channel close pending local verification) |

## Notes

Left as they are, with the reason:

- **Stealth position leak.** Fixed behind the console variable `crossguard.StealthHidesFromEnemies`
  (default 1; set 0 on the server for the old, render-only hiding). `IsNetRelevantFor` is false
  for enemy connections while `State.Stealthed`; no reveal radius (the design has none). The
  enemy client's actor channel closes, so the character is destroyed there and re-created, with
  fresh replicated state (tags, attributes, equipment), when it is relevant again; stealth start
  and end call `ForceNetUpdate`. Open points for the play test:
  - The net driver keeps replicating an actor for `RelevantTimeout` (engine default 5 s) after
    its last relevant check, so the position reached enemies for the first ~5 s of a 12 s
    stealth. **Confirmed** (2026-10-16, UE 5.8, `Crossguard.Net.StealthHidesFromEnemyClient` on a
    dedicated PIE server): the enemy client dropped the stealthed character 4.3–4.5 s after
    `State.Stealthed` appeared. **Fix A (owner's choice), built, pending local verification:** when
    the server gains `State.Stealthed`, `ACrossguardCharacter::CloseStealthHiddenChannels` closes
    the character's actor channel (`EChannelCloseReason::Relevancy`) on every client connection it
    is hidden from (`IsHiddenByStealthFrom`, the same rule as `IsNetRelevantFor`; split-screen
    children count as that connection's viewers), so the enemy client drops it at once;
    `IsNetRelevantFor` keeps it from reopening. Only with the switch on; nothing on standalone (no
    net driver). `RelevantTimeout` is untouched. The test logs the measured time as
    `STEALTH LEAK: ... s` and fails above 0.5 s (placeholder).
  - Only the classic net driver calls `IsNetRelevantFor` and has actor channels to close. With
    Iris or a Replication Graph (replication driver) the channel close is skipped, and the hiding
    needs a filter there instead.
  - A listen-server host sees everything (no connection to filter); only remote clients are
    protected.
  - Server-side AI (monsters, minions) is unaffected: relevancy only concerns client connections.
  - `bAlwaysRelevant` must stay false on characters; `NetCullDistanceSquared` only hides more.
- **Gear affixes visible to everyone.** Equipped items replicate whole. Others only need the mesh
  and rarity. Strip it to a cosmetic struct once gear visuals exist.
- **Aim.** The aim is the client's control rotation, as in every UE shooter, so aimbots are a
  client-integrity problem (EAC/BattlEye), not a server one. Range, cone and line of sight are
  already checked on the server.
- **Movement and speed.** `UCharacterMovementComponent` corrects these on the server. Keep its
  client error checks on (the default; `bIgnoreClientMovementErrorChecksAndCorrection` stays false).
- **Stun while looting or using the inventory.** Stunned blocks skills and consumables, but not
  moving items, looting or interacting. This is a design question; it gives no outcome a player
  could not get a moment later.
- **Stash and vendor range.** Both work anywhere between raids (no NPC or stash actor yet). Add a
  range check to the hub actor when they exist.
- **Loadout during raids.** This is an open question in the roadmap. If the answer is "between
  raids only", add an `IsInRaid` check to `ServerRequestLoadout`.
- **Request spam.** All the other RPCs are cheap lookups on small arrays. Trade and guild invites
  rely on the busy state, the distance and the registry rules. Rate limits per connection
  belong with the dedicated server setup.
- **Gold at `int32` limits.** `EndRaid` adds extracted coins without `CanReceiveGold`. It would take
  about 2 billion coins carried out at once, which the inventory cannot hold.
- **Dropped items in the hub** pass items between players without the trade window. That is fine
  for items (the trade rules allow them); gold in raids is coins, which are items anyway.
- **Exec commands** are client-side wrappers only. Remove them when the trade and guild windows
  exist. In Shipping the console is off by default anyway.
