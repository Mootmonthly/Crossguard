# Gameplay Ability System Setup

How Crossguard's stats and skills map onto Unreal's Gameplay Ability System (GAS).

## Ownership

- `ACrossguardCharacter` owns the `UCrossguardAbilitySystemComponent` (ASC) and the
  `UCrossguardAttributeSet`. Replication mode is Mixed.
- The server calls `InitializeAttributes(Level, Allocation)` on possession. It validates the
  allocation with `CrossguardRules`, then fills every stat and sets Health to MaxHealth.

## Stats

Every stat in the Stats doc is an attribute on `UCrossguardAttributeSet`. Fractions are stored as
fractions: `0.15` = 15%.

| Kind | Base value comes from | Modified by |
| --- | --- | --- |
| STR, AGI, INT | Player allocation | Effects (Heroic Might `+10`, Unleash `×2`) |
| Attribute-derived stats (MaxHealth, ADB, AR, MoveSpeed, CDR/cast time per category, ...) | `Crossguard::DeriveStats`, rewritten by the ASC whenever STR, AGI, INT or MHPB changes | Effects stack on top (Add, Multiply) |
| Everything else (AAD, AP, AMD, APH, AMH, Melee/Ranged/Minion CDR, MHPB) | 0 | Effects |

MaxHealth base = derived Max HP × (1 + MHPB). When MaxHealth changes, Health keeps its
percentage.

Health and MaxHealth replicate to everyone. Every other stat replicates to the owner only.

## Skills

Every skill is a subclass (C++ or Blueprint) of `UCrossguardGameplayAbility`. Set:

- `SkillId`: the id in `Data/Skills/Skills.json`.
- `SkillCategory`: Active, Spell, Art or Passive.
- Asset tags: `Skill.Weapon.Melee`, `Skill.Weapon.Ranged` and/or `Skill.Minion` when they apply.
- `BaseCooldownSeconds`, `BaseCastTimeSeconds`.
- `CooldownTags`: one unique tag per skill, e.g. `Cooldown.Skill.Fireball`.
- `RequiredWeapons`: any one must be equipped to activate (e.g. Cleave: `AnyMelee`).

### Cooldowns

Every ability uses the native `UCrossguardCooldownEffect` by default (no asset needed). The ability
fills its duration with the cooldown after CDR and adds its `CooldownTags`.
CDR = category CDR + matching Melee/Ranged/Minion CDR, summed and capped at 50%.

Cast time: call `GetReducedCastTime()` for wait tasks and cast bars. Uncapped.

### Damage and healing effects

Native effects `UCrossguardDamageEffect` and `UCrossguardHealingEffect` wrap the executions; use
them directly or make Blueprint effects with the executions.

- Damage: an Instant effect with the execution `UCrossguardDamageExecution`. Set
  `SetByCaller.Damage.Attack` and/or `SetByCaller.Damage.Magic` to the base damage, and
  `SetByCaller.Coefficient` when the skill lists one.
- Healing: an Instant effect with `UCrossguardHealingExecution`, using
  `SetByCaller.Healing.Physical` or `SetByCaller.Healing.Magical`.

The executions call the same formulas as the unit tests in `Tests/CrossguardRules`.

## Skill data in Unreal

`Data/Skills/Skills.json` stays the source of truth.

1. After editing it, run `python3 Tools/export_skill_table.py`. It writes
   `Data/Skills/SkillTable.json` in Unreal's data table format (`FCrossguardSkillRow` rows).
2. In the editor, import `SkillTable.json` as a Data Table with row struct `CrossguardSkillRow`
   (first time: drag it into `Content/Data/`, save as `DT_Skills`). Later: right-click → Reimport.
3. Create one `CrossguardSkillCatalog` data asset (`DA_SkillCatalog`). Set `SkillTable` to
   `DT_Skills` and add each implemented skill to `AbilityClasses` (skill id → ability class).
   Data validation flags ids not in the table and classes whose `SkillId` or category disagree.

## Loadout

`UCrossguardLoadoutComponent` (add it to the character, set `SkillCatalog`):

- `RequestLoadout(Skills)`: the server validates with `Crossguard::ValidateLoadout` against
  Skill Capacity at the character's level, then grants one ability per effective skill. Bundle
  components are granted as separate abilities; a component shared by two bundles is granted once.
- Rejections reach the owning client through `OnLoadoutRejected` with the reason.
- `ValidateLoadout(Skills)` checks without applying, for UI.
- Equipped skills replicate to everyone.

## Passive skills

Subclass `UCrossguardPassiveAbility`. It activates on the server when granted and applies its
`PassiveEffects` (Infinite effects holding the stat modifiers) until unequipped. Example: Heroic
Might = one Infinite effect adding 10 to Strength.

## Controls

`ACrossguardCharacter` builds default Enhanced Input bindings in code (no assets needed): WASD
move, mouse look, Space jump, hold left mouse to attack with the main hand, right mouse for the
secondary action. Set the character's Input properties to your own `InputAction` /
`InputMappingContext` assets to rebind. Camera: first person at eye height; the character faces the
camera direction. The body mesh is hidden from its owner until first-person arms exist.

## Weapons and equipment

- Import `Data/Items/WeaponTable.json` as a Data Table with row struct `CrossguardWeaponRow`
  (`DT_Weapons`) and set it as the character's `EquipmentComponent.WeaponTable`.
- `UCrossguardEquipmentComponent` holds the main-hand and off-hand `FCrossguardWeaponItem`
  (weapon id, rarity, affixes). The server validates changes with `Crossguard::ValidateEquipment`.
- It applies one native `UCrossguardEquipmentEffect`: shield AR, plus `Equipment.Wielding.<Type>`
  tags (and `AnyMelee`, `TwoHandedMelee`). Mastery effects use these as tag requirements, e.g.
  Sword Mastery's Infinite effect requires `Equipment.Wielding.Sword`, so its bonuses and its
  Melee CDR only apply while a sword is held.
- Right click: `ACrossguardCharacter::TrySecondaryAction()` attacks with the off-hand weapon
  (`UCrossguardOffHandAttackAbility`) when dual wielding. Block and parry are not built yet.
- `ACrossguardCharacter::TryWeaponAttack()` activates `UCrossguardWeaponAttackAbility`: damage is
  the weapon's AD/MD at its rarity; the attack interval (weapon speed ÷ (1 + Action Speed)) limits
  the rate. Melee swings sweep for pawns in front; wands trace on the `Weapon` channel; bows and
  staffs spawn `ACrossguardProjectile` (set per-weapon projectile Blueprints for speed).
- Ammo: a weapon with `ammo` in its data (the bow: `arrow`) uses one per shot, taken from the
  Quiver first, then the main grid, then the Backpack. No ammo, no shot.

## Inventory

`UCrossguardInventoryComponent` on the character (set its `ItemTable` to `DT_Items`, imported from
`Data/Items/ItemTable.json` with row struct `CrossguardItemRow`):

- The 10 × 5 main grid, plus the equipped Backpack and Quiver grids. Items keep a position and a
  rotation; sizes come from the weapon, armor and item tables (Skill Gems 1 × 1).
- Server authoritative. Clients ask by item id: `RequestMoveItem`, `RequestEquipItem`,
  `RequestUnequipWeapon`, `RequestUnequipArmor`, `RequestUnequipContainer`, `RequestUseItem`.
  Failures reach the owning client through `OnInventoryRejected`. Items replicate to the owner only.
- Equipping moves an item from the grid into `UCrossguardEquipmentComponent` and the old item back
  into the grid (rejected if it does not fit). The equipment component no longer takes items from
  clients.
- `AddItem` (server) tops up stacks first, then finds the first free spot; ammo tries the Quiver
  first. Dropping a stack on the top-left square of the same item merges them.
- Using a Skill Gem learns its skill (between raids) and consumes the gem.
- Consumables: `RequestReadyItem(ItemId)` puts a stack in hand (Q cycles through them for now);
  left click then uses one (`RequestStartUse` / `RequestStopUse`). The server applies the native
  `UCrossguardHealOverTimeEffect` (flat healing spread over the item's heal duration in 0.5 s ticks).
  Items with a use time channel; releasing early removes the effect.
- Stacks combine only the same item at the same rarity.
- Testing: the character's `DebugInventory` items are added at spawn.

### Stash

`UCrossguardStashComponent` on the player state (`GetStash()`): tabs of 10 × 30 squares, one to
start; `GrantStashTab()` (server) adds one once tab purchases exist. Between raids only:
`RequestStore` / `RequestStoreAnywhere` move an inventory stack in, `RequestRetrieve` moves one out,
`RequestMoveInStash` moves, rotates and merges, `RequestUseStashItem` learns a Skill Gem. Copied
across seamless travel; not saved yet. Shared tabs (subscription) need the backend.

### Gold and vendors

- Gold coins are `Currency` items (stacks of 100) while carried. `EndRaid(true)` moves every coin
  the character carries into the player state's banked `Gold`; dying loses them with the rest of
  the raid inventory.
- `UCrossguardVendorComponent` on the player state (`GetVendor()`): `RequestSell(Source, ItemId)`
  sells a whole stack from the inventory or stash for banked gold, between raids. Price per item:
  `SellValue + SellPerRarity × rarity level` from the item table (gemstones 50–300). Weapons and
  armor cannot be sold until their prices are decided.
- `UCrossguardStashComponent::RequestBuyStashTab()` spends 3000 banked gold for a tab, up to 3.

### Bodies and loot containers

- On death (`ACrossguardCharacter::HandleDeath`), everything the character wears and carries moves
  into an `ACrossguardLootContainer` spawned where it died: a 10-wide grid that grows taller to fit.
  A body is spawned even when empty. The dead character ragdolls; if it is removed (its player
  left), the container shows a ragdoll copy of its mesh instead.
- `Revive(HealthFraction)` (server, for revive spells) clears `State.Dead`, stands the character up
  and gives back what is still on the body: worn items go back on, the rest into the inventory.
  Testing: `DebugReviveDelay` revives the character that many seconds after death.
- E (`TryInteract`) opens the nearest container in front of you within its `InteractRange`
  (250 cm). Then `RequestTakeFromContainer(ItemId)` / `RequestPutInContainer(ItemId)` move stacks;
  the server re-checks range and that you are alive on every move. Dead characters cannot use their
  inventory.
- Container contents replicate to everyone; the open container replicates to its looter.
- Testing without UI: `bDebugLootTakesAll` on the character takes everything that fits on open.

## Armor

- Import `Data/Items/ArmorTable.json` (row struct `CrossguardArmorRow`, `DT_Armor`) and set it as
  the character's `EquipmentComponent.ArmorTable`.
- Players equip and unequip armor through the inventory (below).
- The equipment effect adds armor stats and affixes, sets `EmptyArmorSlots` (0–6), and grants
  `Equipment.Armor.Plate` (any plate piece) and `Equipment.Armor.PlateAllSlots` (plate in the five
  body slots).
- Skill effects use them: [Cor Leonis] modifiers use an attribute-based magnitude of
  `EmptyArmorSlots` × 0.05; [Armor Mastery] and [The Mithril Knight] effects require the plate tags.
  Abilities can also set `RequiredArmor`.

## Progression and Skill Library

`ACrossguardPlayerState` holds what outlives the pawn: banked XP (level is derived from it),
raid XP, attribute allocation, Skill Library and respec tokens. Not saved yet: it lasts for
the session and across seamless travel.

- `AwardXp(Source, QuestXp, Rarity)` (server): in a raid the XP is held as raid XP; between raids it
  is banked at once.
- `BeginRaid()` / `EndRaid(bExtracted)` (server): extracting banks raid XP plus 35, failing loses it. The
  game mode calls `BeginRaid` for every player when `bIsRaid` is on (default). Turn `bIsRaid` off on
  a hub or test map to try between-raid actions.
- Kills: when damage brings a character to 0 Health, the attribute set calls
  `ACrossguardGameMode::HandleKill`. The killer's player gets `PlayerKill` XP for a player victim,
  otherwise the victim's `KillXpSource` (default `BasicMonster`). No XP for yourself or allies.
- `RequestAttributeAllocation(Allocation)`: between raids only. Spending unspent points is free;
  lowering any attribute is a respec and uses one respec token. `GrantRespecTokens` (server) is
  for the store backend once a real-money purchase is verified; no store exists yet.
- `LearnSkill(SkillId, Catalog)` (server): between raids only; the inventory consumes the Skill Gem
  when it returns `None`.
- The character copies level and allocation from the player state (`ApplyProgression`). Turn on
  `bUseDebugProgression` to use the character's own `Level` and `AttributeAllocation` instead.
- The loadout only accepts skills in the player's Skill Library. Debug skills are learned
  automatically. AI pawns have no player state and skip the check.

## Teams

- `ACrossguardGameMode` (default pawn `ACrossguardCharacter`, player state
  `ACrossguardPlayerState`) assigns teams on login from the `?Party=<key>` URL option through
  `Crossguard::FTeamRegistry`: same key, same team, up to 8. Logins past the cap are refused.
- `ACrossguardPlayerState::TeamId` replicates to everyone; it also implements
  `IGenericTeamAgentInterface` for AI perception.
- `UCrossguardTeamLibrary::GetTeamId` / `AreAllies` work on pawns, controllers and player states.
- Friendly fire is always on: nothing checks teams before applying damage.

## Testing in the editor

`BP_CrossguardCharacter` → **Crossguard | Debug**:

- `DebugMainHand`, `DebugOffHand`: weapon id (e.g. `sword_one_handed`) and rarity.
- `DebugArmor`: armor pieces (e.g. `plate_helm`).
- `DebugSkills`: skill ids; needs the loadout's `SkillCatalog` set.
- `bRollDebugAffixes`: roll random affixes for each item's rarity.

They are equipped on the server at spawn. Every hit logs `<target> took <n> damage from <source>
(<hp> / <max> HP)` to the Output Log (category `LogCrossguard`). For a target dummy, place a
`BP_CrossguardCharacter` in the level with **Auto Possess AI** = **Placed in World**.
`showdebug abilitysystem` in the console shows attributes and tags.

## Checks

`Tools/run_checks.sh` runs the skill data validator, the table export check, the C++ struct check
and the rules unit tests.

## Not built yet

- Generating the stat-modifier effects for passives from the table (currently made by hand).
- Restricting when the loadout can change (e.g. only between raids).
- Saving progression; extraction points; a revive spell.
- Shields (Eldritch Shield), CC, Mute, Silence, minions.
- Client-side ability system init for pawns without a PlayerState (AI). Players are covered by
  `OnRep_PlayerState`.
- Animations, weapon-on-weapon collision (fencing).
- Inventory, loot and stash UI, a hotbar, single-item drops and pickups in the world, the guild
  bank, shared stash tabs, splitting stacks, treasure chests (reuse `ACrossguardLootContainer`).

The order these get built in is in [roadmap.md](roadmap.md).
- Verify in two-client PIE (`showdebug abilitysystem`) that `Equipment.Wielding.*` tags reach
  simulated proxies; they are added to the equipment effect spec's dynamic granted tags.
