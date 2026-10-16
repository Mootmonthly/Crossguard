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
class UCrossguardTradeComponent;
class UCrossguardGuardAbility;
class UCrossguardTrackingComponent;
struct FOnAttributeChangeData;

class ACrossguardCharacter;

// The clank, natively: the character that played it and the point of impact.
DECLARE_MULTICAST_DELEGATE_TwoParams(FCrossguardWeaponClankSignature, ACrossguardCharacter*, FVector);

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

	// No jumping while stunned.
	virtual bool CanJumpInternal_Implementation() const override;

	// Server: a melee swing starts. For a moment its blade is in the way: a swing from the front
	// meeting it clashes (CrossguardGuard).
	void NoteMeleeSwing(int32 InImpactPower, float InWeaponDamage);

	// Server: the swing still in the way at Now (within Window seconds of its start), if any.
	bool GetActiveSwing(double Now, double Window, int32& OutImpactPower, float& OutWeaponDamage, FVector& OutFacing) const;

	// Weapons met (a block, a parry or a clash): every machine plays the clank.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWeaponClank(FVector ImpactPoint);

	// The clank: hook the bounce-back animation and the sound here (Blueprint).
	UFUNCTION(BlueprintImplementableEvent, Category = "Crossguard|Combat")
	void OnWeaponClank(FVector ImpactPoint);

	// The same clank for C++ listeners (tests), broadcast right after OnWeaponClank on every machine
	// that plays it.
	FCrossguardWeaponClankSignature OnWeaponClankNative;

	// Clanks this character has played on this machine (tests).
	int32 GetWeaponClankCount() const { return WeaponClankCount; }

	UCrossguardAbilitySystemComponent* GetCrossguardAbilitySystemComponent() const { return AbilitySystemComponent; }
	UCrossguardLoadoutComponent* GetLoadoutComponent() const { return LoadoutComponent; }
	UCrossguardEquipmentComponent* GetEquipmentComponent() const { return EquipmentComponent; }
	UCrossguardInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }
	UCrossguardTradeComponent* GetTradeComponent() const { return TradeComponent; }

	// Trading until a trade window exists (console, the ` key; the Output Log shows every change):
	// TradeItems lists your inventory's item ids; TradeNearest asks the nearest player; TradeRespond
	// 1 or 0; TradeOffer "3,7" 0 offers item ids and banked gold; TradeLock 1 or 0; TradeAccept;
	// TradeCancel.
	UFUNCTION(Exec)
	void TradeItems();

	UFUNCTION(Exec)
	void TradeNearest();

	UFUNCTION(Exec)
	void TradeRespond(bool bAccept);

	UFUNCTION(Exec)
	void TradeOffer(const FString& ItemIds, int32 Gold);

	UFUNCTION(Exec)
	void TradeLock(bool bLocked);

	UFUNCTION(Exec)
	void TradeAccept();

	UFUNCTION(Exec)
	void TradeCancel();

	// Guilds until a guild window exists (console; Output Log): GuildCreate "Name"; GuildInviteNearest;
	// GuildAccept (your first invite); GuildLeave; GuildInfo (roster and bank with ids); GuildRank
	// <player id> Member|Officer|Leader; GuildKick <player id>; GuildGold <amount> (negative takes);
	// GuildDeposit <inventory item id>; GuildWithdraw <bank slot id>; GuildGive <player id> <slot id>.
	UFUNCTION(Exec)
	void GuildCreate(const FString& Name);

	UFUNCTION(Exec)
	void GuildInviteNearest();

	UFUNCTION(Exec)
	void GuildAccept();

	UFUNCTION(Exec)
	void GuildLeave();

	UFUNCTION(Exec)
	void GuildInfo();

	UFUNCTION(Exec)
	void GuildRank(const FString& PlayerId, const FString& NewRank);

	UFUNCTION(Exec)
	void GuildKick(const FString& PlayerId);

	UFUNCTION(Exec)
	void GuildGold(int32 Amount);

	UFUNCTION(Exec)
	void GuildDeposit(int32 InventoryItemId);

	UFUNCTION(Exec)
	void GuildWithdraw(int32 SlotId);

	UFUNCTION(Exec)
	void GuildGive(const FString& PlayerId, int32 SlotId);

	// Parties until a party panel exists (console; Output Log; docs/parties.md): PartyInvite <player
	// name>; PartyAccept (your first invite); PartyDecline; PartyLeave; PartyKick <name or id>;
	// PartyPromote <name or id>; PartyInfo; PartyStartRaid (leader, hub only).
	UFUNCTION(Exec)
	void PartyInvite(const FString& PlayerName);

	UFUNCTION(Exec)
	void PartyAccept();

	UFUNCTION(Exec)
	void PartyDecline();

	UFUNCTION(Exec)
	void PartyLeave();

	UFUNCTION(Exec)
	void PartyKick(const FString& Member);

	UFUNCTION(Exec)
	void PartyPromote(const FString& Member);

	UFUNCTION(Exec)
	void PartyInfo();

	UFUNCTION(Exec)
	void PartyStartRaid();

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

	// Server. Damage landed and the character is still alive. Attacker is whoever caused it (the
	// shooter of a projectile, the summoner's minion, ...). Monsters turn on their attacker.
	virtual void OnDamaged(AActor* Attacker, float Damage) {}

	// Server. Brings a dead character back with HealthFraction of its Max HP (at least 1 HP). It gets
	// back what is still on its body: worn gear goes back on, the rest into the inventory. Whatever
	// was looted is gone. For revive spells. False if the character is not dead, or the 120 s revive
	// window has run out.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Death")
	bool Revive(float HealthFraction = 0.5f);

	// False for AI characters (monsters, minions): their death is final.
	virtual bool CanBeRevived() const { return true; }

	// The revive window ran out: dead for the rest of the raid.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Death")
	bool IsPermanentlyDead() const { return bPermanentlyDead; }

	// Launches on the owning client too, so its movement prediction does not undo a server-side
	// launch (knock-ups). Call LaunchCharacter on the server as well.
	UFUNCTION(Client, Reliable)
	void ClientLaunch(FVector Velocity);

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

	// [Stealth] anti-cheat: while State.Stealthed, not net-relevant to viewers outside its team, so
	// enemy clients never receive its position (the actor despawns there and is re-created when
	// stealth ends). Its own player and allies (spectators included) still get it. Switch:
	// crossguard.StealthHidesFromEnemies (CrossguardCharacter.cpp; placeholder, default on).
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

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

	// Not player-controlled (monsters): rolled into its body on death, on top of what it carried.
	// A row of Data/Loot/LootTables.json (e.g. "basic_monster"). None: only what it carried.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Progression")
	FName LootTableId;

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

	// Testing: on-screen text (top left) with Health, shield, weapons, the selected skill and
	// states; a quick tap of Q selects the next skill. ACrossguardHUD (the placeholder UI,
	// docs/ui.md) turns it off for its player, as its HUD and skill wheel replace it.
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

	// G (placeholder until the inventory UI): drops the stack in hand (a readied hotbar item).
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> DropAction;

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

	// Changes BaseWalkSpeed and applies it (AI stats from data).
	void SetBaseWalkSpeed(float Speed);

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

	// Block and parry (right click held). Granted on possession.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Combat")
	TSubclassOf<UCrossguardGuardAbility> GuardAbilityClass;

	// Set its ItemTable in the character Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Inventory")
	TObjectPtr<UCrossguardInventoryComponent> InventoryComponent;

	// Player-to-player trading.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Inventory")
	TObjectPtr<UCrossguardTradeComponent> TradeComponent;

	// [Hunter's Tracking] footprints, drawn on this character's own screen only.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Skills")
	TObjectPtr<UCrossguardTrackingComponent> TrackingComponent;

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
	void OnDrop();
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
	// Right click released: the guard drops.
	void OnSecondaryReleased();

	UFUNCTION(Server, Reliable)
	void ServerInteract();
	// Server. What TryInteract and ServerInteract do (the RPC is rate-limited first).
	void HandleInteract();
	// Server. World time of the last ServerInteract taken: each one walks every actor, so a client
	// spamming it is refused for InteractMinInterval.
	double LastInteractRequestTime = -1.0;
	static constexpr double InteractMinInterval = 0.1;
	// False for players in Shipping builds: the Debug options (loadout, progression, loot, revive)
	// then never reach them, even if a Blueprint default was left on. AI keep them.
	bool AllowsDebugOptions() const;

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

	// [Stealth]: hidden on this machine while State.Stealthed, unless the local player is on its
	// side. Each machine decides for itself; visibility is not replicated.
	void OnStealthChanged(const FGameplayTag Tag, int32 Count);
	void UpdateStealthVisibility();
	// The stealth rule of IsNetRelevantFor: true while stealthed (switch on) for a viewer that is
	// neither its own player nor an ally.
	bool IsHiddenByStealthFrom(const AActor* RealViewer, const AActor* ViewTarget) const;
	// Server, on stealth start: closes the actor channel of every client connection it is hidden from,
	// so enemy clients drop it at once instead of after RelevantTimeout (docs/anti-cheat.md).
	void CloseStealthHiddenChannels();
	bool bStealthBound = false;
	FTimerHandle StealthVisibilityTimer;
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

	// Server: the last melee swing, for clashes.
	double LastSwingTime = -1.0;
	int32 LastSwingImpactPower = 0;
	float LastSwingWeaponDamage = 0.0f;
	FVector LastSwingFacing = FVector::ForwardVector;

	// Clanks played on this machine (GetWeaponClankCount).
	int32 WeaponClankCount = 0;
};
