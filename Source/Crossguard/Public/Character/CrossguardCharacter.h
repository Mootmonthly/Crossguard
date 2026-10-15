#pragma once

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "CrossguardCharacter.generated.h"

class UCrossguardAttributeSet;
class UCrossguardLoadoutComponent;
struct FOnAttributeChangeData;

// Player character. Owns its ability system: a character's stats and skills live and die with it,
// as an extraction match ends with the character extracted or dead.
UCLASS()
class CROSSGUARD_API ACrossguardCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACrossguardCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UCrossguardAbilitySystemComponent* GetCrossguardAbilitySystemComponent() const { return AbilitySystemComponent; }
	UCrossguardLoadoutComponent* GetLoadoutComponent() const { return LoadoutComponent; }

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Placeholder until progression is loaded from the player's save. Replicated so clients can
	// show Skill Capacity.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Crossguard|Progression", meta = (ClampMin = "1"))
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Progression")
	FCrossguardAttributeAllocation AttributeAllocation;

protected:
	// MaxWalkSpeed at 100% MoveSpeed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Movement", meta = (ClampMin = "0"))
	float BaseWalkSpeed = 450.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Abilities")
	TObjectPtr<UCrossguardAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UCrossguardAttributeSet> AttributeSet;

	// Set its SkillCatalog in the character Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Abilities")
	TObjectPtr<UCrossguardLoadoutComponent> LoadoutComponent;

private:
	void InitializeAbilitySystem();
	void OnMoveSpeedChanged(const FOnAttributeChangeData& Data);
	void ApplyMoveSpeed(float MoveSpeed);

	bool bMoveSpeedBound = false;
};
