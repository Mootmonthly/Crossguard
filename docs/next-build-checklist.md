# Next Build Checklist

Everything written since the last editor build, in the order to set up and test it. Tick items off
as you go; anything that fails, paste the error or describe what happened.

## 1. Pull and build

1. Close the editor. In the project folder: `git pull origin foundation/core-rules`.
2. Right-click `Crossguard.uproject` → **Generate Visual Studio project files** (new source files
   were added).
3. Open `Crossguard.sln`, build **Development Editor / Win64**. Paste any errors.

## 2. Data tables

In `Content/Data/` (right-click → **Reimport** for existing ones):

| Table | Source file | Row struct |
| --- | --- | --- |
| `DT_Weapons` (reimport) | `Data/Items/WeaponTable.json` | `CrossguardWeaponRow` |
| `DT_Armor` (reimport) | `Data/Items/ArmorTable.json` | `CrossguardArmorRow` |
| `DT_Items` (new: drag in) | `Data/Items/ItemTable.json` | `CrossguardItemRow` |

On `BP_CrossguardCharacter`, select **InventoryComponent** and set **Item Table** to `DT_Items`.

## 3. Character settings

On `BP_CrossguardCharacter` → **Crossguard | Debug**:

- If you set `Level` on the Blueprint for testing, tick **bUseDebugProgression**; otherwise the
  character starts at level 1 from XP.
- **DebugInventory**: add a few entries to test with, e.g.
  - Category `Ammo`, Item Id `arrow`, Count 45 (expect stacks of 20 + 20 + 5)
  - Category `Consumable`, Item Id `healing_potion`, Count 3
  - Category `Consumable`, Item Id `bandage`, Count 2
  - Category `Currency`, Item Id `gold_coin`, Count 250
  - Category `Trinket`, Item Id `ruby`, Rarity `Rare`
  - Category `SkillGem`, Item Id `heroic_might`
- **DebugMainHand** `bow` to test arrows.
- **DebugReviveDelay** 5 to test revive; **bDebugLootTakesAll** on to test looting without UI.

## 4. Things to test in a raid (game mode `bIsRaid` on, the default)

| Test | Expected |
| --- | --- |
| Add `throwing_knife` and `explosive_bottle` to `DebugInventory`, press their hotbar key, left click | The knife arcs and hits for 15; the bottle explodes for 10 within 3 m, then 3 per second for 5 s (you too, if you stand in it). |
| Press 1 and 2 with weapons in both sets (equip with `RequestEquipItem(..., WeaponSet)`) | The weapon in hand switches; attacks use the new set. |
| Shoot the bow 45 times | Arrows run out and the bow stops firing. |
| Press 3 (debug consumables fill the hotbar in order), then left click | A potion heals 15 HP over 10 s; a bandage heals 10 HP while left click is held for 2 s. |
| Kill the dummy | It ragdolls and leaves a body; you get 5 XP (raid XP, shown by `GetRaidXp`). |
| Put gear on the dummy, kill it, press F at the body | With `bDebugLootTakesAll`, its items move into your inventory. |
| Die with `DebugReviveDelay` set | You ragdoll, a body appears, then you stand up with your gear back. |
| Have someone loot your body before the revive | The looted items stay gone. |
| Place an `ACrossguardExtractionZone`, stand in it 5 s | Output Log: extracted, XP banked, gold banked (250). |
| Set the game mode's `PortalUnlockSeconds` to 0, place an `ACrossguardPointOfInterest` around you, press X | A portal appears (give `BeamMesh` a mesh to see it); 60 s later it opens; step in to extract. Walk 10 m+ away while it channels and it collapses. |
| Press X outside a point of interest, or a second time | Refused (Output Log says why). |
| Place an `ACrossguardDoor` (give it a mesh), press F | It snaps open and closed. |
| Tick the door's Lock → bLocked, press F | Nothing without a lockpick; with the `Ability.Lockpicking` tag it opens after 5 s. |

Passive skills: put `heroic_might` (or a mastery) in `DebugSkills` and check
`showdebug abilitysystem`: Strength +10 (masteries only while their weapon is held).

## 5. Things to test between raids (set the game mode's `bIsRaid` off)

| Test | Expected |
| --- | --- |
| Right-click a Skill Gem (`RequestUseItem`) | The skill joins the Skill Library; the gem is used up. |
| Stash: `RequestStoreAnywhere`, `RequestRetrieve` | Items move between inventory and stash. |
| Vendor: `RequestSell(Inventory, ruby id)` | +150 gold for a Rare ruby. |
| `RequestBuyStashTab` with 3000+ gold | A second tab appears; a fourth purchase is refused. |
| `RequestAttributeAllocation` lowering a stat | Refused without a respec token. |

These are called from Blueprint (e.g. a debug widget or the Level Blueprint) until the UI exists.

## 6. Two players (PIE, 2 clients, Listen Server)

- Both see each other's bodies ragdoll.
- `showdebug abilitysystem` on a client shows the other player's `Equipment.Wielding.*` tags.
- Looting the same body at the same time never duplicates an item.
