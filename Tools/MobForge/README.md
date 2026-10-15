# Mob Forge

A private claude.ai page for creating monsters and bosses:
https://claude.ai/artifact/JPDt46urNPnzMsgZuD7RYW

`mob-forge.html` is the published page's source. It stores one document per mob in the
artifact's database (`mobs/<id>`: `{mob, meta: {updatedAt, syncedAt, deleted}}`) and validates
edits with the same rules as `Tools/validate_mobs.py`. `refs/catalog` holds the weapon, armor,
item and skill ids (weapons with `weaponType`, `handedness` and `range`; skills with gem rarity,
category, `aiUsable` and `roles`) the drop, weapon and skill editors offer; Claude refreshes it at each sync with `python3 Tools/sync_mob_forge.py --catalog <file>`.
Only editors can write.

The Combat section's **Weapon** picks the mob's `weapon`: none (natural attacks) or a catalog
weapon its combat role can hold, with the same rules as the validator (Warriors no bows, crossbows
or slings; Ranged none, meaning a bow, or a bow, crossbow or sling; no shields). Documents saved before the
field existed load and sync as `weapon: null`.

The Skills section picks the mob's `skills` from the catalog skills with `aiUsable` true (all
but a few, like `flight`) whose `roles` include the mob's combat role: actives it uses on its
target, passives that are always on, bundles that grant their skills (see
[AI-usable skills](../../docs/mob-data-schema.md#ai-usable-skills)). Documents saved before the
field existed load and sync as `skills: []`.

## Syncing into the repo

Ask Claude to "sync mobs". Claude:

1. Exports the `mobs` collection to a local folder.
2. Runs `python3 Tools/sync_mob_forge.py <folder>`: applies pending documents (edited, new or
   marked deleted) to `Data/Mobs/Mobs.json`, validates, and regenerates `Data/Mobs/MobTable.json`
   and `Data/Loot/LootTable.json`. Nothing is written if validation fails; the errors come back to
   you.
3. Runs `Tools/run_checks.sh`, commits and pushes.
4. Marks the synced documents (`meta.syncedAt`), deletes the ones marked for deletion, and
   refreshes `refs/catalog` from `Tools/sync_mob_forge.py --catalog`.

After a sync, reimport `DT_Mobs` and `DT_Loot` in the editor.
