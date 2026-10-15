# Mob Data Schema

`Data/Mobs/Mobs.json` is the source of truth for monster and boss types. Edit it through the
[Mob Forge](../Tools/MobForge/README.md). `Tools/validate_mobs.py` checks it; `Tools/export_mob_table.py`
writes `Data/Mobs/MobTable.json` for Unreal (row struct `CrossguardMobRow`), and
`Tools/export_loot_table.py` exports each mob's drops as the loot table `mob_<id>`. All run in
`Tools/run_checks.sh`.

An `ACrossguardMonster` placed in a map with a **Mob Id** takes that mob's stats when the game
starts (the game mode's **Mob Table** must be set).

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | `snake_case`, unique. The monster's Mob Id. |
| `name` | string | Display name. |
| `kind` | enum | `Monster` (killer gets basic monster XP, 5) or `Boss` (boss XP, 50). |
| `combatRole` | enum | `Warrior` (runs up and hits) or `Mage` (keeps its distance, casts Fireball: 50 + 15 splash, as the skill). |
| `health` | number > 0 | Max HP. |
| `attackDamage` | number ≥ 0 | Warrior: Attack Damage per hit, before resistance. |
| `attackInterval` | number > 0 | Warrior: seconds between hits. |
| `attackRange` | number > 0 | Warrior: reach in meters. |
| `fireballInterval` | number > 0 | Mage: seconds between fireballs. |
| `keepDistance` | number ≥ 0 | Mage: meters it stays from its target. |
| `aggroRange` | number ≥ 0 | Meters within which it notices enemies it can see. At most `leashRange`. |
| `leashRange` | number > 0 | Meters from home where it gives up: retargets someone in aggro range, or walks home and fully resets. |
| `moveSpeed` | number ≥ 0 | Meters per second (a player walks 4.5). |
| `scale` | number > 0 | Size; 1 = player size. |
| `loot` | object | Its drops: `rolls`, `nothingWeight`, `rarityWeights`, `entries`, exactly as a table in [loot-data-schema.md](loot-data-schema.md) (no `id`). |
| `description` | string | Free text. |
| `notes` | string[] | Open questions. |
