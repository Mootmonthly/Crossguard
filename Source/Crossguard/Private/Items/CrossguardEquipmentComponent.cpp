#include "Items/CrossguardEquipmentComponent.h"

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemGlobals.h"
#include "Crossguard.h"
#include "CrossguardRules/Inventory.h"
#include "Engine/DataTable.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

// UE and rules enums must stay in the same order.
static_assert(static_cast<int>(ECrossguardWeaponType::Sword) == static_cast<int>(Crossguard::EWeaponType::Sword), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Axe) == static_cast<int>(Crossguard::EWeaponType::Axe), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Dagger) == static_cast<int>(Crossguard::EWeaponType::Dagger), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Spear) == static_cast<int>(Crossguard::EWeaponType::Spear), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Mace) == static_cast<int>(Crossguard::EWeaponType::Mace), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Quarterstaff) == static_cast<int>(Crossguard::EWeaponType::Quarterstaff), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Hammer) == static_cast<int>(Crossguard::EWeaponType::Hammer), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Shield) == static_cast<int>(Crossguard::EWeaponType::Shield), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Bow) == static_cast<int>(Crossguard::EWeaponType::Bow), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Crossbow) == static_cast<int>(Crossguard::EWeaponType::Crossbow), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Sling) == static_cast<int>(Crossguard::EWeaponType::Sling), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Throwing) == static_cast<int>(Crossguard::EWeaponType::Throwing), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::Wand) == static_cast<int>(Crossguard::EWeaponType::Wand), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::MagicStaff) == static_cast<int>(Crossguard::EWeaponType::MagicStaff), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::AnyMelee) == static_cast<int>(Crossguard::EWeaponType::AnyMelee), "Weapon type enums out of sync");
static_assert(static_cast<int>(ECrossguardWeaponType::TwoHandedMelee) == static_cast<int>(Crossguard::EWeaponType::TwoHandedMelee), "Weapon type enums out of sync");

static_assert(static_cast<int>(ECrossguardArmorSlot::Head) == static_cast<int>(Crossguard::EArmorSlot::Head), "Armor slot enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorSlot::Chest) == static_cast<int>(Crossguard::EArmorSlot::Chest), "Armor slot enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorSlot::Hands) == static_cast<int>(Crossguard::EArmorSlot::Hands), "Armor slot enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorSlot::Legs) == static_cast<int>(Crossguard::EArmorSlot::Legs), "Armor slot enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorSlot::Feet) == static_cast<int>(Crossguard::EArmorSlot::Feet), "Armor slot enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorSlot::Back) == static_cast<int>(Crossguard::EArmorSlot::Back), "Armor slot enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorType::Cloth) == static_cast<int>(Crossguard::EArmorType::Cloth), "Armor type enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorType::Leather) == static_cast<int>(Crossguard::EArmorType::Leather), "Armor type enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorType::Plate) == static_cast<int>(Crossguard::EArmorType::Plate), "Armor type enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorRequirement::None) == static_cast<int>(Crossguard::EArmorRequirement::None), "Armor requirement enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorRequirement::Plate) == static_cast<int>(Crossguard::EArmorRequirement::Plate), "Armor requirement enums out of sync");
static_assert(static_cast<int>(ECrossguardArmorRequirement::PlateAllSlots) == static_cast<int>(Crossguard::EArmorRequirement::PlateAllSlots), "Armor requirement enums out of sync");

namespace
{
	Crossguard::EWeaponType ToRulesWeaponType(ECrossguardWeaponType Type)
	{
		return static_cast<Crossguard::EWeaponType>(static_cast<uint8>(Type));
	}

	FGameplayTag WieldingTag(ECrossguardWeaponType Type)
	{
		switch (Type)
		{
		case ECrossguardWeaponType::Sword: return CrossguardTags::Equipment_Wielding_Sword;
		case ECrossguardWeaponType::Axe: return CrossguardTags::Equipment_Wielding_Axe;
		case ECrossguardWeaponType::Dagger: return CrossguardTags::Equipment_Wielding_Dagger;
		case ECrossguardWeaponType::Spear: return CrossguardTags::Equipment_Wielding_Spear;
		case ECrossguardWeaponType::Mace: return CrossguardTags::Equipment_Wielding_Mace;
		case ECrossguardWeaponType::Quarterstaff: return CrossguardTags::Equipment_Wielding_Quarterstaff;
		case ECrossguardWeaponType::Hammer: return CrossguardTags::Equipment_Wielding_Hammer;
		case ECrossguardWeaponType::Shield: return CrossguardTags::Equipment_Wielding_Shield;
		case ECrossguardWeaponType::Bow: return CrossguardTags::Equipment_Wielding_Bow;
		case ECrossguardWeaponType::Crossbow: return CrossguardTags::Equipment_Wielding_Crossbow;
		case ECrossguardWeaponType::Sling: return CrossguardTags::Equipment_Wielding_Sling;
		case ECrossguardWeaponType::Throwing: return CrossguardTags::Equipment_Wielding_Throwing;
		case ECrossguardWeaponType::Wand: return CrossguardTags::Equipment_Wielding_Wand;
		case ECrossguardWeaponType::MagicStaff: return CrossguardTags::Equipment_Wielding_MagicStaff;
		case ECrossguardWeaponType::AnyMelee: return CrossguardTags::Equipment_Wielding_AnyMelee;
		case ECrossguardWeaponType::TwoHandedMelee: return CrossguardTags::Equipment_Wielding_TwoHandedMelee;
		}
		return FGameplayTag();
	}

	ECrossguardEquipError ToEquipError(Crossguard::EEquipError Error)
	{
		switch (Error)
		{
		case Crossguard::EEquipError::None: return ECrossguardEquipError::None;
		case Crossguard::EEquipError::ShieldInMainHand: return ECrossguardEquipError::ShieldInMainHand;
		case Crossguard::EEquipError::TwoHandedInOffHand: return ECrossguardEquipError::TwoHandedInOffHand;
		case Crossguard::EEquipError::TwoHandedWithOffHand: return ECrossguardEquipError::TwoHandedWithOffHand;
		case Crossguard::EEquipError::WildcardType: return ECrossguardEquipError::UnknownWeapon;
		}
		return ECrossguardEquipError::NotReady;
	}
}

UCrossguardEquipmentComponent::UCrossguardEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
	Armor.SetNum(Crossguard::ArmorSlotCount);
}

void UCrossguardEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everyone sees what you are holding.
	DOREPLIFETIME(UCrossguardEquipmentComponent, MainHand);
	DOREPLIFETIME(UCrossguardEquipmentComponent, OffHand);
	// Owner only: what is stowed is not visible.
	DOREPLIFETIME_CONDITION(UCrossguardEquipmentComponent, StowedMainHand, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardEquipmentComponent, StowedOffHand, COND_OwnerOnly);
	DOREPLIFETIME(UCrossguardEquipmentComponent, ActiveWeaponSet);
	DOREPLIFETIME(UCrossguardEquipmentComponent, bWeaponsDrawn);
	DOREPLIFETIME(UCrossguardEquipmentComponent, bDrawingWeapons);
	DOREPLIFETIME(UCrossguardEquipmentComponent, bHoldingThrowable);
	DOREPLIFETIME(UCrossguardEquipmentComponent, HeldThrowableType);
	DOREPLIFETIME(UCrossguardEquipmentComponent, Armor);
}

UCrossguardAbilitySystemComponent* UCrossguardEquipmentComponent::GetAbilitySystem() const
{
	return Cast<UCrossguardAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()));
}

const FCrossguardWeaponRow* UCrossguardEquipmentComponent::FindWeapon(FName WeaponId) const
{
	if (WeaponTable == nullptr || WeaponId.IsNone())
	{
		return nullptr;
	}
	return WeaponTable->FindRow<FCrossguardWeaponRow>(WeaponId, TEXT("UCrossguardEquipmentComponent::FindWeapon"), false);
}

const FCrossguardArmorRow* UCrossguardEquipmentComponent::FindArmor(FName ArmorId) const
{
	if (ArmorTable == nullptr || ArmorId.IsNone())
	{
		return nullptr;
	}
	return ArmorTable->FindRow<FCrossguardArmorRow>(ArmorId, TEXT("UCrossguardEquipmentComponent::FindArmor"), false);
}

const FCrossguardArmorItem& UCrossguardEquipmentComponent::GetArmorItem(ECrossguardArmorSlot Slot) const
{
	static const FCrossguardArmorItem Empty;
	const int32 Index = static_cast<int32>(Slot);
	return Armor.IsValidIndex(Index) ? Armor[Index] : Empty;
}

const FCrossguardArmorRow* UCrossguardEquipmentComponent::GetArmorRow(ECrossguardArmorSlot Slot) const
{
	return FindArmor(GetArmorItem(Slot).ArmorId);
}

Crossguard::FArmorLoadout UCrossguardEquipmentComponent::BuildArmorLoadout() const
{
	Crossguard::FArmorLoadout Loadout{};
	for (int32 Index = 0; Index < Crossguard::ArmorSlotCount && Index < Armor.Num(); ++Index)
	{
		if (const FCrossguardArmorRow* Row = FindArmor(Armor[Index].ArmorId))
		{
			Loadout[Index] = static_cast<Crossguard::EArmorType>(static_cast<uint8>(Row->ArmorType));
		}
	}
	return Loadout;
}

bool UCrossguardEquipmentComponent::SatisfiesArmorRequirement(ECrossguardArmorRequirement Requirement) const
{
	return Crossguard::SatisfiesArmorRequirement(static_cast<Crossguard::EArmorRequirement>(static_cast<uint8>(Requirement)), BuildArmorLoadout());
}

int32 UCrossguardEquipmentComponent::GetEmptyArmorSlotCount() const
{
	return Crossguard::CountEmptyArmorSlots(BuildArmorLoadout());
}

const FCrossguardWeaponItem& UCrossguardEquipmentComponent::GetItem(ECrossguardEquipmentSlot Slot) const
{
	return Slot == ECrossguardEquipmentSlot::MainHand ? MainHand : OffHand;
}

const FCrossguardWeaponItem& UCrossguardEquipmentComponent::GetWeaponInSet(int32 Set, ECrossguardEquipmentSlot Slot) const
{
	if (IsActiveSet(Set))
	{
		return GetItem(Slot);
	}
	return Slot == ECrossguardEquipmentSlot::MainHand ? StowedMainHand : StowedOffHand;
}

void UCrossguardEquipmentComponent::RequestSwitchWeaponSet(int32 Set)
{
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		ServerSwitchWeaponSet_Implementation(Set);
	}
	else
	{
		ServerSwitchWeaponSet(Set);
	}
}

void UCrossguardEquipmentComponent::ServerSwitchWeaponSet_Implementation(int32 Set)
{
	const UCrossguardAbilitySystemComponent* ASC = GetAbilitySystem();
	if (Set < 0 || Set >= Crossguard::WeaponSetCount || ASC == nullptr || ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		return;
	}
	// Already in hand, or already being drawn.
	if (Set == ActiveWeaponSet && bWeaponsDrawn)
	{
		return;
	}
	if (Set != ActiveWeaponSet)
	{
		Swap(MainHand, StowedMainHand);
		Swap(OffHand, StowedOffHand);
		ActiveWeaponSet = Set;
	}

	// Drawing puts away whatever item the hands held. The effect is refreshed once, below.
	if (UCrossguardInventoryComponent* Inventory = GetOwner()->FindComponentByClass<UCrossguardInventoryComponent>())
	{
		Inventory->ReleaseHeldItem(false);
	}
	bHoldingThrowable = false;
	bWeaponsDrawn = true;
	bDrawingWeapons = true;
	const float Dexterity = ASC->GetNumericAttribute(UCrossguardAttributeSet::GetDexterityAttribute());
	const float Seconds = static_cast<float>(Crossguard::WeaponDrawSeconds(Crossguard::FItemConfig(), Dexterity));
	GetOwner()->GetWorldTimerManager().SetTimer(DrawTimer, this, &UCrossguardEquipmentComponent::FinishDrawing, FMath::Max(Seconds, 0.01f), false);
	RefreshEquipmentEffect();
	OnEquipmentChanged.Broadcast();
}

void UCrossguardEquipmentComponent::FinishDrawing()
{
	bDrawingWeapons = false;
	// The set is in hand: its stats and tags start counting.
	RefreshEquipmentEffect();
	OnEquipmentChanged.Broadcast();
}

void UCrossguardEquipmentComponent::HolsterWeapons()
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || (!bWeaponsDrawn && !bDrawingWeapons))
	{
		return;
	}
	GetOwner()->GetWorldTimerManager().ClearTimer(DrawTimer);
	bWeaponsDrawn = false;
	bDrawingWeapons = false;
	RefreshEquipmentEffect();
	OnEquipmentChanged.Broadcast();
}

void UCrossguardEquipmentComponent::SetHeldThrowable(bool bHolding, ECrossguardWeaponType Type)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || (bHoldingThrowable == bHolding && HeldThrowableType == Type))
	{
		return;
	}
	bHoldingThrowable = bHolding;
	HeldThrowableType = Type;
	RefreshEquipmentEffect();
	OnEquipmentChanged.Broadcast();
}

FGameplayTag UCrossguardEquipmentComponent::GetWieldingTag(ECrossguardWeaponType Type)
{
	return WieldingTag(Type);
}

const FCrossguardWeaponRow* UCrossguardEquipmentComponent::GetWeaponRow(ECrossguardEquipmentSlot Slot) const
{
	return FindWeapon(GetItem(Slot).WeaponId);
}

Crossguard::FEquipment UCrossguardEquipmentComponent::BuildRulesEquipment(const FCrossguardWeaponItem& Main, const FCrossguardWeaponItem& Off) const
{
	const auto ToRules = [this](const FCrossguardWeaponItem& Item) -> std::optional<Crossguard::FEquippedWeapon>
	{
		const FCrossguardWeaponRow* Row = FindWeapon(Item.WeaponId);
		if (Row == nullptr)
		{
			return std::nullopt;
		}
		Crossguard::FEquippedWeapon Weapon;
		Weapon.Type = ToRulesWeaponType(Row->WeaponType);
		Weapon.Handedness = Row->Handedness == ECrossguardHandedness::TwoHanded ? Crossguard::EHandedness::TwoHanded : Crossguard::EHandedness::OneHanded;
		Weapon.bMelee = Row->IsMelee();
		Weapon.bAttacks = Row->Attacks > 0.0f && Row->Delivery != ECrossguardAttackDelivery::None;
		Weapon.bCanParry = Row->bCanParry;
		return Weapon;
	};

	Crossguard::FEquipment Equipment;
	Equipment.MainHand = ToRules(Main);
	Equipment.OffHand = ToRules(Off);
	return Equipment;
}

bool UCrossguardEquipmentComponent::SatisfiesWeaponRequirement(const TArray<ECrossguardWeaponType>& AnyOf) const
{
	std::vector<Crossguard::EWeaponType> Required;
	Required.reserve(AnyOf.Num());
	for (const ECrossguardWeaponType Type : AnyOf)
	{
		Required.push_back(ToRulesWeaponType(Type));
	}
	// A throwing knife in hand counts as a one-handed melee dagger (also for AnyMelee).
	if (bHoldingThrowable)
	{
		Crossguard::FEquipment Held;
		Held.MainHand = Crossguard::FEquippedWeapon{ToRulesWeaponType(HeldThrowableType), Crossguard::EHandedness::OneHanded, true, true, false};
		if (Crossguard::SatisfiesWeaponRequirement(Required, Held))
		{
			return true;
		}
	}
	// Weapons count only while their set is drawn and in hand.
	return AreWeaponsReady() && Crossguard::SatisfiesWeaponRequirement(Required, BuildRulesEquipment(MainHand, OffHand));
}

ECrossguardSecondaryAction UCrossguardEquipmentComponent::GetSecondaryAction() const
{
	switch (Crossguard::SecondaryAction(BuildRulesEquipment(MainHand, OffHand)))
	{
	case Crossguard::ESecondaryAction::None: return ECrossguardSecondaryAction::None;
	case Crossguard::ESecondaryAction::OffHandAttack: return ECrossguardSecondaryAction::OffHandAttack;
	case Crossguard::ESecondaryAction::Block: return ECrossguardSecondaryAction::Block;
	case Crossguard::ESecondaryAction::Parry: return ECrossguardSecondaryAction::Parry;
	}
	return ECrossguardSecondaryAction::None;
}

float UCrossguardEquipmentComponent::GetAttackInterval(ECrossguardEquipmentSlot Slot) const
{
	const FCrossguardWeaponRow* Row = GetWeaponRow(Slot);
	if (Row == nullptr)
	{
		return 0.0f;
	}
	const UCrossguardAbilitySystemComponent* ASC = GetAbilitySystem();
	const float ActionSpeed = ASC != nullptr ? ASC->GetNumericAttribute(UCrossguardAttributeSet::GetActionSpeedAttribute()) : 0.0f;
	return static_cast<float>(Crossguard::AttackInterval(Crossguard::FItemConfig(), Row->Attacks, Row->PerSeconds, ActionSpeed));
}

ECrossguardEquipError UCrossguardEquipmentComponent::CheckEquipArmor(ECrossguardArmorSlot Slot, const FCrossguardArmorItem& Item) const
{
	const FCrossguardArmorRow* Row = FindArmor(Item.ArmorId);
	if (ArmorTable == nullptr || GetAbilitySystem() == nullptr || !Armor.IsValidIndex(static_cast<int32>(Slot)))
	{
		return ECrossguardEquipError::NotReady;
	}
	if (!Item.IsEmpty() && Row == nullptr)
	{
		return ECrossguardEquipError::UnknownArmor;
	}
	if (Row != nullptr && Row->Slot != Slot)
	{
		return ECrossguardEquipError::WrongArmorSlot;
	}
	return ECrossguardEquipError::None;
}

ECrossguardEquipError UCrossguardEquipmentComponent::EquipArmor(ECrossguardArmorSlot Slot, const FCrossguardArmorItem& Item)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return ECrossguardEquipError::NotReady;
	}
	const ECrossguardEquipError Error = CheckEquipArmor(Slot, Item);
	if (Error != ECrossguardEquipError::None)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Armor equip rejected for %s: slot %d, error %d"), *GetNameSafe(GetOwner()),
			static_cast<int32>(Slot), static_cast<int32>(Error));
		return Error;
	}

	Armor[static_cast<int32>(Slot)] = Item;
	RefreshEquipmentEffect();
	OnEquipmentChanged.Broadcast();
	return ECrossguardEquipError::None;
}

bool UCrossguardEquipmentComponent::GetArmorSlotFor(FName ArmorId, ECrossguardArmorSlot& OutSlot) const
{
	const FCrossguardArmorRow* Row = FindArmor(ArmorId);
	if (Row == nullptr)
	{
		return false;
	}
	OutSlot = Row->Slot;
	return true;
}

void UCrossguardEquipmentComponent::OnRep_Equipment()
{
	OnEquipmentChanged.Broadcast();
}

ECrossguardEquipError UCrossguardEquipmentComponent::CheckEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item, int32 Set) const
{
	if (Set >= Crossguard::WeaponSetCount)
	{
		return ECrossguardEquipError::NotReady;
	}
	if (WeaponTable == nullptr || GetAbilitySystem() == nullptr)
	{
		return ECrossguardEquipError::NotReady;
	}
	if (!Item.IsEmpty() && FindWeapon(Item.WeaponId) == nullptr)
	{
		return ECrossguardEquipError::UnknownWeapon;
	}
	const FCrossguardWeaponItem& NewMain = Slot == ECrossguardEquipmentSlot::MainHand ? Item : GetWeaponInSet(Set, ECrossguardEquipmentSlot::MainHand);
	const FCrossguardWeaponItem& NewOff = Slot == ECrossguardEquipmentSlot::OffHand ? Item : GetWeaponInSet(Set, ECrossguardEquipmentSlot::OffHand);
	return ToEquipError(Crossguard::ValidateEquipment(BuildRulesEquipment(NewMain, NewOff)));
}

ECrossguardEquipError UCrossguardEquipmentComponent::Equip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item, int32 Set)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return ECrossguardEquipError::NotReady;
	}
	if (Set >= Crossguard::WeaponSetCount)
	{
		return ECrossguardEquipError::NotReady;
	}
	const ECrossguardEquipError Error = CheckEquip(Slot, Item, Set);
	if (Error != ECrossguardEquipError::None)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Equip rejected for %s: slot %d, error %d"), *GetNameSafe(GetOwner()),
			static_cast<int32>(Slot), static_cast<int32>(Error));
		return Error;
	}

	if (IsActiveSet(Set))
	{
		(Slot == ECrossguardEquipmentSlot::MainHand ? MainHand : OffHand) = Item;
	}
	else
	{
		(Slot == ECrossguardEquipmentSlot::MainHand ? StowedMainHand : StowedOffHand) = Item;
	}
	RefreshEquipmentEffect();
	OnEquipmentChanged.Broadcast();
	return ECrossguardEquipError::None;
}

void UCrossguardEquipmentComponent::InitializeEquipmentEffect()
{
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		RefreshEquipmentEffect();
	}
}

void UCrossguardEquipmentComponent::RefreshEquipmentEffect()
{
	UCrossguardAbilitySystemComponent* ASC = GetAbilitySystem();
	if (ASC == nullptr)
	{
		return;
	}

	if (EquipmentEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(EquipmentEffectHandle);
		EquipmentEffectHandle.Invalidate();
	}

	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCrossguardEquipmentEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return;
	}

	TMap<FName, float> Stats;
	for (const FName& Name : UCrossguardEquipmentEffect::GetStatNames())
	{
		Stats.Add(Name, 0.0f);
	}

	const auto AddAffixes = [this, &Stats](const TArray<FCrossguardAffix>& Affixes, FName ItemId)
	{
		for (const FCrossguardAffix& Affix : Affixes)
		{
			const FCrossguardAffixRow* AffixRow = AffixTable != nullptr
				? AffixTable->FindRow<FCrossguardAffixRow>(Affix.AffixId, TEXT("RefreshEquipmentEffect"), false)
				: nullptr;
			float* Total = AffixRow != nullptr ? Stats.Find(AffixRow->Stat) : nullptr;
			if (Total == nullptr)
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Affix %s on %s does not map to an equipment stat"), *Affix.AffixId.ToString(),
					*ItemId.ToString());
				continue;
			}
			*Total += Affix.Value;
		}
	};

	// Weapon stats, affixes and Wielding tags count only while the set is drawn and in hand.
	const Crossguard::FEquipment Equipment = AreWeaponsReady() ? BuildRulesEquipment(MainHand, OffHand) : Crossguard::FEquipment();
	for (const FCrossguardWeaponItem* Item : {&MainHand, &OffHand})
	{
		if (!AreWeaponsReady())
		{
			break;
		}
		const FCrossguardWeaponRow* Row = FindWeapon(Item->WeaponId);
		if (Row == nullptr)
		{
			continue;
		}
		if (Row->Stat == ECrossguardWeaponStat::AttackResistance)
		{
			Stats.FindChecked(FName(TEXT("AttackResistance"))) += UCrossguardItemLibrary::GetWeaponValue(*Row, Item->Rarity);
		}
		AddAffixes(Item->Affixes, Item->WeaponId);
		Spec.Data->DynamicGrantedTags.AddTag(WieldingTag(Row->WeaponType));
	}
	if (bHoldingThrowable)
	{
		Spec.Data->DynamicGrantedTags.AddTag(WieldingTag(HeldThrowableType));
	}
	for (const ECrossguardWeaponType Wildcard : {ECrossguardWeaponType::AnyMelee, ECrossguardWeaponType::TwoHandedMelee})
	{
		if (Crossguard::SatisfiesWeaponRequirement({ToRulesWeaponType(Wildcard)}, Equipment))
		{
			Spec.Data->DynamicGrantedTags.AddTag(WieldingTag(Wildcard));
		}
	}

	for (const FCrossguardArmorItem& Piece : Armor)
	{
		const FCrossguardArmorRow* Row = FindArmor(Piece.ArmorId);
		if (Row == nullptr)
		{
			continue;
		}
		for (const FCrossguardArmorStat& Stat : Row->Stats)
		{
			if (float* Total = Stats.Find(Stat.Stat))
			{
				*Total += UCrossguardItemLibrary::GetArmorStatValue(Stat, Piece.Rarity);
			}
			else
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Armor stat %s on %s is not an equipment stat"), *Stat.Stat.ToString(),
					*Piece.ArmorId.ToString());
			}
		}
		AddAffixes(Piece.Affixes, Piece.ArmorId);
	}
	const Crossguard::FArmorLoadout Loadout = BuildArmorLoadout();
	Stats.FindChecked(FName(TEXT("EmptyArmorSlots"))) = static_cast<float>(Crossguard::CountEmptyArmorSlots(Loadout));
	if (Crossguard::SatisfiesArmorRequirement(Crossguard::EArmorRequirement::Plate, Loadout))
	{
		Spec.Data->DynamicGrantedTags.AddTag(CrossguardTags::Equipment_Armor_Plate);
	}
	if (Crossguard::SatisfiesArmorRequirement(Crossguard::EArmorRequirement::PlateAllSlots, Loadout))
	{
		Spec.Data->DynamicGrantedTags.AddTag(CrossguardTags::Equipment_Armor_PlateAllSlots);
	}

	for (const TPair<FName, float>& Stat : Stats)
	{
		Spec.Data->SetSetByCallerMagnitude(Stat.Key, Stat.Value);
	}
	EquipmentEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
}
