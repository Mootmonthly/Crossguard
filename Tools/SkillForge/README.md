# Skill Forge

A private claude.ai page for creating and editing skills:
https://claude.ai/artifact/VfvhBT3pYmxnZJevn1b2dC

`skill-forge.html` is the published page's source. It stores one document per skill in the
artifact's database (`skills/<id>`: `{skill, meta: {updatedAt, syncedAt, deleted}}`) and validates
edits with the same rules as `Tools/validate_skills.py`. Only editors can write.

## Syncing into the repo

Ask Claude to "sync skills". Claude:

1. Exports the `skills` collection to a local folder.
2. Runs `python3 Tools/sync_skill_forge.py <folder>`: applies pending documents (edited, new or
   marked deleted) to `Data/Skills/Skills.json`, validates, and regenerates
   `Data/Skills/SkillTable.json`. Nothing is written if validation fails; the errors come back to
   you.
3. Runs `Tools/run_checks.sh`, commits and pushes.
4. Marks the synced documents (`meta.syncedAt`) and deletes the ones marked for deletion.

After a sync, reimport `DT_Skills` in the editor.
