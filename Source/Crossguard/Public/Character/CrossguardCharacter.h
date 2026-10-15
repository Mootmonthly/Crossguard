#pragma once

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"

#include "CrossguardCharacter.generated.h"

class UCameraComponent;
class UCrossguardAttributeSet;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
class UCrossguardEquipmentComponent;
class UCrossguardWeaponAttackAbility;
class UCrossguardLoadoutComponent;
struct FOnAttributeChangeData;

// Player character. Owns its ability system: a character's stats and skills live and die with it,
// as an extraction match ends with the character extracted or dead.
UCLASS()
class CROSSGUARD_API ACrossguardCharacter : public ACharacter, public IAbilitySystemInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ACrossguardCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// The player state's team, for AI perception (which asks the pawn, not the player state).
	virtual FGenericTeamId GetGenericTeamId() const override;
	UCrossguardAbilitySystemComponent* GetCrossguardAbilitySystemComponent() const { return AbilitySystemComponent; }
	UCrossguardLoadoutComponent* GetLoadoutComponent() const { return LoadoutComponent; }
	UCrossguardEquipmentComponent* GetEquipmentComponent() const { return EquipmentComponent; }

	// Left click: basic attack with the main-hand weapon.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Combat")
	bool TryWeaponAttack();

	// Right click: off-hand attack when dual wielding. Block (shield) and parry are not built yet
	// and return false.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Combat")
	bool TrySecondaryAction();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	// Adds the mapping context for the local player.
	virtual void PawnClientRestart() override;

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
	// First-person camera at eye height, turned by the mouse. The character faces where it looks.
	// The body mesh is hidden from its owner until first-person arms exist; others still see it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	// Input. Leave empty to use the built-in defaults: WASD move, mouse look, Space jump, left
	// mouse attack, right mouse secondary action. Set these to your own assets to rebind.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> AttackAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> SecondaryActionInput;

	// MaxWalkSpeed at 100% MoveSpeed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Movement", meta = (ClampMin = "0"))
	float BaseWalkSpeed = 450.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Abilities")
	TObjectPtr<UCrossguardAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UCrossguardAttributeSet> AttributeSet;

	// Set its WeaponTable in the character Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Equipment")
	TObjectPtr<UCrossguardEquipmentComponent> EquipmentComponent;

	// Granted on possession.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Combat")
	TSubclassOf<UCrossguardWeaponAttackAbility> WeaponAttackAbilityClass;

	// Granted on possession.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Combat")
	TSubclassOf<UCrossguardWeaponAttackAbility> OffHandAttackAbilityClass;

	// Set its SkillCatalog in the character Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Abilities")
	TObjectPtr<UCrossguardLoadoutComponent> LoadoutComponent;

private:
	// Creates any input asset left empty above, with the default bindings.
	void EnsureDefaultInput();
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnAttack();
	void OnSecondary();

	void InitializeAbilitySystem();
	void OnMoveSpeedChanged(const FOnAttributeChangeData& Data);
	void ApplyMoveSpeed(float MoveSpeed);

	bool bMoveSpeedBound = false;
	bool bWeaponAttackGranted = false;
};
