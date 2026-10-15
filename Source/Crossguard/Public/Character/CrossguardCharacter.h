#pragma once

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardItemTypes.h"

#include "CrossguardCharacter.generated.h"

class ACrossguardLootContainer;
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
	UCrossguardInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	// Left click: basic attack with the main-hand weapon.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Combat")
	bool TryWeaponAttack();

	// Right click: off-hand attack when dual wielding. Block (shield) and parry are not built yet
	// and return false.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Combat")
	bool TrySecondaryAction();

	// E: opens the loot container (a body, later a chest) in front of the character, within its
	// InteractRange. Runs on the server; clients forward it.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Interaction")
	void TryInteract();

	// Server. Called when Health reaches 0: everything the character carries and wears moves into a
	// lootable body at its feet. Nothing is spawned if it carried nothing.
	void HandleDeath();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	// Adds the mapping context for the local player.
	virtual void PawnClientRestart() override;

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server. Copies level and attribute allocation from the player state and re-initializes the
	// attributes if either changed. Called by the player state; does nothing with
	// bUseDebugProgression or without a player state (AI).
	void ApplyProgression();

	// Copied from the player state for players. Set by hand for AI, or with bUseDebugProgression.
	// Replicated so clients can show Skill Capacity.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Crossguard|Progression", meta = (ClampMin = "1"))
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Progression")
	FCrossguardAttributeAllocation AttributeAllocation;

	// XP its killer gets when this character is not player-controlled (monsters, dummies).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Progression")
	ECrossguardXpSource KillXpSource = ECrossguardXpSource::BasicMonster;

	// Testing: use Level and AttributeAllocation above instead of the player's progression.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	bool bUseDebugProgression = false;

	// Testing only: equipped on the server when the character spawns, without going through the
	// inventory.
	// Item ids are row names in the weapon / armor tables (e.g. "sword_one_handed", "plate_helm").
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	FCrossguardWeaponItem DebugMainHand;

	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	FCrossguardWeaponItem DebugOffHand;

	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	TArray<FCrossguardArmorItem> DebugArmor;

	// Skill ids to equip (subject to Skill Capacity at Level). Players learn them into their Skill
	// Library first, without gems.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	TArray<FName> DebugSkills;

	// Testing only: added to the inventory at spawn (e.g. Category Ammo, ItemId "arrow", Count 40;
	// Category SkillGem, ItemId "fireball").
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	TArray<FCrossguardInventoryItem> DebugInventory;

	// Testing until the inventory UI exists: opening a container takes everything that fits.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	bool bDebugLootTakesAll = false;

	// Replace each debug item's affixes with a random roll for its rarity.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	bool bRollDebugAffixes = false;

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

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> InteractAction;

	// Spawned on death to hold what the character carried.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Inventory")
	TSubclassOf<ACrossguardLootContainer> BodyContainerClass;

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

	// Set its ItemTable in the character Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Inventory")
	TObjectPtr<UCrossguardInventoryComponent> InventoryComponent;

	// Set its SkillCatalog in the character Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Abilities")
	TObjectPtr<UCrossguardLoadoutComponent> LoadoutComponent;

private:
	// Server. Equips the Debug* items and skills.
	void ApplyDebugLoadout();

	// Creates any input asset left empty above, with the default bindings.
	void EnsureDefaultInput();
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnAttack();
	void OnSecondary();

	UFUNCTION(Server, Reliable)
	void ServerInteract();
	// Server. The nearest container within reach that the camera is facing.
	ACrossguardLootContainer* FindInteractTarget() const;

	void InitializeAbilitySystem();
	// Copies level and allocation from the player state. Returns true if either changed.
	bool PullProgression();
	void OnMoveSpeedChanged(const FOnAttributeChangeData& Data);
	void ApplyMoveSpeed(float MoveSpeed);

	bool bMoveSpeedBound = false;
	bool bWeaponAttackGranted = false;
};
