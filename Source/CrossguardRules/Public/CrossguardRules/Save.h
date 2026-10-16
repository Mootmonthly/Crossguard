// Character saves: the record a backend stores per character, its JSON form, version migration and
// validation on load (docs/persistence.md).
//
// What persists (docs/design-decisions.md, "Saving"): banked XP, attribute allocation, Skill
// Library, equipped skills, respec tokens, banked gold, stash tabs and their contents, and the gear
// carried between raids. Raid XP and anything picked up during a raid never are: a save made during
// a raid records no carried gear, because that gear is at risk until the player extracts.
//
// Loading is two steps. ParseCharacterSave refuses a document that is not a save at all (bad JSON,
// wrong types, a newer version, another player's file, too many entries). ValidateCharacterSave then
// repairs values that a valid document can still hold: it clamps numbers, drops broken entries, moves
// overlapping stash items to free squares and sets aside items and skills the game no longer knows
// (quarantine; they come back on a later load once known). Every repair is reported.

#pragma once

#include "CrossguardRules/Config.h"
#include "CrossguardRules/Inventory.h"
#include "CrossguardRules/Items.h"
#include "CrossguardRules/Json.h"
#include "CrossguardRules/Progression.h"
#include "CrossguardRules/RulesApi.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Crossguard
{
	// Written into every save. Raise it when the format changes, and add a migration step from the
	// old version (FSaveMigrations).
	constexpr int CurrentSaveVersion = 1;

	// Where a carried item is worn.
	enum class ESavedWorn
	{
		None,
		// Weapon in WornSet's main / off hand.
		MainHand,
		OffHand,
		// Armor; the game knows each piece's slot.
		Armor,
		// The equipped Backpack or Quiver (ContainerKind).
		Container,
	};

	struct FSavedItem
	{
		EItemCategory Category = EItemCategory::Other;
		// Weapon id, armor id, item-table id, or for Skill Gems the skill id.
		std::string ItemId;
		ERarity Rarity = ERarity::Common;
		int Count = 1;
		// Weapons and armor.
		std::vector<FRolledAffix> Affixes;
		// Category Container: Backpack or Quiver.
		EContainerKind ContainerKind = EContainerKind::Backpack;
		// Unrotated, as last resolved from the item data. Validation replaces it with the current data.
		FGridSize Size;
		// Stash items: the square in their tab. Carried loose items: the square in Grid.
		FGridPlacement Placement;
		// Carried loose items: which grid (main inventory, Backpack, Quiver).
		EContainerKind Grid = EContainerKind::Main;
		// Carried only.
		ESavedWorn Worn = ESavedWorn::None;
		// MainHand / OffHand: weapon set 0 or 1.
		int WornSet = 0;
	};

	struct FSavedStashTab
	{
		std::vector<FSavedItem> Items;
	};

	struct FCharacterSave
	{
		int Version = CurrentSaveVersion;
		// The backend's key for this character (UCrossguardGuildSubsystem::PlayerIdOf in the game).
		std::string PlayerId;
		// Unix seconds when written. Informational.
		std::int64_t SavedAt = 0;

		// Banked XP; the level follows from it.
		int TotalXp = 0;
		FAttributes Attributes;
		int RespecTokens = 0;
		std::vector<std::string> SkillLibrary;
		// The skill loadout between raids.
		std::vector<std::string> EquippedSkills;

		// Banked gold.
		std::int64_t Gold = 0;
		// Bought tabs; the stash has StartingStashTabs + PurchasedStashTabs tabs.
		int PurchasedStashTabs = 0;
		std::vector<FSavedStashTab> StashTabs;

		// Saved during a raid: Carried is empty (the gear was at risk), and on load the raid counts
		// as not extracted.
		bool bInRaid = false;
		// Gear carried between raids: the inventory, Backpack, Quiver and worn equipment.
		std::vector<FSavedItem> Carried;

		// Set aside on an earlier load (unknown to the game data, or no room). Kept so nothing is
		// lost; validation restores them to the stash once they are known and fit.
		std::vector<FSavedItem> QuarantinedItems;
		std::vector<std::string> QuarantinedSkills;
	};

	// Sizes a document may have before it is refused as corrupt. Far above what a real character
	// reaches, so a save the game wrote always loads.
	struct FSaveLimits
	{
		std::size_t MaxDocumentBytes = 8 * 1024 * 1024;
		int MaxDepth = 16;
		std::size_t MaxIdLength = 128;
		std::size_t MaxSkills = 512;
		std::size_t MaxStashTabs = 16;
		std::size_t MaxItemsPerTab = 300;
		std::size_t MaxCarriedItems = 256;
		std::size_t MaxQuarantinedItems = 2048;
		std::size_t MaxAffixesPerItem = 16;
	};

	// One migration: rewrites a document of FromVersion into FromVersion + 1, in place. False: the
	// document cannot be migrated (refused).
	struct FSaveMigrationStep
	{
		int FromVersion = 0;
		std::function<bool(FJsonNode& Document)> Apply;
	};

	struct FSaveMigrations
	{
		int CurrentVersion = CurrentSaveVersion;
		std::vector<FSaveMigrationStep> Steps;
	};

	// The game's migrations: none yet (version 1 is the first format).
	CROSSGUARDRULES_API const FSaveMigrations& DefaultSaveMigrations();

	enum class ESaveLoadError
	{
		None,
		// Larger than FSaveLimits::MaxDocumentBytes.
		TooLarge,
		InvalidJson,
		// A field is missing, of the wrong type, out of its integer range, or an unknown enum name.
		InvalidField,
		// More entries than FSaveLimits allows.
		TooManyEntries,
		// Newer than the game, or older with no migration path.
		UnsupportedVersion,
		MigrationFailed,
		// The document belongs to another player id.
		WrongPlayer,
	};

	CROSSGUARDRULES_API const char* SaveLoadErrorName(ESaveLoadError Error);

	struct FSaveParseResult
	{
		ESaveLoadError Error = ESaveLoadError::None;
		// What was wrong (field path, parser message).
		std::string Detail;
		FCharacterSave Save;
		// The version the document had before migration.
		int LoadedVersion = 0;
		bool bMigrated = false;

		bool IsValid() const { return Error == ESaveLoadError::None; }
	};

	// Reads a save document. ExpectedPlayerId empty: any player id is accepted. Save.Version is
	// Migrations.CurrentVersion on success.
	CROSSGUARDRULES_API FSaveParseResult ParseCharacterSave(const std::string& Json, const std::string& ExpectedPlayerId = std::string(),
		const FSaveMigrations& Migrations = DefaultSaveMigrations(), const FSaveLimits& Limits = FSaveLimits());

	// The document for a record (always CurrentSaveVersion). Also writes the level from TotalXp, for
	// people and backends reading the file; loading ignores it.
	CROSSGUARDRULES_API std::string WriteCharacterSave(const FCharacterSave& Save, const FProgressionConfig& Progression = FProgressionConfig());

	// What the game knows about an item right now.
	struct FSavedItemInfo
	{
		FGridSize Size;
		int MaxStack = 1;
	};

	struct FSaveValidationRules
	{
		FProgressionConfig Progression;
		FInventoryConfig Inventory;
		// ACrossguardPlayerState keeps gold in an int32.
		std::int64_t MaxGold = 2147483647;
		std::size_t MaxQuarantinedItems = FSaveLimits().MaxQuarantinedItems;
		// False: the game has no such item (quarantined). Null: every item is known, keeps its saved
		// size and any stack size.
		std::function<bool(const FSavedItem& Item, FSavedItemInfo& OutInfo)> LookUpItem;
		// False: the game has no such skill (quarantined). Null: every skill is known.
		std::function<bool(const std::string& SkillId)> IsKnownSkill;
	};

	enum class ESaveIssue
	{
		// TotalXp below 0: set to 0.
		NegativeXp,
		// Negative or more points than the level gives: every point unassigned (placeholder).
		InvalidAllocation,
		NegativeRespecTokens,
		// Below 0 or above MaxGold: clamped.
		GoldOutOfRange,
		// Purchased tabs outside 0..MaxPurchasedStashTabs: clamped.
		StashTabsOutOfRange,
		// Empty or duplicate skill id: dropped.
		InvalidSkill,
		UnknownSkill,
		// An equipped skill not in the library, or equipped twice: dropped.
		LoadoutSkillDropped,
		// Count below 1 or an empty id: dropped.
		InvalidItem,
		// Not in the game data: quarantined.
		UnknownItem,
		// More than the item's stack size: clamped (placeholder).
		StackTooLarge,
		// Empty affix id or a non-finite value: the affix is dropped.
		InvalidAffix,
		// Overlapping, outside its tab, or in a tab beyond the bought ones: moved to a free square.
		ItemMoved,
		// ... and no square was free: quarantined.
		NoRoom,
		// Worn in a slot already taken, the wrong kind of slot, or an invalid weapon set: carried
		// loose instead.
		InvalidWorn,
		// Saved during a raid: carried gear (if any was recorded) is gone, as on a failed extraction.
		UnfinishedRaid,
		// A quarantined item or skill came back.
		Restored,
		// The quarantine is over MaxQuarantinedItems: the oldest entries are dropped.
		QuarantineFull,
	};

	CROSSGUARDRULES_API const char* SaveIssueName(ESaveIssue Issue);

	struct FSaveIssue
	{
		ESaveIssue Kind = ESaveIssue::InvalidItem;
		std::string Detail;
	};

	// Repairs a parsed record in place (see the top of this file). Afterwards: the stash has exactly
	// StartingStashTabs + PurchasedStashTabs tabs with no overlaps, bInRaid is false, and every
	// value is in range.
	CROSSGUARDRULES_API std::vector<FSaveIssue> ValidateCharacterSave(FCharacterSave& Save, const FSaveValidationRules& Rules);

	// Places Item in the first free square of any tab (rotation allowed), setting its placement.
	// False if it fits nowhere or has no size.
	CROSSGUARDRULES_API bool PlaceInStash(std::vector<FSavedStashTab>& Tabs, FSavedItem& Item, const FInventoryConfig& Config);

	// Stable names used in the document ("weapon", "rare", ...).
	CROSSGUARDRULES_API const char* ItemCategoryName(EItemCategory Category);
	CROSSGUARDRULES_API const char* RarityName(ERarity Rarity);
}
