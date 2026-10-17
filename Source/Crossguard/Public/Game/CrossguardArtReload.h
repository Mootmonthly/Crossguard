#pragma once

#include "CoreMinimal.h"

class UDataTable;
class UWorld;

// CgReloadArtData, a console command in every build but Shipping (docs/art-placeholder-plan.md,
// "Tuning art without restarting"): re-reads the art fields of Data/Items/Weapons.json,
// Data/Items/Items.json and Data/Mobs/Mobs.json straight from the JSON (no export, no reimport) into the
// loaded data tables, then re-applies them to what is spawned in every game world (Play-In-Editor:
// server and clients). Living monsters take their row's body again (replicated), every character
// rebuilds its held weapons; projectiles take the new look from the next shot. Only art fields change,
// in memory (the table assets are not saved); gameplay fields (a mob's `scale`, damage, ...) need the
// exporter and a reimport. Conversions match Tools/export_*_table.py.
#if !UE_BUILD_SHIPPING
namespace CrossguardArtReload
{
	// JsonText: a source file's text (a JSON array of rows with an "id"). Sets the art fields of Table's
	// rows that are in it, as the exporter writes them (a field left out goes back to its default);
	// other rows and fields are untouched. Returns the rows changed, or -1 when the text is not a JSON
	// array or Table has another row struct.
	CROSSGUARD_API int32 ApplyMobArt(UDataTable* Table, const FString& JsonText);
	CROSSGUARD_API int32 ApplyItemArt(UDataTable* Table, const FString& JsonText);
	CROSSGUARD_API int32 ApplyWeaponArt(UDataTable* Table, const FString& JsonText);

	// Re-applies the tables' art in World: every living monster with a MobId whose row has a mesh (on
	// the server; clients follow by replication) and every character's held weapons (where they
	// render). Returns the actors refreshed.
	CROSSGUARD_API int32 RefreshWorld(UWorld* World);

	// The console command: the tables used by every game world, from the JSON files under the
	// project's Data/ folder, then every game world refreshed. Returns the summary (also logged).
	CROSSGUARD_API FString ReloadAll();
}
#endif
