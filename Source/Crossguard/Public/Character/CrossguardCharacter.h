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

class ACrossguardExtractPortal;
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

	// F: uses the nearest interactable (body, chest, door) in front of the character, within its
	// range. If it is locked, starts lockpicking instead; pressing F again stops. Runs on the
	// server; clients forward it.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Interaction")
	void TryInteract();

	// X: summons an extract portal at the character's feet: once per raid, inside a point of
	// interest, 10 minutes into the raid. Runs on the server; clients forward it.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Extraction")
	void TrySummonPortal();

	// True while picking a lock (for a progress bar). Owner only.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Interaction")
	bool IsLockpicking() const { return bLockpicking; }

	// Item table id of the lockpick item. Characters with the Ability.Lockpicking tag need none.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Interaction")
	FName LockpickItemId = TEXT("lockpick");

	// Server. Called when Health reaches 0: everything the character carries and wears moves into a
	// lootable body where it fell. Always spawned, even when empty: a body is information.
	virtual void HandleDeath();

	// Server. Brings a dead character back with HealthFraction of its Max HP (at least 1 HP). It gets
	// back what is still on its body: worn gear goes back on, the rest into the inventory. Whatever
	// was looted is gone. For revive spells. False if the character is not dead, or the 120 s revive
	// window has run out.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Death")
	bool Revive(float HealthFraction = 0.5f);

	// The revive window ran out: dead for the rest of the raid.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Death")
	bool IsPermanentlyDead() const { return bPermanentlyDead; }

	// Ragdolls the character when dead, stands it back up when revived. Runs on every machine
	// (called by the attribute set when State.Dead is added or removed).
	void SetDeadVisuals(bool bDead);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	// False for summoned minions: killing them gives no XP.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Progression")
	bool bGrantsKillXp = true;

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
	// Category SkillGem, ItemId "fireball"). Consumables and throwables also fill the hotbar.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	TArray<FCrossguardInventoryItem> DebugInventory;

	// Testing until revive spells exist: seconds after death until the character revives itself at
	// half health. 0: never.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug", meta = (ClampMin = "0"))
	float DebugReviveDelay = 0.0f;

	// Testing until the inventory UI exists: opening a container takes everything that fits.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	bool bDebugLootTakesAll = false;

	// Testing until the HUD exists: on-screen text (top left) with Health, shield, weapons, the
	// selected skill and states. A quick tap of Q selects the next skill, since there is no wheel
	// widget yet.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	bool bDebugHud = true;

	// Replace each debug item's affixes with a random roll for its rarity.
	UPROPERTY(EditAnywhere, Category = "Crossguard|Debug")
	bool bRollDebugAffixes = false;

protected:
	// First-person camera at eye height, turned by the mouse. The character faces where it looks.
	// The body mesh is hidden from its owner until first-person arms exist; others still see it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	// Input. Leave empty to use the built-in defaults (docs/design-decisions.md, Controls): WASD
	// move, mouse look, Space jump, left mouse attack or use, right mouse secondary action, 1/2
	// weapon sets, 3-7 hotbar, hold Q skill wheel, E cast, F interact, X portal. Set these to your
	// own assets to rebind.
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

	// Keys 1 and 2.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> WeaponSet1Action;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> WeaponSet2Action;

	// Keys 3 to 7, hotbar slots 0 to 4.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> Hotbar1Action;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> Hotbar2Action;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> Hotbar3Action;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> Hotbar4Action;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> Hotbar5Action;

	// Hold Q: skill wheel.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> SkillWheelAction;

	// E: cast the selected skill.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> CastSkillAction;

	// Left Ctrl: descend while flying ([Flight]). Space rises.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> FlyDownAction;

	// X: summon an extract portal (placeholder key).
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> SummonPortalAction;

	// Spawned by TrySummonPortal. Set a Blueprint subclass with the beam and portal visuals.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Extraction")
	TSubclassOf<ACrossguardExtractPortal> PortalClass;

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
	// Held Space / Left Ctrl while flying.
	void OnFlyUp();
	void OnFlyDown();
	void OnAttack();
	// Left click pressed / released: uses a readied consumable instead of attacking.
	void OnAttackPressed();
	void OnAttackReleased();
	void OnWeaponSet1();
	void OnWeaponSet2();
	void OnHotbar1();
	void OnHotbar2();
	void OnHotbar3();
	void OnHotbar4();
	void OnHotbar5();
	void OnSkillWheelOpened();
	void OnSkillWheelClosed();
	void OnCastSkill();
	void OnSecondary();

	UFUNCTION(Server, Reliable)
	void ServerInteract();

	UFUNCTION(Server, Reliable)
	void ServerSummonPortal();
	// Server. The nearest interactable within its range that the camera is facing.
	AActor* FindInteractTarget() const;
	// Server. Lockpicking: 5 s scaled by Dexterity, cannot fail. Needs Ability.Lockpicking or a
	// lockpick item (one is used up per lock picked).
	void StartLockpicking(AActor* Target);
	void FinishLockpicking();
	void StopLockpicking();

	UPROPERTY(Replicated)
	bool bLockpicking = false;

	TWeakObjectPtr<AActor> LockpickTarget;
	FTimerHandle LockpickTimer;

	void InitializeAbilitySystem();
	// Copies level and allocation from the player state. Returns true if either changed.
	bool PullProgression();
	void OnMoveSpeedChanged(const FOnAttributeChangeData& Data);
	void ApplyMoveSpeed(float MoveSpeed);

	UPROPERTY(Replicated)
	bool bPermanentlyDead = false;

	// Server: runs from death until the revive window closes.
	FTimerHandle ReviveWindowTimer;

	// bDebugHud.
	void DrawDebugHud();
	void ShowDebugMessage(const FString& Message, const FColor& Color) const;
	FTimerHandle DebugHudTimer;
	FName SelectedSkillAtWheelOpen;
	void OnReviveWindowExpired();

	// Server. This character's body while it is dead.
	UPROPERTY()
	TObjectPtr<ACrossguardLootContainer> Body;

	// The mesh's place on the capsule, restored on revive.
	FTransform MeshRelativeTransform;
	FName MeshCollisionProfile;

	// Local: left click is using an item, so it does not attack until released.
	bool bUsingItem = false;

	bool bMoveSpeedBound = false;
	bool bWeaponAttackGranted = false;
};
