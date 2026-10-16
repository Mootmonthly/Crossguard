#include "UI/CrossguardUIHelpers.h"

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Character/CrossguardCharacter.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "Skills/CrossguardSkillCatalog.h"
#include "UI/CrossguardUIButton.h"

namespace CrossguardUI
{
	FString WeaponName(const FCrossguardWeaponItem& Weapon, const UCrossguardEquipmentComponent* Equipment)
	{
		if (Weapon.IsEmpty())
		{
			return FString();
		}
		const UDataTable* Table = Equipment != nullptr ? Equipment->WeaponTable.Get() : nullptr;
		const FCrossguardWeaponRow* Row = Table != nullptr ? Table->FindRow<FCrossguardWeaponRow>(Weapon.WeaponId, TEXT("UI"), false) : nullptr;
		return Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName.ToString() : Weapon.WeaponId.ToString();
	}

	FString ArmorName(const FCrossguardArmorItem& Armor, const UCrossguardEquipmentComponent* Equipment)
	{
		if (Armor.IsEmpty())
		{
			return FString();
		}
		const UDataTable* Table = Equipment != nullptr ? Equipment->ArmorTable.Get() : nullptr;
		const FCrossguardArmorRow* Row = Table != nullptr ? Table->FindRow<FCrossguardArmorRow>(Armor.ArmorId, TEXT("UI"), false) : nullptr;
		return Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName.ToString() : Armor.ArmorId.ToString();
	}

	FString ItemName(const FCrossguardInventoryItem& Item, const UCrossguardInventoryComponent* Inventory,
		const UCrossguardEquipmentComponent* Equipment)
	{
		switch (Item.Category)
		{
		case ECrossguardItemCategory::Weapon:
			return WeaponName(Item.Weapon, Equipment);
		case ECrossguardItemCategory::Armor:
			return ArmorName(Item.Armor, Equipment);
		case ECrossguardItemCategory::Container:
			return Item.ContainerKind == ECrossguardInventoryContainer::Quiver ? TEXT("Quiver") : TEXT("Backpack");
		case ECrossguardItemCategory::SkillGem:
			return FString::Printf(TEXT("Gem: %s"), *Item.ItemId.ToString());
		default:
			break;
		}
		const UDataTable* Table = Inventory != nullptr ? Inventory->ItemTable.Get() : nullptr;
		const FCrossguardItemRow* Row = Table != nullptr ? Table->FindRow<FCrossguardItemRow>(Item.ItemId, TEXT("UI"), false) : nullptr;
		return Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName.ToString() : Item.ItemId.ToString();
	}

	FString ShortItemName(const FString& Name)
	{
		// Words: runs of letters and digits ("Gem: fireball", "healing_potion" split too).
		TArray<FString> Words;
		FString Word;
		for (const TCHAR Char : Name)
		{
			if (FChar::IsAlnum(Char))
			{
				Word.AppendChar(Char);
			}
			else if (!Word.IsEmpty())
			{
				Words.Add(Word);
				Word.Reset();
			}
		}
		if (!Word.IsEmpty())
		{
			Words.Add(Word);
		}
		if (Words.Num() > 1)
		{
			TArray<FString> Kept;
			for (const FString& Each : Words)
			{
				if (!Each.Equals(TEXT("of"), ESearchCase::IgnoreCase) && !Each.Equals(TEXT("the"), ESearchCase::IgnoreCase)
					&& !Each.Equals(TEXT("a"), ESearchCase::IgnoreCase) && !Each.Equals(TEXT("an"), ESearchCase::IgnoreCase))
				{
					Kept.Add(Each);
				}
			}
			if (Kept.Num() > 0)
			{
				Words = MoveTemp(Kept);
			}
		}
		if (Words.Num() == 0)
		{
			return TEXT("?");
		}
		if (Words.Num() == 1)
		{
			const FString Short = Words[0].Left(3);
			return Short.Left(1).ToUpper() + Short.Mid(1);
		}
		FString Initials;
		for (int32 Index = 0; Index < Words.Num() && Initials.Len() < 4; ++Index)
		{
			Initials.AppendChar(FChar::ToUpper(Words[Index][0]));
		}
		return Initials;
	}

	ECrossguardRarity ItemRarity(const FCrossguardInventoryItem& Item)
	{
		switch (Item.Category)
		{
		case ECrossguardItemCategory::Weapon:
			return Item.Weapon.Rarity;
		case ECrossguardItemCategory::Armor:
			return Item.Armor.Rarity;
		default:
			return Item.Rarity;
		}
	}

	FString ItemLabel(const FCrossguardInventoryItem& Item, const UCrossguardInventoryComponent* Inventory,
		const UCrossguardEquipmentComponent* Equipment)
	{
		const FString Count = Item.Count > 1 ? FString::Printf(TEXT(" x%d"), Item.Count) : FString();
		return FString::Printf(TEXT("%s%s (%s)"), *ItemName(Item, Inventory, Equipment), *Count, *UIEnumName(ItemRarity(Item)));
	}

	FLinearColor RarityColor(ECrossguardRarity Rarity)
	{
		return UCrossguardItemLibrary::GetRarityColor(Rarity);
	}

	float RelativeLuminance(const FLinearColor& Color)
	{
		// FLinearColor is already linear: no sRGB decoding.
		return 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
	}

	float ContrastRatio(const FLinearColor& First, const FLinearColor& Second)
	{
		const float Lighter = FMath::Max(RelativeLuminance(First), RelativeLuminance(Second));
		const float Darker = FMath::Min(RelativeLuminance(First), RelativeLuminance(Second));
		return (Lighter + 0.05f) / (Darker + 0.05f);
	}

	FLinearColor RarityTextColor(ECrossguardRarity Rarity)
	{
		const FLinearColor Base = RarityColor(Rarity);
		const float Luminance = RelativeLuminance(Base);
		if (Luminance >= MinRarityTextLuminance || Luminance >= 1.0f)
		{
			return FLinearColor(Base.R, Base.G, Base.B, 1.0f);
		}
		// Luminance is linear in the channels, so mixing toward white by Mix lands exactly on the minimum.
		const float Mix = (MinRarityTextLuminance - Luminance) / (1.0f - Luminance);
		return FLinearColor(Base.R + Mix * (1.0f - Base.R), Base.G + Mix * (1.0f - Base.G), Base.B + Mix * (1.0f - Base.B), 1.0f);
	}

	FLinearColor RarityBackgroundColor(ECrossguardRarity Rarity)
	{
		const FLinearColor Base = RarityColor(Rarity);
		// Placeholder look: 1.5 % grey plus 4 % of the rarity colour (Rare: #21283B in sRGB).
		return FLinearColor(0.015f + 0.04f * Base.R, 0.015f + 0.04f * Base.G, 0.015f + 0.04f * Base.B, 0.95f);
	}

	void StyleRarityButton(UCrossguardUIButton* Button, ECrossguardRarity Rarity)
	{
		if (Button != nullptr)
		{
			Button->SetColor(RarityBackgroundColor(Rarity));
			Button->SetLabelColor(RarityTextColor(Rarity));
		}
	}

	FString SkillName(FName SkillId, const UCrossguardLoadoutComponent* Loadout)
	{
		const UCrossguardSkillCatalog* Catalog = Loadout != nullptr ? Loadout->SkillCatalog.Get() : nullptr;
		const FCrossguardSkillRow* Row = Catalog != nullptr ? Catalog->FindSkill(SkillId) : nullptr;
		return Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName.ToString() : SkillId.ToString();
	}

	float SkillCooldownRemaining(const UAbilitySystemComponent* AbilitySystem, FName SkillId)
	{
		if (AbilitySystem == nullptr || SkillId.IsNone() || !AbilitySystem->AbilityActorInfo.IsValid())
		{
			return 0.0f;
		}
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			const UCrossguardGameplayAbility* Ability = Cast<UCrossguardGameplayAbility>(Spec.Ability);
			if (Ability != nullptr && Ability->SkillId == SkillId)
			{
				// The ability's cooldown tags (CooldownTags) against the owner's active effects.
				return FMath::Max(0.0f, Ability->GetCooldownTimeRemaining(AbilitySystem->AbilityActorInfo.Get()));
			}
		}
		return 0.0f;
	}

	ACrossguardCharacter* FindNearestPlayer(const ACrossguardCharacter* From, float MaxDistance)
	{
		if (From == nullptr || From->GetWorld() == nullptr)
		{
			return nullptr;
		}
		ACrossguardCharacter* Nearest = nullptr;
		float NearestDistance = MaxDistance;
		for (TActorIterator<ACrossguardCharacter> It(From->GetWorld()); It; ++It)
		{
			ACrossguardCharacter* Other = *It;
			// Other players: only player characters have a player state on clients.
			if (Other == From || Other->GetPlayerState() == nullptr)
			{
				continue;
			}
			const float Distance = FVector::Dist(From->GetActorLocation(), Other->GetActorLocation());
			if (Distance <= NearestDistance)
			{
				Nearest = Other;
				NearestDistance = Distance;
			}
		}
		return Nearest;
	}

	FString PlayerNameOf(const APawn* Pawn)
	{
		const APlayerState* State = Pawn != nullptr ? Pawn->GetPlayerState() : nullptr;
		return State != nullptr ? State->GetPlayerName() : FString(TEXT("?"));
	}
}
