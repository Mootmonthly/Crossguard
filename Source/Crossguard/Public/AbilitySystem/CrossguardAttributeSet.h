#pragma once

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "CoreMinimal.h"

#include "CrossguardAttributeSet.generated.h"

#define CROSSGUARD_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

// Every stat from the Stats doc (docs/design-decisions.md). Attribute-derived base values are
// written by UCrossguardAbilitySystemComponent::RecomputeDerivedStats; gameplay effects stack on top.
UCLASS()
class CROSSGUARD_API UCrossguardAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCrossguardAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	// Attributes. Base value is the player's allocation; skills and gear modify it.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Primary", ReplicatedUsing = OnRep_Strength)
	FGameplayAttributeData Strength; // STR
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, Strength)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Primary", ReplicatedUsing = OnRep_Agility)
	FGameplayAttributeData Agility; // AGI
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, Agility)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Primary", ReplicatedUsing = OnRep_Intelligence)
	FGameplayAttributeData Intelligence; // INT
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, Intelligence)

	// Health. MaxHealth base is derived from STR and MaxHealthBonus.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Vitals", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health; // Current HP
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, Health)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Vitals", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth; // MHP
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MaxHealth)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Vitals", ReplicatedUsing = OnRep_MaxHealthBonus)
	FGameplayAttributeData MaxHealthBonus; // MHPB, fraction
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MaxHealthBonus)

	// Attack damage stats. Fractions: 0.15 = 15%.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Attack", ReplicatedUsing = OnRep_AdditionalAttackDamage)
	FGameplayAttributeData AdditionalAttackDamage; // AAD
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, AdditionalAttackDamage)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Attack", ReplicatedUsing = OnRep_AttackDamageBonus)
	FGameplayAttributeData AttackDamageBonus; // ADB
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, AttackDamageBonus)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Attack", ReplicatedUsing = OnRep_AttackPenetration)
	FGameplayAttributeData AttackPenetration; // AP
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, AttackPenetration)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Attack", ReplicatedUsing = OnRep_AttackResistance)
	FGameplayAttributeData AttackResistance; // AR
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, AttackResistance)

	// Magic damage stats.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Magic", ReplicatedUsing = OnRep_AdditionalMagicDamage)
	FGameplayAttributeData AdditionalMagicDamage; // AMD
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, AdditionalMagicDamage)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Magic", ReplicatedUsing = OnRep_MagicDamageBonus)
	FGameplayAttributeData MagicDamageBonus; // MDB
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MagicDamageBonus)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Magic", ReplicatedUsing = OnRep_MagicResistance)
	FGameplayAttributeData MagicResistance; // MR
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MagicResistance)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Magic", ReplicatedUsing = OnRep_MagicPenetration)
	FGameplayAttributeData MagicPenetration; // MP: fraction of the target's MR removed
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MagicPenetration)

	// Flat damage added to every damaging hit after resistances (TD).
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Attack", ReplicatedUsing = OnRep_TrueDamage)
	FGameplayAttributeData TrueDamage; // TD
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, TrueDamage)

	// Healing stats.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Healing", ReplicatedUsing = OnRep_AdditionalPhysicalHealing)
	FGameplayAttributeData AdditionalPhysicalHealing; // APH
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, AdditionalPhysicalHealing)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Healing", ReplicatedUsing = OnRep_PhysicalHealingBonus)
	FGameplayAttributeData PhysicalHealingBonus; // PHB
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, PhysicalHealingBonus)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Healing", ReplicatedUsing = OnRep_AdditionalMagicalHealing)
	FGameplayAttributeData AdditionalMagicalHealing; // AMH
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, AdditionalMagicalHealing)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Healing", ReplicatedUsing = OnRep_MagicalHealingBonus)
	FGameplayAttributeData MagicalHealingBonus; // MHB
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MagicalHealingBonus)

	// Movement and handling. MoveSpeed is a multiplier (1.0 = 100%).
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Handling", ReplicatedUsing = OnRep_MoveSpeed)
	FGameplayAttributeData MoveSpeed; // Movement Speed
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MoveSpeed)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Handling", ReplicatedUsing = OnRep_ActionSpeed)
	FGameplayAttributeData ActionSpeed; // AS
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, ActionSpeed)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Handling", ReplicatedUsing = OnRep_Dexterity)
	FGameplayAttributeData Dexterity; // D
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, Dexterity)

	// Cooldown reduction by category and filter. Summed per skill and capped (CdrCap).
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cooldown", ReplicatedUsing = OnRep_ActiveCooldownReduction)
	FGameplayAttributeData ActiveCooldownReduction; // Active CDR (STR)
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, ActiveCooldownReduction)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cooldown", ReplicatedUsing = OnRep_SpellCooldownReduction)
	FGameplayAttributeData SpellCooldownReduction; // Spell CDR (INT)
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, SpellCooldownReduction)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cooldown", ReplicatedUsing = OnRep_ArtCooldownReduction)
	FGameplayAttributeData ArtCooldownReduction; // Art CDR (AGI)
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, ArtCooldownReduction)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cooldown", ReplicatedUsing = OnRep_MeleeCooldownReduction)
	FGameplayAttributeData MeleeCooldownReduction; // Melee skill CDR
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MeleeCooldownReduction)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cooldown", ReplicatedUsing = OnRep_RangedCooldownReduction)
	FGameplayAttributeData RangedCooldownReduction; // Ranged skill CDR
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, RangedCooldownReduction)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cooldown", ReplicatedUsing = OnRep_MinionCooldownReduction)
	FGameplayAttributeData MinionCooldownReduction; // Minion skill CDR
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, MinionCooldownReduction)

	// Cast time reduction by category. Uncapped.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|CastTime", ReplicatedUsing = OnRep_ActiveCastTimeReduction)
	FGameplayAttributeData ActiveCastTimeReduction; // Active (STR)
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, ActiveCastTimeReduction)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|CastTime", ReplicatedUsing = OnRep_SpellCastTimeReduction)
	FGameplayAttributeData SpellCastTimeReduction; // Spell (INT)
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, SpellCastTimeReduction)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|CastTime", ReplicatedUsing = OnRep_ArtCastTimeReduction)
	FGameplayAttributeData ArtCastTimeReduction; // Art (AGI)
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, ArtCastTimeReduction)

	// Empty armor slots (0-6), set by the equipment effect. [Cor Leonis] scales its bonuses with it
	// through attribute-based magnitudes.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Equipment", ReplicatedUsing = OnRep_EmptyArmorSlots)
	FGameplayAttributeData EmptyArmorSlots;
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, EmptyArmorSlots)

	// Attack Damage a barrier still absorbs ([Eldritch Shield]). The skill sets it; the damage
	// execution uses it up before Health. Magic Damage passes through.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_AttackShield)
	FGameplayAttributeData AttackShield;
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, AttackShield)

	// Meta attributes: server-only inputs from the damage and healing executions, applied to
	// Health in PostGameplayEffectExecute. Not replicated.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Meta")
	FGameplayAttributeData IncomingDamage;
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, IncomingDamage)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Meta")
	FGameplayAttributeData IncomingHealing;
	CROSSGUARD_ATTRIBUTE_ACCESSORS(UCrossguardAttributeSet, IncomingHealing)

	// Adds State.Dead once and ragdolls the character. Runs on the server and, through OnRep_Health,
	// on every client.
	void MarkDead();

	// Revive: removes State.Dead and stands the character back up. Runs on the server and, through
	// OnRep_Health when Health rises above 0, on every client.
	void ClearDead();

protected:

	UFUNCTION()
	void OnRep_Strength(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Agility(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Intelligence(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHealthBonus(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AdditionalAttackDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AttackDamageBonus(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AttackPenetration(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AttackResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AdditionalMagicDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MagicDamageBonus(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MagicResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MagicPenetration(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_TrueDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_EmptyArmorSlots(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AttackShield(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AdditionalPhysicalHealing(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_PhysicalHealingBonus(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AdditionalMagicalHealing(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MagicalHealingBonus(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MoveSpeed(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_ActionSpeed(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Dexterity(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_ActiveCooldownReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_SpellCooldownReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_ArtCooldownReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MeleeCooldownReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_RangedCooldownReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MinionCooldownReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_ActiveCastTimeReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_SpellCastTimeReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_ArtCastTimeReduction(const FGameplayAttributeData& OldValue);
};
