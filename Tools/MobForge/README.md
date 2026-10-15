# Mob Forge

A private claude.ai page for creating monsters and bosses:
https://claude.ai/artifact/JPDt46urNPnzMsgZuD7RYW

`mob-forge.html` is the published page's source. It stores one document per mob in the
artifact's database (`mobs/<id>`: `{mob, meta: {updatedAt, syncedAt, deleted}}`) and validates
edits with the same rules as `Tools/validate_mobs.py`. `refs/catalog` holds the weapon, armor,
item and skill ids (with gem rarities) the drop editor offers; Claude refreshes it at each sync.
Only editors can write.

## Syncing into the repo

Ask Claude to "sync mobs". Claude:

1. Exports the `mobs` collection to a local folder.
2. Runs `python3 Tools/sync_mob_forge.py <folder>`: applies pending documents (edited, new or
   marked deleted) to `Data/Mobs/Mobs.json`, validates, and regenerates `Data/Mobs/MobTable.json`
   and `Data/Loot/LootTable.json`. Nothing is written if validation fails; the errors come back to
   you.
3. Runs `Tools/run_checks.sh`, commits and pushes.
4. Marks the synced documents (`meta.syncedAt`), deletes the ones marked for deletion, and
   refreshes `refs/catalog`.

After a sync, reimport `DT_Mobs` and `DT_Loot` in the editor.
