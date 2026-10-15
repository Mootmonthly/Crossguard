#include "AbilitySystem/CrossguardAttributeSet.h"

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Crossguard.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UCrossguardAttributeSet::UCrossguardAttributeSet()
{
	// Real values are set by UCrossguardAbilitySystemComponent::InitializeAttributes.
	InitMaxHealth(1.0f);
	InitHealth(1.0f);
	InitMoveSpeed(1.0f);
}

void UCrossguardAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Health is visible to everyone; every other stat only to the owning client.
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, Strength, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, Agility, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, Intelligence, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MaxHealthBonus, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, AdditionalAttackDamage, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, AttackDamageBonus, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, AttackPenetration, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, AttackResistance, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, AdditionalMagicDamage, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MagicDamageBonus, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MagicResistance, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MagicPenetration, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, TrueDamage, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, EmptyArmorSlots, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, AdditionalPhysicalHealing, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, PhysicalHealingBonus, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, AdditionalMagicalHealing, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MagicalHealingBonus, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MoveSpeed, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, ActionSpeed, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, Dexterity, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, ActiveCooldownReduction, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, SpellCooldownReduction, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, ArtCooldownReduction, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MeleeCooldownReduction, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, RangedCooldownReduction, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, MinionCooldownReduction, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, ActiveCastTimeReduction, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, SpellCastTimeReduction, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCrossguardAttributeSet, ArtCastTimeReduction, COND_OwnerOnly, REPNOTIFY_Always);
}

void UCrossguardAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
	else if (Attribute == GetMoveSpeedAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UCrossguardAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);

	// Same clamps for base values written with SetNumericAttributeBase.
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
	else if (Attribute == GetMoveSpeedAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UCrossguardAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	// Keep the Health percentage when MaxHealth changes (e.g. Dragonification +50% Max HP).
	if (Attribute == GetMaxHealthAttribute() && OldValue > 0.0f)
	{
		UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
		if (ASC != nullptr && ASC->IsOwnerActorAuthoritative())
		{
			const float NewHealth = FMath::Clamp(GetHealth() * NewValue / OldValue, 0.0f, NewValue);
			ASC->SetNumericAttributeBase(GetHealthAttribute(), NewHealth);
		}
	}
}

void UCrossguardAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float Damage = GetIncomingDamage();
		SetIncomingDamage(0.0f);
		if (Damage > 0.0f)
		{
			const bool bWasAlive = GetHealth() > 0.0f;
			SetHealth(FMath::Clamp(GetHealth() - Damage, 0.0f, GetMaxHealth()));
			UE_LOG(LogCrossguard, Log, TEXT("%s took %.1f damage from %s (%.1f / %.1f HP)"), *GetNameSafe(GetOwningActor()), Damage,
				*GetNameSafe(Data.EffectSpec.GetContext().GetOriginalInstigator()), GetHealth(), GetMaxHealth());
			if (GetHealth() <= 0.0f)
			{
				// Server. Clients add the same tag in OnRep_Health when the replicated Health reaches 0.
				MarkDead();
				const UWorld* World = GetOwningActor() != nullptr ? GetOwningActor()->GetWorld() : nullptr;
				ACrossguardGameMode* GameMode = World != nullptr ? World->GetAuthGameMode<ACrossguardGameMode>() : nullptr;
				if (bWasAlive && GameMode != nullptr)
				{
					GameMode->HandleKill(GetOwningActor(), Data.EffectSpec.GetContext().GetOriginalInstigator());
				}
			}
		}
	}
	else if (Data.EvaluatedData.Attribute == GetIncomingHealingAttribute())
	{
		const float Healing = GetIncomingHealing();
		SetIncomingHealing(0.0f);
		if (Healing > 0.0f)
		{
			SetHealth(FMath::Clamp(GetHealth() + Healing, 0.0f, GetMaxHealth()));
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
}

void UCrossguardAttributeSet::OnRep_Strength(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, Strength, OldValue);
}

void UCrossguardAttributeSet::OnRep_Agility(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, Agility, OldValue);
}

void UCrossguardAttributeSet::OnRep_Intelligence(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, Intelligence, OldValue);
}

void UCrossguardAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, Health, OldValue);
	if (GetHealth() <= 0.0f)
	{
		MarkDead();
	}
}

void UCrossguardAttributeSet::MarkDead()
{
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	if (ASC != nullptr && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		ASC->AddLooseGameplayTag(CrossguardTags::State_Dead);
	}
}

void UCrossguardAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MaxHealth, OldValue);
}

void UCrossguardAttributeSet::OnRep_MaxHealthBonus(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MaxHealthBonus, OldValue);
}

void UCrossguardAttributeSet::OnRep_AdditionalAttackDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, AdditionalAttackDamage, OldValue);
}

void UCrossguardAttributeSet::OnRep_AttackDamageBonus(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, AttackDamageBonus, OldValue);
}

void UCrossguardAttributeSet::OnRep_AttackPenetration(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, AttackPenetration, OldValue);
}

void UCrossguardAttributeSet::OnRep_AttackResistance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, AttackResistance, OldValue);
}

void UCrossguardAttributeSet::OnRep_AdditionalMagicDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, AdditionalMagicDamage, OldValue);
}

void UCrossguardAttributeSet::OnRep_MagicDamageBonus(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MagicDamageBonus, OldValue);
}

void UCrossguardAttributeSet::OnRep_MagicResistance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MagicResistance, OldValue);
}

void UCrossguardAttributeSet::OnRep_MagicPenetration(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MagicPenetration, OldValue);
}

void UCrossguardAttributeSet::OnRep_TrueDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, TrueDamage, OldValue);
}

void UCrossguardAttributeSet::OnRep_EmptyArmorSlots(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, EmptyArmorSlots, OldValue);
}

void UCrossguardAttributeSet::OnRep_AdditionalPhysicalHealing(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, AdditionalPhysicalHealing, OldValue);
}

void UCrossguardAttributeSet::OnRep_PhysicalHealingBonus(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, PhysicalHealingBonus, OldValue);
}

void UCrossguardAttributeSet::OnRep_AdditionalMagicalHealing(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, AdditionalMagicalHealing, OldValue);
}

void UCrossguardAttributeSet::OnRep_MagicalHealingBonus(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MagicalHealingBonus, OldValue);
}

void UCrossguardAttributeSet::OnRep_MoveSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MoveSpeed, OldValue);
}

void UCrossguardAttributeSet::OnRep_ActionSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, ActionSpeed, OldValue);
}

void UCrossguardAttributeSet::OnRep_Dexterity(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, Dexterity, OldValue);
}

void UCrossguardAttributeSet::OnRep_ActiveCooldownReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, ActiveCooldownReduction, OldValue);
}

void UCrossguardAttributeSet::OnRep_SpellCooldownReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, SpellCooldownReduction, OldValue);
}

void UCrossguardAttributeSet::OnRep_ArtCooldownReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, ArtCooldownReduction, OldValue);
}

void UCrossguardAttributeSet::OnRep_MeleeCooldownReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MeleeCooldownReduction, OldValue);
}

void UCrossguardAttributeSet::OnRep_RangedCooldownReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, RangedCooldownReduction, OldValue);
}

void UCrossguardAttributeSet::OnRep_MinionCooldownReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, MinionCooldownReduction, OldValue);
}

void UCrossguardAttributeSet::OnRep_ActiveCastTimeReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, ActiveCastTimeReduction, OldValue);
}

void UCrossguardAttributeSet::OnRep_SpellCastTimeReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, SpellCastTimeReduction, OldValue);
}

void UCrossguardAttributeSet::OnRep_ArtCastTimeReduction(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCrossguardAttributeSet, ArtCastTimeReduction, OldValue);
}
