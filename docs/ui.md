# Placeholder UI

A working UI built entirely in C++ (UMG widgets whose trees are made in code), so the game can be
played without console commands and without any `.uasset`. It is ugly on purpose: boxes and text.
Artists replace it piece by piece with Widget Blueprints (see [Replacing it](#replacing-it)).

Code: `Source/Crossguard/Public/UI`, `Source/Crossguard/Private/UI`. Tests:
`Crossguard.Scenario.UI.WidgetsBind` (`Private/Tests/CrossguardScenarioUITests.cpp`),
`Crossguard.Scenario.UI.StashAndVendor` and `Crossguard.Visual.HubUI`
(`Private/Tests/CrossguardScenarioHubUITests.cpp`), `Crossguard.Scenario.UI.PanelsBind` and
`Crossguard.UI.RarityContrast` (`Private/Tests/CrossguardScenarioUIPanelTests.cpp`),
`Crossguard.Net.ReconnectShowsDisconnected` (`Private/Tests/CrossguardScenarioReconnectTests.cpp`).

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
| K | Attributes window (attribute points, Apply, respec) | Placeholder key (Controls) |
| P | Party window (members, invite, inbox, Start raid) | Placeholder key (Controls) |
| F at a stash chest / vendor NPC | Stash screen / vendor screen (hub only; the server agrees first) | Ruled (Controls: F interacts) |
| `OpenStash`, `OpenVendor` (console) | The same screens without a chest or NPC, for testing (refused in a raid) | Placeholder (debug) |
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
| Inventory | `UCrossguardInventoryWidget` | Equipment (two weapon sets, six armor slots, Backpack, Quiver), the 10 × 5 grid and the Backpack / Quiver grids (cells over each item's footprint, rarity colours, short names in one-square cells, stack counts, tooltips with name, count and rarity; items drag and drop), selected item actions (equip main / off hand, equip, take in hand, hotbar keys 3–7, learn a gem, split 1 or half off a stack, rotate, drop, put in the open body), hotbar keys (click to clear), and the open body or chest beside it (click a stack to take it, Take all, Close). |
| Trade window | `UCrossguardTradeWidget` | Ask the nearest player; answer a request; your items, your offer and theirs; banked gold; lock / unlock; accept; cancel; state line (who has locked or accepted, stacks n / 12). |
| Attributes window | `UCrossguardAttributesWidget` | Level, attribute points and unspent points; Strength, Agility, Intelligence with the planned value and - / +; Reset, Unassign all; Apply (free: only unspent points added), "Respec with token (n left)", "Respec for 1000 gold (you have n)" (greyed when the plan does not lower anything, is over the points, cannot be paid, or in a raid); refusals in the window. |
| Party window | `UCrossguardPartyWidget` | Party header (size, "you lead"); members in join order with "(you)", "[leader]", "(disconnected)" (held for a reconnect) or "(not here)"; Make leader and Kick per member (leader only); invite by name; Leave party; Start raid (leader or solo; hub); the invite inbox (inviter, members, time left of 180 s) with Accept / Decline per invite and Decline all; refusals in the window. |
| Team list | `UCrossguardTeamListWidget` | Always on, top right, only with teammates: each teammate's name, "(disconnected)" in orange while their player is away. |
| Guild window | `UCrossguardGuildWidget` | Create a guild (name field) and answer invites; name, your rank and what it allows; roster with rank and online state, promote / demote / make leader / kick (greyed when your rank cannot); invite nearest player; leave; bank gold with deposit / withdraw; bank stacks with withdraw; your stacks with deposit. |
| Stash screen | `UCrossguardStashWidget` | The inventory grids beside one stash tab (10 × 30, 32 px squares, scrolls), tab buttons, "Buy tab: 3000 gold (n / 3 bought)" (greyed without the gold, in a raid or at the cap; tooltip says why), banked gold, the selected item's actions (take to inventory, store, learn a Skill Gem). Opened by F on a stash chest (`ACrossguardStashChest`) or `OpenStash`. |
| Vendor screen | `UCrossguardVendorWidget` | The inventory grids, a sell box to drop stacks on, banked gold, "Sell all gemstones: n gold", and one row per inventory stack with its price and a Sell button (greyed, "not bought", for what vendors do not take). Opened by F on a vendor NPC (`ACrossguardVendorNPC`) or `OpenVendor`. Selling only: buying from vendors is not ruled. |
| Main menu | `UCrossguardMainMenuWidget` (shown by `ACrossguardMenuHUD` on `L_MainMenu`) | Title screen: Play (host the hub), Join (address field), Settings (mouse sensitivity, field of view, master volume; Save settings), Quit; the reason the player came back ("Your raid is over.") in the status line. Not a window: the menu map has no `ACrossguardHUD`. See [hub-and-menu.md](hub-and-menu.md). |
| Building blocks | `UCrossguardWidgetBase`, `UCrossguardWindowWidget`, `UCrossguardUIButton`, `UCrossguardItemGridWidget`, `UCrossguardItemDragOperation`, `CrossguardUI::*` helpers | Tree built in code, periodic refresh, a full-screen window frame that swallows stray clicks, a click/right-click/hover box that can start a drag and take a drop, the item grid (its cells drag; a drop anywhere on it reports the square the item's top-left lands on), the drag payload (item, source, grabbed square). |

### How to use the inventory

- Drag an item (hold the left button and move): drop it on a square to move it there (the square
  under the mouse is where the grabbed part of the item lands), on a stack of the same kind to
  merge, on an equipment slot to equip it there, or on the open body / chest to put it in; drag a
  stack out of the body / chest onto your grids or an equipment slot to take it. A drag keeps the
  item's rotation. Everything below still works by clicking.
- Click an item to select it (yellow). Then click a square to move it there (Rotate toggles the
  orientation for that move), click an item of the same kind to merge into it, or click an
  equipment slot to equip it there (weapons into that set's hand).
- Right click an item: weapons go in the active set's main hand (shields in the off hand), armor and
  containers are worn, consumables and throwables are taken in hand, Skill Gems are learned
  (between raids only; the server says so otherwise).
- With nothing selected, click a worn item in the equipment list to take it off (a Backpack or
  Quiver must be empty first).
- Closing the inventory closes the open body or chest.

### How to use the stash and the vendor

- Hub only: play a map as the hub (`open /Game/Maps/Dev/L_DevSandbox?Hub` in the console, or a game
  mode with `bIsRaid` off). In a raid, F on a chest or vendor is refused by the server (the HUD says
  "Stash: BetweenRaidsOnly"), `OpenStash` / `OpenVendor` say "between raids only", and an open screen
  closes when a raid begins.
- Placing them: `Tools\editor_setup.bat -SkipBuild -AddHubActors` adds a stash chest and a vendor NPC
  beside `L_DevSandbox`'s player start (a new map from `-CreateMap` gets them too); or drag
  `CrossguardStashChest` and `CrossguardVendorNPC` in from Place Actors and save the map.
- The stash screen closes when you walk more than 1.5 m past the chest's interact range (the vendor
  screen likewise); opened from the console it stays.
- Stash: drag an inventory stack onto a stash square to store it there, a stash stack onto your
  grids to take it (it goes wherever it fits, stacking first: the server has no "take to this
  square"), or a stash stack to another stash square (onto a stack of the same kind merges). Click to
  select, then click a square, works too. Right click: a stash stack comes to your inventory, an
  inventory stack goes to the first free stash square; a Skill Gem in either is learned (between
  raids, ruled). Tabs: click "Tab n"; buy one with "Buy tab".
- Vendor: drag a stack onto the sell box or click its Sell button; the whole stack sells at once
  (assumed until ruled, `design-decisions.md`). "Sell all gemstones" sells every Trinket stack
  (rubies, sapphires, emeralds). No confirmation and no buy-back (placeholder). Stash stacks can be
  sold by the component (`RequestSell(Stash, id)`) but the screen lists the inventory only.
- Refusals (no room, not enough gold, not bought) show at the bottom of the screen and on the HUD;
  a sale shows "Sold for n gold".
- Placeholder (no ruling): the stash chest is a 1 m box, the vendor a 1.8 m cylinder; the screens'
  layout, the close distance and square sizes.

### Attributes (K)

- Plan with - and + (never below 0, never more points than the level has), then:
  - **Apply** when the plan only adds unspent points: free (`RequestAttributeAllocation`, no
    payment).
  - **Respec with token** / **Respec for 1000 gold** when the plan lowers any attribute: that is a
    respec (ruled price: one token or 1000 banked gold, the player chooses). Unassign all, then +,
    is a full respec.
- Greyed buttons are hints; the server checks everything and pays (`ApplyAttributeAllocation`).
  When the allocation changes on the server the plan starts over from it ("Applied.").
- The window opens in a raid too, read-only (all requests greyed): attributes change between raids
  only. Assumed (placeholder): reading them in a raid is fine.
- Console: `Respec <str> <agi> <int> [gold]` asks for that allocation (a respec pays with a token,
  or with gold when `gold` is given); `Respec` alone prints the allocation, unspent points, tokens,
  gold and the price.

### Party (P) and the team list

- Not in a party: invite someone by name (the party is founded with you as leader) or accept an
  invite. Names are matched on this server, any case. The leader also makes someone leader or
  kicks; anyone leaves. Start raid: the leader, or a player alone (docs/parties.md, "Raid launch").
- Invite inbox: one row per inviting party with the time left (counted on this machine from when
  the view arrived: `UCrossguardPartyComponent::GetInviteSecondsLeft`, off by the network delay; the
  server's 180 s decide). An expired row says "expired", Accept greys out; Decline removes it.
- Hub only (hints, the server decides): invite, accept, leave, kick, Start raid. Decline, Decline all
  and Make leader work in a raid too (parties.md).
- "(disconnected)": a member (or, in the team list, a teammate) whose player left mid-raid and
  whose character waits for a reconnect (`ACrossguardPlayerState::IsDisconnected`, set and cleared
  by the game mode). "(not here)": a member with no player state on this server.
- The HUD's widget classes for these: `Attributes Widget Class`, `Party Widget Class`, `Team List
  Widget Class` (empty: the built-in ones); keys `Toggle Attributes Action`, `Toggle Party Action`.
- Placeholder (no ruling): the keys, the layouts, the team list's place, invite by typed name (no
  friends list), "(not here)".

### Contrast

- Item buttons and cells (inventory, stash and loot grids, equipment list, trade lists) use a dark
  tint of the rarity colour as the background and a light tint as the text
  (`CrossguardUI::StyleRarityButton`); rarity text on a window (guild lists, the partner's offer,
  the vendor's sell list) uses the light tint (`RarityTextColor`). The light tint is the rarity
  colour mixed with white until its relative luminance is 0.6 (Uncommon and Mystic are already that
  bright).
- Contrast (WCAG ratio, AA asks 4.5:1): every rarity's text is 8:1 or more on its background, 5:1
  or more hovered, 8:1 or more on a window. Before: Rare blue on the grey button was 1.3:1, Epic
  1.3:1, Common 2.3:1. White on the default grey button is 6.2:1 (4.8:1 hovered: the hover lift is
  now +0.05, was +0.15). `Crossguard.UI.RarityContrast` checks all of it.
- Rarity text colours (sRGB): Common #CBCBCB, Uncommon #1EFF00, Rare #BFCBEF, Epic #D9C2F7,
  Legendary #FFBCA3, Mystic #FFD700; backgrounds #2F2F2F, #214221, #21283B, #30233F, #422A21,
  #423A21. Placeholder look; the item data's rarity colours (`GetRarityColor`) are unchanged.

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
   `Skill Wheel Class`, `Inventory Widget Class`, `Trade Widget Class`, `Guild Widget Class`,
   `Stash Widget Class`, `Vendor Widget Class`), and set
   it as the game mode Blueprint's **HUD Class**.
3. Rebind the UI keys with your own input assets on the HUD (`UI Mapping Context` and the four
   actions); empty uses the built-in defaults above.
4. Keep the rule: buttons call `Request*` functions only.

`bReplaceDebugHud` (on by default) turns off the character's on-screen debug text and its "tap Q
for the next skill" fallback for the HUD's player. Turn it off on the HUD to get the debug text back.

## Not built

- Drag and drop (click-to-select-then-click works), splitting stacks, the
  Skill Library / loadout editor, guild redistribution ("Give" to a member)
  and the guild bank log, rearranging the guild bank, a chat.
- A spectator camera switcher (spectating shows the name only); character select; a "back to menu"
  button in the game (the main menu exists: [hub-and-menu.md](hub-and-menu.md)).
- Gamepad navigation.

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
| `UFUNCTION(Exec)` on the HUD (`OpenStash`, `OpenVendor`) reached from the console (as `AHUD::ShowHUD`) | `Public/UI/CrossguardHUD.h` |
| A drag's source grid read as the operation's outer (`NewObject<UCrossguardItemDragOperation>(this)` in the grid) | `Private/UI/CrossguardStashWidget.cpp` (`IsFromStash`) |
| `UCrossguardUIButton` with no click callback (`MakeButton(..., nullptr)`) as a drop target (the sell box) | `Private/UI/CrossguardVendorWidget.cpp` |
| Binding a widget's `UFUNCTION` to `OnProgressionRejected` / `OnPartyRefused` with `AddUniqueDynamic` from the widget | `Private/UI/CrossguardAttributesWidget.cpp`, `Private/UI/CrossguardPartyWidget.cpp` |
| `UWorld::GetRealTimeSeconds` (double) for the invite countdown | `Private/Game/CrossguardPartyComponent.cpp` |
| Tab may be taken by Slate focus navigation while a text field has focus; I always works | — |
