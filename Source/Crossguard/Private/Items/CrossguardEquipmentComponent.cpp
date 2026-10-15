#include "Items/CrossguardEquipmentComponent.h"

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemGlobals.h"
#include "Crossguard.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"

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
}

void UCrossguardEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everyone sees what you are holding.
	DOREPLIFETIME(UCrossguardEquipmentComponent, MainHand);
	DOREPLIFETIME(UCrossguardEquipmentComponent, OffHand);
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

const FCrossguardWeaponItem& UCrossguardEquipmentComponent::GetItem(ECrossguardEquipmentSlot Slot) const
{
	return Slot == ECrossguardEquipmentSlot::MainHand ? MainHand : OffHand;
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
	return Crossguard::SatisfiesWeaponRequirement(Required, BuildRulesEquipment(MainHand, OffHand));
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

void UCrossguardEquipmentComponent::RequestEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item)
{
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		ApplyEquip(Slot, Item);
	}
	else
	{
		ServerEquip(Slot, Item);
	}
}

void UCrossguardEquipmentComponent::RequestUnequip(ECrossguardEquipmentSlot Slot)
{
	RequestEquip(Slot, FCrossguardWeaponItem());
}

void UCrossguardEquipmentComponent::ServerEquip_Implementation(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item)
{
	ApplyEquip(Slot, Item);
}

void UCrossguardEquipmentComponent::ClientEquipRejected_Implementation(ECrossguardEquipmentSlot Slot, ECrossguardEquipError Error)
{
	OnEquipRejected.Broadcast(Slot, Error);
}

void UCrossguardEquipmentComponent::OnRep_Equipment()
{
	OnEquipmentChanged.Broadcast();
}

void UCrossguardEquipmentComponent::ApplyEquip(ECrossguardEquipmentSlot Slot, const FCrossguardWeaponItem& Item)
{
	ECrossguardEquipError Error = ECrossguardEquipError::None;
	if (WeaponTable == nullptr || GetAbilitySystem() == nullptr)
	{
		Error = ECrossguardEquipError::NotReady;
	}
	else if (!Item.IsEmpty() && FindWeapon(Item.WeaponId) == nullptr)
	{
		Error = ECrossguardEquipError::UnknownWeapon;
	}
	else
	{
		const FCrossguardWeaponItem& NewMain = Slot == ECrossguardEquipmentSlot::MainHand ? Item : MainHand;
		const FCrossguardWeaponItem& NewOff = Slot == ECrossguardEquipmentSlot::OffHand ? Item : OffHand;
		Error = ToEquipError(Crossguard::ValidateEquipment(BuildRulesEquipment(NewMain, NewOff)));
	}

	if (Error != ECrossguardEquipError::None)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Equip rejected for %s: slot %d, error %d"), *GetNameSafe(GetOwner()),
			static_cast<int32>(Slot), static_cast<int32>(Error));
		// Reaches the owning client; on a listen server host it runs locally.
		ClientEquipRejected(Slot, Error);
		return;
	}

	(Slot == ECrossguardEquipmentSlot::MainHand ? MainHand : OffHand) = Item;
	RefreshEquipmentEffect();
	OnEquipmentChanged.Broadcast();
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

	const Crossguard::FEquipment Equipment = BuildRulesEquipment(MainHand, OffHand);
	for (const FCrossguardWeaponItem* Item : {&MainHand, &OffHand})
	{
		const FCrossguardWeaponRow* Row = FindWeapon(Item->WeaponId);
		if (Row == nullptr)
		{
			continue;
		}
		if (Row->Stat == ECrossguardWeaponStat::AttackResistance)
		{
			Stats.FindChecked(FName(TEXT("AttackResistance"))) += UCrossguardItemLibrary::GetWeaponValue(*Row, Item->Rarity);
		}
		for (const FCrossguardAffix& Affix : Item->Affixes)
		{
			const FCrossguardAffixRow* AffixRow = AffixTable != nullptr
				? AffixTable->FindRow<FCrossguardAffixRow>(Affix.AffixId, TEXT("RefreshEquipmentEffect"), false)
				: nullptr;
			float* Total = AffixRow != nullptr ? Stats.Find(AffixRow->Stat) : nullptr;
			if (Total == nullptr)
			{
				UE_LOG(LogCrossguard, Warning, TEXT("Affix %s on %s does not map to an equipment stat"), *Affix.AffixId.ToString(),
					*Item->WeaponId.ToString());
				continue;
			}
			*Total += Affix.Value;
		}
		Spec.Data->DynamicGrantedTags.AddTag(WieldingTag(Row->WeaponType));
	}
	for (const ECrossguardWeaponType Wildcard : {ECrossguardWeaponType::AnyMelee, ECrossguardWeaponType::TwoHandedMelee})
	{
		if (Crossguard::SatisfiesWeaponRequirement({ToRulesWeaponType(Wildcard)}, Equipment))
		{
			Spec.Data->DynamicGrantedTags.AddTag(WieldingTag(Wildcard));
		}
	}

	for (const TPair<FName, float>& Stat : Stats)
	{
		Spec.Data->SetSetByCallerMagnitude(Stat.Key, Stat.Value);
	}
	EquipmentEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
}
