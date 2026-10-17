#include "CrossguardRules/Save.h"

#include "CrossguardRules/Experience.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Crossguard
{
	// File-local helpers in a named namespace: the Unreal module builds as a unity build.
	namespace SaveDetail
	{
		constexpr EItemCategory AllCategories[] = {
			EItemCategory::Weapon, EItemCategory::Armor, EItemCategory::Ammo, EItemCategory::SkillGem,
			EItemCategory::Container, EItemCategory::Material, EItemCategory::Consumable, EItemCategory::Throwable,
			EItemCategory::Currency, EItemCategory::Trinket, EItemCategory::Tool, EItemCategory::Other,
		};

		constexpr ERarity AllRarities[] = {
			ERarity::Common, ERarity::Uncommon, ERarity::Rare, ERarity::Epic, ERarity::Legendary, ERarity::Mystic,
		};

		const char* ContainerName(EContainerKind Kind)
		{
			switch (Kind)
			{
			case EContainerKind::Main:
				return "main";
			case EContainerKind::Backpack:
				return "backpack";
			case EContainerKind::Quiver:
				return "quiver";
			}
			return "main";
		}

		const char* WornName(ESavedWorn Worn)
		{
			switch (Worn)
			{
			case ESavedWorn::None:
				return "";
			case ESavedWorn::MainHand:
				return "mainHand";
			case ESavedWorn::OffHand:
				return "offHand";
			case ESavedWorn::Armor:
				return "armor";
			case ESavedWorn::Container:
				return "container";
			}
			return "";
		}

		bool ParseCategory(const std::string& Name, EItemCategory& Out)
		{
			for (const EItemCategory Category : AllCategories)
			{
				if (Name == ItemCategoryName(Category))
				{
					Out = Category;
					return true;
				}
			}
			return false;
		}

		bool ParseRarity(const std::string& Name, ERarity& Out)
		{
			for (const ERarity Rarity : AllRarities)
			{
				if (Name == RarityName(Rarity))
				{
					Out = Rarity;
					return true;
				}
			}
			return false;
		}

		bool ParseContainer(const std::string& Name, EContainerKind& Out)
		{
			for (const EContainerKind Kind : {EContainerKind::Main, EContainerKind::Backpack, EContainerKind::Quiver})
			{
				if (Name == ContainerName(Kind))
				{
					Out = Kind;
					return true;
				}
			}
			return false;
		}

		bool ParseWorn(const std::string& Name, ESavedWorn& Out)
		{
			for (const ESavedWorn Worn : {ESavedWorn::MainHand, ESavedWorn::OffHand, ESavedWorn::Armor, ESavedWorn::Container})
			{
				if (Name == WornName(Worn))
				{
					Out = Worn;
					return true;
				}
			}
			return false;
		}

		// --- Writing -----------------------------------------------------------------------------

		FJsonNode StringArray(const std::vector<std::string>& Values)
		{
			FJsonNode Array = FJsonNode::MakeArray();
			for (const std::string& Value : Values)
			{
				Array.Push(FJsonNode::MakeString(Value));
			}
			return Array;
		}

		FJsonNode WriteItem(const FSavedItem& Item)
		{
			FJsonNode Node = FJsonNode::MakeObject();
			Node.Set("category", FJsonNode::MakeString(ItemCategoryName(Item.Category)));
			Node.Set("id", FJsonNode::MakeString(Item.ItemId));
			Node.Set("rarity", FJsonNode::MakeString(RarityName(Item.Rarity)));
			Node.Set("count", FJsonNode::MakeInt(Item.Count));
			if (Item.Category == EItemCategory::Container)
			{
				Node.Set("containerKind", FJsonNode::MakeString(ContainerName(Item.ContainerKind)));
			}
			FJsonNode Size = FJsonNode::MakeArray();
			Size.Push(FJsonNode::MakeInt(Item.Size.Width));
			Size.Push(FJsonNode::MakeInt(Item.Size.Height));
			Node.Set("size", std::move(Size));
			Node.Set("x", FJsonNode::MakeInt(Item.Placement.X));
			Node.Set("y", FJsonNode::MakeInt(Item.Placement.Y));
			Node.Set("rotated", FJsonNode::MakeBool(Item.Placement.bRotated));
			if (Item.Grid != EContainerKind::Main)
			{
				Node.Set("grid", FJsonNode::MakeString(ContainerName(Item.Grid)));
			}
			if (Item.Worn != ESavedWorn::None)
			{
				Node.Set("worn", FJsonNode::MakeString(WornName(Item.Worn)));
				if (Item.Worn == ESavedWorn::MainHand || Item.Worn == ESavedWorn::OffHand)
				{
					Node.Set("set", FJsonNode::MakeInt(Item.WornSet));
				}
			}
			if (!Item.Affixes.empty())
			{
				FJsonNode Affixes = FJsonNode::MakeArray();
				for (const FRolledAffix& Affix : Item.Affixes)
				{
					FJsonNode AffixNode = FJsonNode::MakeObject();
					AffixNode.Set("id", FJsonNode::MakeString(Affix.Id));
					AffixNode.Set("value", FJsonNode::MakeReal(Affix.Value));
					Affixes.Push(std::move(AffixNode));
				}
				Node.Set("affixes", std::move(Affixes));
			}
			return Node;
		}

		FJsonNode ItemArray(const std::vector<FSavedItem>& Items)
		{
			FJsonNode Array = FJsonNode::MakeArray();
			for (const FSavedItem& Item : Items)
			{
				Array.Push(WriteItem(Item));
			}
			return Array;
		}

		// --- Reading -----------------------------------------------------------------------------

		// Reads typed fields; the first problem sets Error and Detail and every later read fails.
		class FFieldReader
		{
		public:
			explicit FFieldReader(const FSaveLimits& InLimits) : Limits(InLimits) {}

			ESaveLoadError Error = ESaveLoadError::None;
			std::string Detail;

			bool Ok() const { return Error == ESaveLoadError::None; }

			static std::string Join(const std::string& Path, const char* Key) { return Path.empty() ? std::string(Key) : Path + "." + Key; }

			bool Fail(ESaveLoadError InError, const std::string& Path, const char* What)
			{
				if (Ok())
				{
					Error = InError;
					Detail = Path + ": " + What;
				}
				return false;
			}

			// Null (and an error if bRequired) when absent; an error when present but not of Type.
			const FJsonNode* Get(const FJsonNode& Object, const char* Key, const std::string& Path, FJsonNode::EType Type, bool bRequired)
			{
				if (!Ok())
				{
					return nullptr;
				}
				const FJsonNode* Node = Object.Find(Key);
				const std::string FieldPath = Join(Path, Key);
				if (Node == nullptr)
				{
					if (bRequired)
					{
						Fail(ESaveLoadError::InvalidField, FieldPath, "missing");
					}
					return nullptr;
				}
				const bool bMatches = Type == FJsonNode::EType::Real ? Node->IsNumber() : Node->Type() == Type;
				if (!bMatches)
				{
					Fail(ESaveLoadError::InvalidField, FieldPath, "wrong type");
					return nullptr;
				}
				return Node;
			}

			bool Int64(const FJsonNode& Object, const char* Key, const std::string& Path, std::int64_t& Out, bool bRequired,
				std::int64_t Min = std::numeric_limits<std::int64_t>::min(), std::int64_t Max = std::numeric_limits<std::int64_t>::max())
			{
				const FJsonNode* Node = Get(Object, Key, Path, FJsonNode::EType::Integer, bRequired);
				if (Node == nullptr)
				{
					return Ok();
				}
				if (Node->AsInt() < Min || Node->AsInt() > Max)
				{
					return Fail(ESaveLoadError::InvalidField, Join(Path, Key), "out of range");
				}
				Out = Node->AsInt();
				return true;
			}

			bool Int(const FJsonNode& Object, const char* Key, const std::string& Path, int& Out, bool bRequired)
			{
				std::int64_t Value = Out;
				if (!Int64(Object, Key, Path, Value, bRequired, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()))
				{
					return false;
				}
				Out = static_cast<int>(Value);
				return true;
			}

			bool Bool(const FJsonNode& Object, const char* Key, const std::string& Path, bool& Out, bool bRequired)
			{
				const FJsonNode* Node = Get(Object, Key, Path, FJsonNode::EType::Bool, bRequired);
				if (Node != nullptr)
				{
					Out = Node->AsBool();
				}
				return Ok();
			}

			bool String(const FJsonNode& Object, const char* Key, const std::string& Path, std::string& Out, bool bRequired)
			{
				const FJsonNode* Node = Get(Object, Key, Path, FJsonNode::EType::String, bRequired);
				if (Node == nullptr)
				{
					return Ok();
				}
				if (Node->AsString().size() > Limits.MaxIdLength)
				{
					return Fail(ESaveLoadError::InvalidField, Join(Path, Key), "too long");
				}
				Out = Node->AsString();
				return true;
			}

			// An array of at most MaxEntries; null (and an error if bRequired) when absent.
			const FJsonNode* Array(const FJsonNode& Object, const char* Key, const std::string& Path, std::size_t MaxEntries, bool bRequired)
			{
				const FJsonNode* Node = Get(Object, Key, Path, FJsonNode::EType::Array, bRequired);
				if (Node != nullptr && Node->Items().size() > MaxEntries)
				{
					Fail(ESaveLoadError::TooManyEntries, Join(Path, Key), "too many entries");
					return nullptr;
				}
				return Node;
			}

			bool StringList(const FJsonNode& Object, const char* Key, const std::string& Path, std::vector<std::string>& Out, bool bRequired)
			{
				const FJsonNode* Node = Array(Object, Key, Path, Limits.MaxSkills, bRequired);
				if (Node == nullptr)
				{
					return Ok();
				}
				for (const FJsonNode& Entry : Node->Items())
				{
					if (!Entry.IsString() || Entry.AsString().size() > Limits.MaxIdLength)
					{
						return Fail(ESaveLoadError::InvalidField, Join(Path, Key), "entries must be short strings");
					}
					Out.push_back(Entry.AsString());
				}
				return true;
			}

			bool ReadItem(const FJsonNode& Node, const std::string& Path, FSavedItem& Out)
			{
				if (!Node.IsObject())
				{
					return Fail(ESaveLoadError::InvalidField, Path, "not an object");
				}
				std::string Category;
				std::string Rarity;
				String(Node, "category", Path, Category, true);
				String(Node, "id", Path, Out.ItemId, true);
				String(Node, "rarity", Path, Rarity, true);
				Int(Node, "count", Path, Out.Count, true);
				if (!Ok())
				{
					return false;
				}
				if (!ParseCategory(Category, Out.Category))
				{
					return Fail(ESaveLoadError::InvalidField, Path + ".category", "unknown category");
				}
				if (!ParseRarity(Rarity, Out.Rarity))
				{
					return Fail(ESaveLoadError::InvalidField, Path + ".rarity", "unknown rarity");
				}

				std::string Name;
				if (!String(Node, "containerKind", Path, Name, Out.Category == EItemCategory::Container))
				{
					return false;
				}
				if (Out.Category == EItemCategory::Container
					&& (!ParseContainer(Name, Out.ContainerKind) || Out.ContainerKind == EContainerKind::Main))
				{
					return Fail(ESaveLoadError::InvalidField, Path + ".containerKind", "must be backpack or quiver");
				}

				if (const FJsonNode* Size = Array(Node, "size", Path, 2, false))
				{
					if (Size->Items().size() != 2 || !Size->Items()[0].IsInteger() || !Size->Items()[1].IsInteger()
						|| Size->Items()[0].AsInt() < 0 || Size->Items()[0].AsInt() > 1000 || Size->Items()[1].AsInt() < 0
						|| Size->Items()[1].AsInt() > 1000)
					{
						return Fail(ESaveLoadError::InvalidField, Path + ".size", "must be [width, height]");
					}
					Out.Size = {static_cast<int>(Size->Items()[0].AsInt()), static_cast<int>(Size->Items()[1].AsInt())};
				}
				Int(Node, "x", Path, Out.Placement.X, false);
				Int(Node, "y", Path, Out.Placement.Y, false);
				Bool(Node, "rotated", Path, Out.Placement.bRotated, false);

				Name.clear();
				if (!String(Node, "grid", Path, Name, false))
				{
					return false;
				}
				if (!Name.empty() && !ParseContainer(Name, Out.Grid))
				{
					return Fail(ESaveLoadError::InvalidField, Path + ".grid", "unknown grid");
				}
				Name.clear();
				if (!String(Node, "worn", Path, Name, false))
				{
					return false;
				}
				if (!Name.empty() && !ParseWorn(Name, Out.Worn))
				{
					return Fail(ESaveLoadError::InvalidField, Path + ".worn", "unknown slot");
				}
				Int(Node, "set", Path, Out.WornSet, false);

				if (const FJsonNode* Affixes = Array(Node, "affixes", Path, Limits.MaxAffixesPerItem, false))
				{
					for (std::size_t Index = 0; Index < Affixes->Items().size() && Ok(); ++Index)
					{
						const FJsonNode& AffixNode = Affixes->Items()[Index];
						const std::string AffixPath = Path + ".affixes[" + std::to_string(Index) + "]";
						if (!AffixNode.IsObject())
						{
							return Fail(ESaveLoadError::InvalidField, AffixPath, "not an object");
						}
						FRolledAffix Affix;
						String(AffixNode, "id", AffixPath, Affix.Id, true);
						if (const FJsonNode* Value = Get(AffixNode, "value", AffixPath, FJsonNode::EType::Real, true))
						{
							Affix.Value = Value->AsReal();
						}
						Out.Affixes.push_back(Affix);
					}
				}
				return Ok();
			}

			bool ReadItems(const FJsonNode& Object, const char* Key, const std::string& Path, std::size_t MaxEntries, std::vector<FSavedItem>& Out,
				bool bRequired)
			{
				const FJsonNode* Node = Array(Object, Key, Path, MaxEntries, bRequired);
				if (Node == nullptr)
				{
					return Ok();
				}
				for (std::size_t Index = 0; Index < Node->Items().size() && Ok(); ++Index)
				{
					FSavedItem Item;
					if (ReadItem(Node->Items()[Index], Join(Path, Key) + "[" + std::to_string(Index) + "]", Item))
					{
						Out.push_back(std::move(Item));
					}
				}
				return Ok();
			}

		private:
			const FSaveLimits& Limits;
		};

		void ReadDocument(FFieldReader& Reader, const FJsonNode& Root, const FSaveLimits& Limits, FCharacterSave& Out)
		{
			Reader.String(Root, "playerId", "", Out.PlayerId, true);
			Reader.Int64(Root, "savedAt", "", Out.SavedAt, false);

			if (const FJsonNode* Progression = Reader.Get(Root, "progression", "", FJsonNode::EType::Object, true))
			{
				Reader.Int(*Progression, "totalXp", "progression", Out.TotalXp, true);
				if (const FJsonNode* Attributes = Reader.Get(*Progression, "attributes", "progression", FJsonNode::EType::Object, true))
				{
					Reader.Int(*Attributes, "str", "progression.attributes", Out.Attributes.Str, true);
					Reader.Int(*Attributes, "agi", "progression.attributes", Out.Attributes.Agi, true);
					Reader.Int(*Attributes, "int", "progression.attributes", Out.Attributes.Int, true);
				}
				Reader.Int(*Progression, "respecTokens", "progression", Out.RespecTokens, true);
				Reader.StringList(*Progression, "skillLibrary", "progression", Out.SkillLibrary, true);
				Reader.StringList(*Progression, "equippedSkills", "progression", Out.EquippedSkills, false);
			}

			Reader.Int64(Root, "gold", "", Out.Gold, true);

			if (const FJsonNode* Stash = Reader.Get(Root, "stash", "", FJsonNode::EType::Object, true))
			{
				Reader.Int(*Stash, "purchasedTabs", "stash", Out.PurchasedStashTabs, true);
				if (const FJsonNode* Tabs = Reader.Array(*Stash, "tabs", "stash", Limits.MaxStashTabs, true))
				{
					for (std::size_t Index = 0; Index < Tabs->Items().size() && Reader.Ok(); ++Index)
					{
						const std::string TabPath = "stash.tabs[" + std::to_string(Index) + "]";
						if (!Tabs->Items()[Index].IsObject())
						{
							Reader.Fail(ESaveLoadError::InvalidField, TabPath, "not an object");
							break;
						}
						FSavedStashTab Tab;
						Reader.ReadItems(Tabs->Items()[Index], "items", TabPath, Limits.MaxItemsPerTab, Tab.Items, true);
						Out.StashTabs.push_back(std::move(Tab));
					}
				}
			}

			if (const FJsonNode* Carried = Reader.Get(Root, "carried", "", FJsonNode::EType::Object, true))
			{
				Reader.Bool(*Carried, "inRaid", "carried", Out.bInRaid, true);
				Reader.ReadItems(*Carried, "items", "carried", Limits.MaxCarriedItems, Out.Carried, true);
			}

			if (const FJsonNode* QuarantineNode = Reader.Get(Root, "quarantine", "", FJsonNode::EType::Object, false))
			{
				Reader.ReadItems(*QuarantineNode, "items", "quarantine", Limits.MaxQuarantinedItems, Out.QuarantinedItems, false);
				Reader.StringList(*QuarantineNode, "skills", "quarantine", Out.QuarantinedSkills, false);
			}
		}

		// --- Validation --------------------------------------------------------------------------

		std::string Describe(const FSavedItem& Item)
		{
			return std::string(ItemCategoryName(Item.Category)) + " '" + Item.ItemId + "' (" + RarityName(Item.Rarity) + ", x"
				+ std::to_string(Item.Count) + ")";
		}

		enum class EItemVerdict
		{
			Keep,
			Drop,
			Quarantine,
		};

		// Checks one item on its own and refreshes its size from the game data.
		EItemVerdict CheckItem(FSavedItem& Item, const FSaveValidationRules& Rules, const std::string& Where, std::vector<FSaveIssue>& Issues)
		{
			if (Item.ItemId.empty() || Item.Count < 1)
			{
				Issues.push_back({ESaveIssue::InvalidItem, Where + ": " + Describe(Item) + " dropped"});
				return EItemVerdict::Drop;
			}
			const std::size_t AffixesBefore = Item.Affixes.size();
			Item.Affixes.erase(std::remove_if(Item.Affixes.begin(), Item.Affixes.end(),
								   [](const FRolledAffix& Affix) { return Affix.Id.empty() || !std::isfinite(Affix.Value); }),
				Item.Affixes.end());
			if (Item.Affixes.size() != AffixesBefore)
			{
				Issues.push_back({ESaveIssue::InvalidAffix, Where + ": " + Describe(Item) + " lost " + std::to_string(AffixesBefore - Item.Affixes.size())
						+ " broken affix(es)"});
			}
			if (Rules.LookUpItem)
			{
				FSavedItemInfo Info;
				if (!Rules.LookUpItem(Item, Info))
				{
					Issues.push_back({ESaveIssue::UnknownItem, Where + ": " + Describe(Item) + " is not in the game data; quarantined"});
					return EItemVerdict::Quarantine;
				}
				Item.Size = Info.Size;
				const int MaxStack = std::max(1, Info.MaxStack);
				if (Item.Count > MaxStack)
				{
					Issues.push_back({ESaveIssue::StackTooLarge, Where + ": " + Describe(Item) + " clamped to " + std::to_string(MaxStack)});
					Item.Count = MaxStack;
				}
			}
			return EItemVerdict::Keep;
		}

		FInventoryGrid EmptyTabGrid(const FInventoryConfig& Config)
		{
			return FInventoryGrid(FGridSize{Config.StashTabWidth, Config.StashTabHeight});
		}

		FInventoryGrid BuildTabGrid(const FSavedStashTab& Tab, const FInventoryConfig& Config)
		{
			FInventoryGrid Grid = EmptyTabGrid(Config);
			int Id = 1;
			for (const FSavedItem& Item : Tab.Items)
			{
				Grid.Place(Id++, Item.Size, Item.Placement);
			}
			return Grid;
		}

		void Quarantine(FCharacterSave& Save, FSavedItem Item)
		{
			// Quarantined items keep what they had but sit nowhere.
			Item.Placement = FGridPlacement();
			Item.Grid = EContainerKind::Main;
			Item.Worn = ESavedWorn::None;
			Item.WornSet = 0;
			Save.QuarantinedItems.push_back(std::move(Item));
		}

		void ValidateProgression(FCharacterSave& Save, const FSaveValidationRules& Rules, std::vector<FSaveIssue>& Issues)
		{
			if (Save.TotalXp < 0)
			{
				Issues.push_back({ESaveIssue::NegativeXp, "total XP " + std::to_string(Save.TotalXp) + " set to 0"});
				Save.TotalXp = 0;
			}
			const int Level = LevelForTotalXp(Rules.Progression, Save.TotalXp);
			if (ValidateAllocation(Rules.Progression, Level, Save.Attributes) != EAllocationError::None)
			{
				Issues.push_back({ESaveIssue::InvalidAllocation, "attributes " + std::to_string(Save.Attributes.Str) + "/" + std::to_string(Save.Attributes.Agi)
						+ "/" + std::to_string(Save.Attributes.Int) + " invalid at level " + std::to_string(Level) + "; all points unassigned"});
				Save.Attributes = FAttributes();
			}
			if (Save.RespecTokens < 0)
			{
				Issues.push_back({ESaveIssue::NegativeRespecTokens, "respec tokens " + std::to_string(Save.RespecTokens) + " set to 0"});
				Save.RespecTokens = 0;
			}
			if (Save.Gold < 0 || Save.Gold > Rules.MaxGold)
			{
				const std::int64_t Clamped = std::clamp<std::int64_t>(Save.Gold, 0, Rules.MaxGold);
				Issues.push_back({ESaveIssue::GoldOutOfRange, "gold " + std::to_string(Save.Gold) + " clamped to " + std::to_string(Clamped)});
				Save.Gold = Clamped;
			}
		}

		void ValidateSkills(FCharacterSave& Save, const FSaveValidationRules& Rules, std::vector<FSaveIssue>& Issues)
		{
			std::vector<std::string> Library;
			std::unordered_set<std::string> Seen;
			std::vector<std::string> Quarantined;
			for (const std::string& Skill : Save.SkillLibrary)
			{
				if (Skill.empty() || !Seen.insert(Skill).second)
				{
					Issues.push_back({ESaveIssue::InvalidSkill, "skill library entry '" + Skill + "' dropped (empty or duplicate)"});
				}
				else if (Rules.IsKnownSkill && !Rules.IsKnownSkill(Skill))
				{
					Issues.push_back({ESaveIssue::UnknownSkill, "skill '" + Skill + "' is not in the game data; quarantined"});
					Quarantined.push_back(Skill);
				}
				else
				{
					Library.push_back(Skill);
				}
			}
			// Earlier quarantined skills come back once known. Seen holds the library and what was
			// just quarantined, so nothing is listed twice.
			for (const std::string& Skill : Save.QuarantinedSkills)
			{
				if (Skill.empty() || !Seen.insert(Skill).second)
				{
					continue;
				}
				if (!Rules.IsKnownSkill || Rules.IsKnownSkill(Skill))
				{
					Issues.push_back({ESaveIssue::Restored, "quarantined skill '" + Skill + "' restored to the library"});
					Library.push_back(Skill);
				}
				else
				{
					Quarantined.push_back(Skill);
				}
			}
			Save.SkillLibrary = std::move(Library);
			Save.QuarantinedSkills = std::move(Quarantined);

			std::vector<std::string> Equipped;
			for (const std::string& Skill : Save.EquippedSkills)
			{
				if (std::find(Save.SkillLibrary.begin(), Save.SkillLibrary.end(), Skill) == Save.SkillLibrary.end()
					|| std::find(Equipped.begin(), Equipped.end(), Skill) != Equipped.end())
				{
					Issues.push_back({ESaveIssue::LoadoutSkillDropped, "equipped skill '" + Skill + "' dropped (not learned, or twice)"});
				}
				else
				{
					Equipped.push_back(Skill);
				}
			}
			Save.EquippedSkills = std::move(Equipped);
		}

		void ValidateStash(FCharacterSave& Save, const FSaveValidationRules& Rules, std::vector<FSaveIssue>& Issues)
		{
			const FInventoryConfig& Config = Rules.Inventory;
			if (Save.PurchasedStashTabs < 0 || Save.PurchasedStashTabs > Config.MaxPurchasedStashTabs)
			{
				const int Clamped = std::clamp(Save.PurchasedStashTabs, 0, Config.MaxPurchasedStashTabs);
				Issues.push_back({ESaveIssue::StashTabsOutOfRange, "purchased stash tabs " + std::to_string(Save.PurchasedStashTabs) + " clamped to "
						+ std::to_string(Clamped)});
				Save.PurchasedStashTabs = Clamped;
			}
			const std::size_t TabCount = static_cast<std::size_t>(std::max(1, Config.StartingStashTabs) + Save.PurchasedStashTabs);

			// Keep every item that fits where it was saved; the rest look for a free square after.
			std::vector<FSavedStashTab> Tabs(TabCount);
			std::vector<FSavedItem> Misplaced;
			for (std::size_t TabIndex = 0; TabIndex < Save.StashTabs.size(); ++TabIndex)
			{
				FInventoryGrid Grid = EmptyTabGrid(Config);
				int Id = 1;
				for (FSavedItem& Item : Save.StashTabs[TabIndex].Items)
				{
					const std::string Where = "stash tab " + std::to_string(TabIndex + 1);
					switch (CheckItem(Item, Rules, Where, Issues))
					{
					case EItemVerdict::Drop:
						continue;
					case EItemVerdict::Quarantine:
						Quarantine(Save, std::move(Item));
						continue;
					case EItemVerdict::Keep:
						break;
					}
					if (TabIndex < TabCount && Grid.Place(Id, Item.Size, Item.Placement))
					{
						++Id;
						Tabs[TabIndex].Items.push_back(std::move(Item));
					}
					else
					{
						Misplaced.push_back(std::move(Item));
					}
				}
			}
			for (FSavedItem& Item : Misplaced)
			{
				if (PlaceInStash(Tabs, Item, Config))
				{
					Issues.push_back({ESaveIssue::ItemMoved, Describe(Item) + " overlapped or lay outside its tab; moved"});
				}
				else
				{
					Issues.push_back({ESaveIssue::NoRoom, Describe(Item) + " overlapped and no square was free; quarantined"});
					Quarantine(Save, std::move(Item));
				}
			}
			Save.StashTabs = std::move(Tabs);
		}

		void ValidateCarried(FCharacterSave& Save, const FSaveValidationRules& Rules, std::vector<FSaveIssue>& Issues)
		{
			if (Save.bInRaid)
			{
				Issues.push_back({ESaveIssue::UnfinishedRaid, "saved during a raid: " + std::to_string(Save.Carried.size())
						+ " carried item(s) lost, as on a failed extraction"});
				Save.Carried.clear();
				Save.bInRaid = false;
			}

			std::vector<FSavedItem> Carried;
			// Hands per weapon set, then Backpack and Quiver.
			bool Taken[WeaponSetCount * 2 + 2] = {};
			for (FSavedItem& Item : Save.Carried)
			{
				switch (CheckItem(Item, Rules, "carried", Issues))
				{
				case EItemVerdict::Drop:
					continue;
				case EItemVerdict::Quarantine:
					Quarantine(Save, std::move(Item));
					continue;
				case EItemVerdict::Keep:
					break;
				}

				int Slot = -1;
				bool bValid = true;
				switch (Item.Worn)
				{
				case ESavedWorn::None:
					break;
				case ESavedWorn::MainHand:
				case ESavedWorn::OffHand:
					bValid = Item.Category == EItemCategory::Weapon && Item.WornSet >= 0 && Item.WornSet < WeaponSetCount;
					Slot = Item.WornSet * 2 + (Item.Worn == ESavedWorn::OffHand ? 1 : 0);
					break;
				case ESavedWorn::Armor:
					bValid = Item.Category == EItemCategory::Armor;
					break;
				case ESavedWorn::Container:
					bValid = Item.Category == EItemCategory::Container;
					Slot = WeaponSetCount * 2 + (Item.ContainerKind == EContainerKind::Quiver ? 1 : 0);
					break;
				}
				if (bValid && Slot >= 0)
				{
					bValid = !Taken[Slot];
					if (bValid)
					{
						Taken[Slot] = true;
					}
				}
				if (!bValid)
				{
					Issues.push_back({ESaveIssue::InvalidWorn, Describe(Item) + " cannot be worn there; carried loose"});
					Item.Worn = ESavedWorn::None;
					Item.WornSet = 0;
				}
				// WornSet only means something for worn weapons.
				if (Item.Worn != ESavedWorn::MainHand && Item.Worn != ESavedWorn::OffHand)
				{
					Item.WornSet = 0;
				}
				Carried.push_back(std::move(Item));
			}
			Save.Carried = std::move(Carried);
		}

		void ValidateQuarantine(FCharacterSave& Save, std::size_t ExistingCount, const FSaveValidationRules& Rules, std::vector<FSaveIssue>& Issues)
		{
			// Only entries quarantined on an earlier load get another chance; new ones just failed.
			std::vector<FSavedItem> Kept;
			for (std::size_t Index = 0; Index < Save.QuarantinedItems.size(); ++Index)
			{
				FSavedItem& Item = Save.QuarantinedItems[Index];
				if (Index >= ExistingCount)
				{
					Kept.push_back(std::move(Item));
					continue;
				}
				std::vector<FSaveIssue> Ignored;
				const EItemVerdict Verdict = CheckItem(Item, Rules, "quarantine", Ignored);
				if (Verdict == EItemVerdict::Drop)
				{
					Issues.push_back({ESaveIssue::InvalidItem, "quarantine: " + Describe(Item) + " dropped"});
				}
				else if (Verdict == EItemVerdict::Keep && PlaceInStash(Save.StashTabs, Item, Rules.Inventory))
				{
					Issues.push_back({ESaveIssue::Restored, "quarantined " + Describe(Item) + " restored to the stash"});
				}
				else
				{
					Kept.push_back(std::move(Item));
				}
			}
			if (Kept.size() > Rules.MaxQuarantinedItems)
			{
				const std::size_t Excess = Kept.size() - Rules.MaxQuarantinedItems;
				Issues.push_back({ESaveIssue::QuarantineFull, std::to_string(Excess) + " oldest quarantined item(s) dropped"});
				Kept.erase(Kept.begin(), Kept.begin() + static_cast<std::ptrdiff_t>(Excess));
			}
			Save.QuarantinedItems = std::move(Kept);
		}
	}

	const char* ItemCategoryName(EItemCategory Category)
	{
		switch (Category)
		{
		case EItemCategory::Weapon:
			return "weapon";
		case EItemCategory::Armor:
			return "armor";
		case EItemCategory::Ammo:
			return "ammo";
		case EItemCategory::SkillGem:
			return "skillGem";
		case EItemCategory::Container:
			return "container";
		case EItemCategory::Material:
			return "material";
		case EItemCategory::Consumable:
			return "consumable";
		case EItemCategory::Throwable:
			return "throwable";
		case EItemCategory::Currency:
			return "currency";
		case EItemCategory::Trinket:
			return "trinket";
		case EItemCategory::Tool:
			return "tool";
		case EItemCategory::Other:
			return "other";
		}
		return "other";
	}

	const char* RarityName(ERarity Rarity)
	{
		switch (Rarity)
		{
		case ERarity::Common:
			return "common";
		case ERarity::Uncommon:
			return "uncommon";
		case ERarity::Rare:
			return "rare";
		case ERarity::Epic:
			return "epic";
		case ERarity::Legendary:
			return "legendary";
		case ERarity::Mystic:
			return "mystic";
		}
		return "common";
	}

	const char* SaveLoadErrorName(ESaveLoadError Error)
	{
		switch (Error)
		{
		case ESaveLoadError::None:
			return "None";
		case ESaveLoadError::TooLarge:
			return "TooLarge";
		case ESaveLoadError::InvalidJson:
			return "InvalidJson";
		case ESaveLoadError::InvalidField:
			return "InvalidField";
		case ESaveLoadError::TooManyEntries:
			return "TooManyEntries";
		case ESaveLoadError::UnsupportedVersion:
			return "UnsupportedVersion";
		case ESaveLoadError::MigrationFailed:
			return "MigrationFailed";
		case ESaveLoadError::WrongPlayer:
			return "WrongPlayer";
		}
		return "Unknown";
	}

	const char* SaveIssueName(ESaveIssue Issue)
	{
		switch (Issue)
		{
		case ESaveIssue::NegativeXp:
			return "NegativeXp";
		case ESaveIssue::InvalidAllocation:
			return "InvalidAllocation";
		case ESaveIssue::NegativeRespecTokens:
			return "NegativeRespecTokens";
		case ESaveIssue::GoldOutOfRange:
			return "GoldOutOfRange";
		case ESaveIssue::StashTabsOutOfRange:
			return "StashTabsOutOfRange";
		case ESaveIssue::InvalidSkill:
			return "InvalidSkill";
		case ESaveIssue::UnknownSkill:
			return "UnknownSkill";
		case ESaveIssue::LoadoutSkillDropped:
			return "LoadoutSkillDropped";
		case ESaveIssue::InvalidItem:
			return "InvalidItem";
		case ESaveIssue::UnknownItem:
			return "UnknownItem";
		case ESaveIssue::StackTooLarge:
			return "StackTooLarge";
		case ESaveIssue::InvalidAffix:
			return "InvalidAffix";
		case ESaveIssue::ItemMoved:
			return "ItemMoved";
		case ESaveIssue::NoRoom:
			return "NoRoom";
		case ESaveIssue::InvalidWorn:
			return "InvalidWorn";
		case ESaveIssue::UnfinishedRaid:
			return "UnfinishedRaid";
		case ESaveIssue::Restored:
			return "Restored";
		case ESaveIssue::QuarantineFull:
			return "QuarantineFull";
		}
		return "Unknown";
	}

	const FSaveMigrations& DefaultSaveMigrations()
	{
		static const FSaveMigrations Migrations;
		return Migrations;
	}

	FSaveParseResult ParseCharacterSave(const std::string& Json, const std::string& ExpectedPlayerId, const FSaveMigrations& Migrations,
		const FSaveLimits& Limits)
	{
		FSaveParseResult Result;
		if (Json.size() > Limits.MaxDocumentBytes)
		{
			Result.Error = ESaveLoadError::TooLarge;
			Result.Detail = std::to_string(Json.size()) + " bytes";
			return Result;
		}
		FJsonNode Root;
		FJsonParseError ParseError;
		if (!ParseJson(Json, Root, &ParseError, Limits.MaxDepth))
		{
			Result.Error = ESaveLoadError::InvalidJson;
			Result.Detail = ParseError.Message + " at byte " + std::to_string(ParseError.Offset);
			return Result;
		}
		if (!Root.IsObject())
		{
			Result.Error = ESaveLoadError::InvalidField;
			Result.Detail = "the document is not an object";
			return Result;
		}

		SaveDetail::FFieldReader Reader(Limits);
		std::int64_t Version = 0;
		if (!Reader.Int64(Root, "version", "", Version, true, 0, std::numeric_limits<int>::max()))
		{
			Result.Error = Reader.Error;
			Result.Detail = Reader.Detail;
			return Result;
		}
		Result.LoadedVersion = static_cast<int>(Version);
		if (Version > Migrations.CurrentVersion)
		{
			Result.Error = ESaveLoadError::UnsupportedVersion;
			Result.Detail = "version " + std::to_string(Version) + " is newer than " + std::to_string(Migrations.CurrentVersion);
			return Result;
		}
		while (Version < Migrations.CurrentVersion)
		{
			const auto Step = std::find_if(Migrations.Steps.begin(), Migrations.Steps.end(),
				[Version](const FSaveMigrationStep& Candidate) { return Candidate.FromVersion == Version && Candidate.Apply; });
			if (Step == Migrations.Steps.end())
			{
				Result.Error = ESaveLoadError::UnsupportedVersion;
				Result.Detail = "no migration from version " + std::to_string(Version);
				return Result;
			}
			if (!Step->Apply(Root) || !Root.IsObject())
			{
				Result.Error = ESaveLoadError::MigrationFailed;
				Result.Detail = "migration from version " + std::to_string(Version) + " failed";
				return Result;
			}
			++Version;
			Result.bMigrated = true;
		}
		Root.Set("version", FJsonNode::MakeInt(Version));

		SaveDetail::ReadDocument(Reader, Root, Limits, Result.Save);
		if (!Reader.Ok())
		{
			Result.Error = Reader.Error;
			Result.Detail = Reader.Detail;
			Result.Save = FCharacterSave();
			return Result;
		}
		Result.Save.Version = static_cast<int>(Version);
		if (!ExpectedPlayerId.empty() && Result.Save.PlayerId != ExpectedPlayerId)
		{
			Result.Error = ESaveLoadError::WrongPlayer;
			Result.Detail = "the file is for '" + Result.Save.PlayerId + "'";
			Result.Save = FCharacterSave();
		}
		return Result;
	}

	std::string WriteCharacterSave(const FCharacterSave& Save, const FProgressionConfig& Progression)
	{
		using namespace SaveDetail;

		FJsonNode Root = FJsonNode::MakeObject();
		Root.Set("version", FJsonNode::MakeInt(CurrentSaveVersion));
		Root.Set("playerId", FJsonNode::MakeString(Save.PlayerId));
		Root.Set("savedAt", FJsonNode::MakeInt(Save.SavedAt));

		FJsonNode ProgressionNode = FJsonNode::MakeObject();
		ProgressionNode.Set("totalXp", FJsonNode::MakeInt(Save.TotalXp));
		ProgressionNode.Set("level", FJsonNode::MakeInt(LevelForTotalXp(Progression, Save.TotalXp)));
		FJsonNode Attributes = FJsonNode::MakeObject();
		Attributes.Set("str", FJsonNode::MakeInt(Save.Attributes.Str));
		Attributes.Set("agi", FJsonNode::MakeInt(Save.Attributes.Agi));
		Attributes.Set("int", FJsonNode::MakeInt(Save.Attributes.Int));
		ProgressionNode.Set("attributes", std::move(Attributes));
		ProgressionNode.Set("respecTokens", FJsonNode::MakeInt(Save.RespecTokens));
		ProgressionNode.Set("skillLibrary", StringArray(Save.SkillLibrary));
		ProgressionNode.Set("equippedSkills", StringArray(Save.EquippedSkills));
		Root.Set("progression", std::move(ProgressionNode));

		Root.Set("gold", FJsonNode::MakeInt(Save.Gold));

		FJsonNode Stash = FJsonNode::MakeObject();
		Stash.Set("purchasedTabs", FJsonNode::MakeInt(Save.PurchasedStashTabs));
		FJsonNode Tabs = FJsonNode::MakeArray();
		for (const FSavedStashTab& Tab : Save.StashTabs)
		{
			FJsonNode TabNode = FJsonNode::MakeObject();
			TabNode.Set("items", ItemArray(Tab.Items));
			Tabs.Push(std::move(TabNode));
		}
		Stash.Set("tabs", std::move(Tabs));
		Root.Set("stash", std::move(Stash));

		FJsonNode Carried = FJsonNode::MakeObject();
		Carried.Set("inRaid", FJsonNode::MakeBool(Save.bInRaid));
		Carried.Set("items", ItemArray(Save.Carried));
		Root.Set("carried", std::move(Carried));

		FJsonNode QuarantineNode = FJsonNode::MakeObject();
		QuarantineNode.Set("items", ItemArray(Save.QuarantinedItems));
		QuarantineNode.Set("skills", StringArray(Save.QuarantinedSkills));
		Root.Set("quarantine", std::move(QuarantineNode));

		return WriteJson(Root);
	}

	std::string WriteSavedItem(const FSavedItem& Item)
	{
		// The item itself: where it lies or is worn is the holder's business.
		FSavedItem Own = Item;
		Own.Placement = FGridPlacement();
		Own.Grid = EContainerKind::Main;
		Own.Worn = ESavedWorn::None;
		Own.WornSet = 0;
		return WriteJson(SaveDetail::WriteItem(Own), false);
	}

	bool ReadSavedItem(const std::string& Json, FSavedItem& OutItem, std::string* OutError, const FSaveLimits& Limits)
	{
		const auto Refuse = [OutError](const std::string& Why)
		{
			if (OutError != nullptr)
			{
				*OutError = Why;
			}
			return false;
		};
		if (Json.size() > Limits.MaxDocumentBytes)
		{
			return Refuse("too large");
		}
		FJsonNode Root;
		FJsonParseError ParseError;
		if (!ParseJson(Json, Root, &ParseError, Limits.MaxDepth))
		{
			return Refuse(ParseError.Message + " at byte " + std::to_string(ParseError.Offset));
		}
		SaveDetail::FFieldReader Reader(Limits);
		FSavedItem Item;
		if (!Reader.ReadItem(Root, "item", Item))
		{
			return Refuse(Reader.Detail);
		}
		OutItem = std::move(Item);
		return true;
	}

	bool PlaceInStash(std::vector<FSavedStashTab>& Tabs, FSavedItem& Item, const FInventoryConfig& Config)
	{
		if (Item.Size.Width <= 0 || Item.Size.Height <= 0)
		{
			return false;
		}
		for (FSavedStashTab& Tab : Tabs)
		{
			FGridPlacement Spot;
			if (SaveDetail::BuildTabGrid(Tab, Config).FindFreeSpot(Item.Size, true, Spot))
			{
				Item.Placement = Spot;
				Item.Grid = EContainerKind::Main;
				Item.Worn = ESavedWorn::None;
				Item.WornSet = 0;
				Tab.Items.push_back(Item);
				return true;
			}
		}
		return false;
	}

	std::vector<FSaveIssue> ValidateCharacterSave(FCharacterSave& Save, const FSaveValidationRules& Rules)
	{
		std::vector<FSaveIssue> Issues;
		const std::size_t EarlierQuarantine = Save.QuarantinedItems.size();
		SaveDetail::ValidateProgression(Save, Rules, Issues);
		SaveDetail::ValidateSkills(Save, Rules, Issues);
		SaveDetail::ValidateStash(Save, Rules, Issues);
		SaveDetail::ValidateCarried(Save, Rules, Issues);
		SaveDetail::ValidateQuarantine(Save, EarlierQuarantine, Rules, Issues);
		Save.Version = CurrentSaveVersion;
		return Issues;
	}
}
