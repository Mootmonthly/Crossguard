# Placeholder UI

A working UI built entirely in C++ (UMG widgets whose trees are made in code), so the game can be
played without console commands and without any `.uasset`. It is ugly on purpose: boxes and text.
Artists replace it piece by piece with Widget Blueprints (see [Replacing it](#replacing-it)).

Code: `Source/Crossguard/Public/UI`, `Source/Crossguard/Private/UI`. Test:
`Crossguard.Scenario.UI.WidgetsBind` (`Private/Tests/CrossguardScenarioUITests.cpp`).

## Rules it follows

- **Display only.** Widgets read replicated state (attributes, tags, inventory, trade view, guild
  view) and every button calls an existing `Request*` function on a component. The server validates
  each request (`docs/anti-cheat.md`); the UI never decides an outcome. Greyed-out buttons (a
  Member's "Withdraw", "Accept trade" before both lock) are hints only.
- **Local player only.** `ACrossguardHUD` (the game mode's HUD Class) makes widgets only for a local
  player controller with a viewport: never on a dedicated server, never for the scenario tests'
  extra server-side players.
- **No game rules changed.** The only gameplay-side edits are one line in the game mode (`HUDClass`)
  and one guard in the character's debug text (`bDebugHud`, which the HUD turns off for its player).

## Keys

| Key | What | Status |
| --- | --- | --- |
| I or Tab | Inventory screen (opens by itself when F opens a body or chest) | Ruled (Controls) |
| T | Trade: asks the nearest player within 5 m and opens the trade window; with a trade or request, opens / closes it | Ruled (Controls) |
| U | Guild window | Ruled (Controls) |
| Esc | Closes every window (in the editor, Esc also ends Play-In-Editor: use the X button there) | Ruled (Controls) |
| Hold Q | Skill wheel: hover a skill to select it, release; E casts it | Ruled (Controls) |
| 1–7, E, F, G, X | Unchanged (the character's bindings) | Ruled |

While a window or the wheel is open the mouse cursor shows and mouse look stops; keys still reach
the game (you can walk, use the hotbar, cast). Clicks on a window never reach the game, so they do
not attack.

## What exists

| Widget | Class | Shows / does |
| --- | --- | --- |
| HUD | `UCrossguardHudWidget` | Health bar and numbers, shield (Eldritch Shield), status effects (Stunned, Rooted, Silenced, Muted, Slowed, Blinded, Knocked up, Heal blocked, Stealthed, CC immune, Flying, Dragon form, Multiscale, Guarding), level, raid XP, carried and banked gold, both weapon sets (active one in brackets) and what is in hand, hotbar keys 3–7 with item names and counts (the readied one highlighted), castable skills with cooldown seconds and the E selection, the interact prompt ("F: Loot body", "F: Open chest", "F: Pick the lock", "F: Gather", "F: Open door", "F: Pick up ..."), a channel bar (gathering, lockpicking, extraction zone, a nearby portal's countdown), downed timer, "Dead for the raid", "Spectating <name>", and messages (refusals, trade results). |
| Skill wheel | `UCrossguardSkillWheelWidget` | Hold Q: castable skills (Active, Spell, Art) in a ring with cooldowns; hovering one selects it (`SetSelectedSkill`). Shown while the loadout's wheel is open (`OnSkillWheelOpened` / `Closed`). |
| Inventory | `UCrossguardInventoryWidget` | Equipment (two weapon sets, six armor slots, Backpack, Quiver), the 10 × 5 grid and the Backpack / Quiver grids (cells over each item's footprint, rarity colours, short names in one-square cells, stack counts, tooltips with name, count and rarity), selected item actions (equip main / off hand, equip, take in hand, hotbar keys 3–7, learn a gem, rotate, drop, put in the open body), hotbar keys (click to clear), and the open body or chest beside it (click a stack to take it, Take all, Close). |
| Trade window | `UCrossguardTradeWidget` | Ask the nearest player; answer a request; your items, your offer and theirs; banked gold; lock / unlock; accept; cancel; state line (who has locked or accepted, stacks n / 12). |
| Guild window | `UCrossguardGuildWidget` | Create a guild (name field) and answer invites; name, your rank and what it allows; roster with rank and online state, promote / demote / make leader / kick (greyed when your rank cannot); invite nearest player; leave; bank gold with deposit / withdraw; bank stacks with withdraw; your stacks with deposit. |
| Building blocks | `UCrossguardWidgetBase`, `UCrossguardWindowWidget`, `UCrossguardUIButton`, `UCrossguardItemGridWidget`, `CrossguardUI::*` helpers | Tree built in code, periodic refresh, a full-screen window frame that swallows stray clicks, a click/right-click/hover box, the item grid. |

### How to use the inventory

- Click an item to select it (yellow). Then click a square to move it there (Rotate toggles the
  orientation for that move), click an item of the same kind to merge into it, or click an
  equipment slot to equip it there (weapons into that set's hand).
- Right click an item: weapons go in the active set's main hand (shields in the off hand), armor and
  containers are worn, consumables and throwables are taken in hand, Skill Gems are learned
  (between raids only; the server says so otherwise).
- With nothing selected, click a worn item in the equipment list to take it off (a Backpack or
  Quiver must be empty first).
- Closing the inventory closes the open body or chest.

### Item cells

- Squares are 40 px (36 px in the loot grid), in the UI's 1920 × 1080 design units: at 1280 × 720
  the engine's default DPI scale shrinks them to about two thirds.
- A one-square-wide item shows a short name of at most 4 characters (initials of a name of several
  words, "Healing Potion" → "HP", small words like "of" skipped; else the first 3 letters, "Arrow" →
  "Arr") with the stack count ("x2") on its own line, in font size 11 (10 in the loot grid).
- A wider item shows its full name, wrapped, with the count on its own line (on the same line when
  the item is one square tall).
- Text that does not fit is cut at the cell's edge. Hovering any cell shows the full name, count and
  rarity ("Healing Potion x2 (Rare)").
- Placeholder (no ruling): the abbreviation rule, square sizes and font sizes.

## Approximations (display only)

The server does not replicate some timers, so the HUD counts them on this machine from when it saw
them start. They can be off by the network delay; the server's own timers decide.

| Bar | Shown as | Server truth |
| --- | --- | --- |
| Downed timer | 120 s from when this client saw State.Dead | `ReviveWindowTimer` (character) |
| Lockpicking | `LockpickSeconds(Dexterity)` from when `IsLockpicking` turned on | `LockpickTimer` |
| Extraction zone | `ExtractSeconds` from when the pawn overlaps the zone | zone countdown (server only) |
| Interact prompt | Same choice as the server's `FindInteractTarget` (nearest interactable in range within about 60°) | The server picks again on F |

Assumed: a chest is a loot container with a `LootTableId`, a body one without.

## Replacing it

Each widget is a normal `UUserWidget` subclass.

1. Create a Widget Blueprint whose parent class is the widget to replace (e.g.
   `CrossguardInventoryWidget`). If the Blueprint has its own design (a root widget), the C++ tree
   is not built: draw everything in the Blueprint from the same components (they are all
   Blueprint-readable) and the `On Refreshed` event, which fires every `Refresh Interval` seconds.
   An empty Blueprint keeps the C++ tree, so you can also only restyle by subclassing.
2. Make a Blueprint subclass of `CrossguardHUD`, set its widget classes (`Hud Widget Class`,
   `Skill Wheel Class`, `Inventory Widget Class`, `Trade Widget Class`, `Guild Widget Class`), and set
   it as the game mode Blueprint's **HUD Class**.
3. Rebind the UI keys with your own input assets on the HUD (`UI Mapping Context` and the four
   actions); empty uses the built-in defaults above.
4. Keep the rule: buttons call `Request*` functions only.

`bReplaceDebugHud` (on by default) turns off the character's on-screen debug text and its "tap Q
for the next skill" fallback for the HUD's player. Turn it off on the HUD to get the debug text back.

## Not built

- Drag and drop (click-to-select-then-click works), splitting stacks, the stash, vendors, the
  Skill Library / loadout editor, attributes, party UI, guild redistribution ("Give" to a member)
  and the guild bank log, rearranging the guild bank, a chat.
- A main menu and a spectator camera switcher (spectating shows the name only).
- Gamepad navigation.

## To do: party panel

Parties are built in code ([parties.md](parties.md), "API for a party panel"); the panel is not.
Read `ACrossguardPlayerState::GetParty()` (`UCrossguardPartyComponent`): `IsInParty`, `IsLeader`,
`GetMembers` (name, id, leader flag), `GetInvites` (party id, inviter, size); refresh on
`OnPartyChanged`, show refusals from `OnPartyRefused`. Buttons: invite a player (`RequestInvite`
with their player state), accept / decline an invite, leave, kick and make leader (leader only;
greyed otherwise), and "Start raid" (`RequestStartRaid`, leader only, hub only). Console stand-ins
until then: `PartyInvite`, `PartyAccept`, `PartyInvites`, `PartyDecline`, `PartyDeclineAll`,
`PartyLeave`, `PartyKick`, `PartyPromote`, `PartyInfo`, `PartyStartRaid`.

## To do: party invite inbox panel

Ruled: players can delete invites from their inbox. Built in code, no panel yet. List
`GetParty()->GetInvites()` (owner only: inviter's name and id, party id, member count; it refreshes
with `OnPartyChanged`), each row with **Accept** (`RequestAccept(PartyId)`) and **Delete**
(`RequestDecline(PartyId)`), plus **Delete all** (`RequestDeclineAll()`). Invites last 180 s; one
can expire while listed (Accept is then refused with `InviteExpired` and the row drops off), so the
panel may grey a row out after 180 s on its own. Decline works in a raid too. Console stand-ins:
`PartyInvites`, `PartyDecline <number, inviter's name or party id>`, `PartyDeclineAll`.

## Engine calls to check on the first compile (UE 5.8)

Written without an engine; these are the calls most likely to need a fix
([first-compile-triage.md](first-compile-triage.md) style):

| Call | Where |
| --- | --- |
| `UGameplayAbility::GetCooldownTimeRemaining(const FGameplayAbilityActorInfo*)` (public, on the CDO), `UAbilitySystemComponent::AbilityActorInfo` | `Private/UI/CrossguardUIHelpers.cpp:92,102` |
| A native `UUserWidget` building its tree into `WidgetTree` before `Super::RebuildWidget` (creating `WidgetTree` if `Initialize` has not) | `Private/UI/CrossguardWidgetBase.cpp:30,43` |
| `UWidgetTree::ConstructWidget<T>` for nested `UUserWidget` subclasses (buttons, grids) | `Public/UI/CrossguardWidgetBase.h:72` |
| `UTextBlock::SetFont` with `FCoreStyle::GetDefaultFontStyle("Regular", Size)` | `Private/UI/CrossguardWidgetBase.cpp:85`, `Private/UI/CrossguardUIButton.cpp:80` |
| A `Visible` user widget returning `FReply::Handled()` from `NativeOnMouseButtonDown` stops clicks reaching the game | `Private/UI/CrossguardWidgetBase.cpp:194` |
| `AActor::EnableInput` on the HUD creating an `UEnhancedInputComponent` (project default) that receives Enhanced Input actions | `Private/UI/CrossguardHUD.cpp:154,168` |
| `FInputModeGameAndUI::SetHideCursorDuringCapture`, `APlayerController::SetShowMouseCursor`, `AController::SetIgnoreLookInput` (a counter: the HUD keeps it balanced) | `Private/UI/CrossguardHUD.cpp:466-472` |
| `UWorld::GetGameViewport`, `APlayerController::SetMouseLocation` | `Private/UI/CrossguardHUD.cpp:70,310` |
| `UWidget::SetClipping(EWidgetClipping::ClipToBounds)`, `UBorder::SetPadding`, `UTextBlock::SetAutoWrapText` on item cells | `Private/UI/CrossguardItemGridWidget.cpp:76`, `Private/UI/CrossguardUIButton.cpp:47` |
| `USizeBox::SetMaxDesiredHeight` | `Private/UI/CrossguardInventoryWidget.cpp:146` |
| `CreateWidget` + `TakeWidget` under `-nullrhi` (the test) | `Private/Tests/CrossguardScenarioUITests.cpp:127` |
| Tab may be taken by Slate focus navigation while a text field has focus; I always works | — |
