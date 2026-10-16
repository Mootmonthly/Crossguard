#include "Character/CrossguardCharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/CrossguardGuard.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "Crossguard.h"
#include "CrossguardRules/Economy.h"
#include "CrossguardRules/Portal.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Game/CrossguardExtractPortal.h"
#include "Game/CrossguardGuildComponent.h"
#include "Game/CrossguardPartyComponent.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardPointOfInterest.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/ActorChannel.h"
#include "Engine/ChildConnection.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Interaction/CrossguardInteractable.h"
#include "Interaction/CrossguardLockComponent.h"
#include "Items/CrossguardLootContainer.h"
#include "Items/CrossguardTradeComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "Skills/CrossguardSkillAbility.h"
#include "Skills/CrossguardTrackingComponent.h"

namespace CrossguardStealthRelevancy
{
	// Placeholder switch for the play test (docs/anti-cheat.md, "Stealth position leak"). Read on the
	// server only: a client changing its own copy changes nothing.
	TAutoConsoleVariable<bool> CVarStealthHidesFromEnemies(
		TEXT("crossguard.StealthHidesFromEnemies"),
		true,
		TEXT("Stealthed characters are not net-relevant to clients outside their team, so enemy clients get no position (the actor despawns there). 0: old behavior, hidden only by each client's rendering."),
		ECVF_Default);
}

ACrossguardCharacter::ACrossguardCharacter()
{
	AbilitySystemComponent = CreateDefaultSubobject<UCrossguardAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// Mixed: full effect replication to the owner, minimal to everyone else.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Created as a subobject of the owning actor, so the ASC registers it automatically.
	AttributeSet = CreateDefaultSubobject<UCrossguardAttributeSet>(TEXT("AttributeSet"));

	LoadoutComponent = CreateDefaultSubobject<UCrossguardLoadoutComponent>(TEXT("LoadoutComponent"));
	EquipmentComponent = CreateDefaultSubobject<UCrossguardEquipmentComponent>(TEXT("EquipmentComponent"));
	InventoryComponent = CreateDefaultSubobject<UCrossguardInventoryComponent>(TEXT("InventoryComponent"));
	TradeComponent = CreateDefaultSubobject<UCrossguardTradeComponent>(TEXT("TradeComponent"));
	TrackingComponent = CreateDefaultSubobject<UCrossguardTrackingComponent>(TEXT("TrackingComponent"));
	TrackingComponent->SetupAttachment(GetRootComponent());
	WeaponAttackAbilityClass = UCrossguardWeaponAttackAbility::StaticClass();
	OffHandAttackAbilityClass = UCrossguardOffHandAttackAbility::StaticClass();
	GuardAbilityClass = UCrossguardGuardAbility::StaticClass();

	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	GetCharacterMovement()->MaxFlySpeed = BaseWalkSpeed;
	// Stop when the keys are let go while flying ([Flight]), as on the ground.
	GetCharacterMovement()->BrakingDecelerationFlying = GetCharacterMovement()->BrakingDecelerationWalking;

	// First person: the body turns with the view.
	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;

	// At BaseEyeHeight, the same point weapon traces and projectiles start from.
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetRootComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;

	GetMesh()->SetOwnerNoSee(true);

	BodyContainerClass = ACrossguardLootContainer::StaticClass();
	PortalClass = ACrossguardExtractPortal::StaticClass();
}

void ACrossguardCharacter::EnsureDefaultInput()
{
	const auto MakeAction = [this](TObjectPtr<UInputAction>& Action, const TCHAR* Name, EInputActionValueType Type)
	{
		if (Action == nullptr)
		{
			Action = NewObject<UInputAction>(this, Name);
			Action->ValueType = Type;
		}
	};
	MakeAction(MoveAction, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	MakeAction(LookAction, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	MakeAction(JumpAction, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	MakeAction(AttackAction, TEXT("IA_Attack"), EInputActionValueType::Boolean);
	MakeAction(SecondaryActionInput, TEXT("IA_Secondary"), EInputActionValueType::Boolean);
	MakeAction(InteractAction, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	MakeAction(WeaponSet1Action, TEXT("IA_WeaponSet1"), EInputActionValueType::Boolean);
	MakeAction(WeaponSet2Action, TEXT("IA_WeaponSet2"), EInputActionValueType::Boolean);
	MakeAction(Hotbar1Action, TEXT("IA_Hotbar1"), EInputActionValueType::Boolean);
	MakeAction(Hotbar2Action, TEXT("IA_Hotbar2"), EInputActionValueType::Boolean);
	MakeAction(Hotbar3Action, TEXT("IA_Hotbar3"), EInputActionValueType::Boolean);
	MakeAction(Hotbar4Action, TEXT("IA_Hotbar4"), EInputActionValueType::Boolean);
	MakeAction(Hotbar5Action, TEXT("IA_Hotbar5"), EInputActionValueType::Boolean);
	MakeAction(SkillWheelAction, TEXT("IA_SkillWheel"), EInputActionValueType::Boolean);
	MakeAction(CastSkillAction, TEXT("IA_CastSkill"), EInputActionValueType::Boolean);
	MakeAction(SummonPortalAction, TEXT("IA_SummonPortal"), EInputActionValueType::Boolean);
	MakeAction(FlyDownAction, TEXT("IA_FlyDown"), EInputActionValueType::Boolean);
	MakeAction(DropAction, TEXT("IA_Drop"), EInputActionValueType::Boolean);

	if (DefaultMappingContext != nullptr)
	{
		return;
	}
	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Default"));
	const auto Swizzle = [Context]() { return NewObject<UInputModifierSwizzleAxis>(Context); };
	const auto Negate = [Context]() { return NewObject<UInputModifierNegate>(Context); };

	// Move is X = right, Y = forward. Keys give X by default; Swizzle moves it to Y.
	Context->MapKey(MoveAction, EKeys::W).Modifiers.Add(Swizzle());
	FEnhancedActionKeyMapping& Back = Context->MapKey(MoveAction, EKeys::S);
	Back.Modifiers.Add(Swizzle());
	Back.Modifiers.Add(Negate());
	Context->MapKey(MoveAction, EKeys::D);
	Context->MapKey(MoveAction, EKeys::A).Modifiers.Add(Negate());

	// Mouse up looks up.
	UInputModifierNegate* InvertY = Negate();
	InvertY->bX = false;
	InvertY->bZ = false;
	Context->MapKey(LookAction, EKeys::Mouse2D).Modifiers.Add(InvertY);

	Context->MapKey(JumpAction, EKeys::SpaceBar);
	Context->MapKey(FlyDownAction, EKeys::LeftControl);
	Context->MapKey(DropAction, EKeys::G);
	Context->MapKey(AttackAction, EKeys::LeftMouseButton);
	Context->MapKey(SecondaryActionInput, EKeys::RightMouseButton);
	Context->MapKey(InteractAction, EKeys::F);
	Context->MapKey(WeaponSet1Action, EKeys::One);
	Context->MapKey(WeaponSet2Action, EKeys::Two);
	Context->MapKey(Hotbar1Action, EKeys::Three);
	Context->MapKey(Hotbar2Action, EKeys::Four);
	Context->MapKey(Hotbar3Action, EKeys::Five);
	Context->MapKey(Hotbar4Action, EKeys::Six);
	Context->MapKey(Hotbar5Action, EKeys::Seven);
	Context->MapKey(SkillWheelAction, EKeys::Q);
	Context->MapKey(CastSkillAction, EKeys::E);
	Context->MapKey(SummonPortalAction, EKeys::X);
	DefaultMappingContext = Context;
}

void ACrossguardCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureDefaultInput();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (Input == nullptr)
	{
		return;
	}
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnLook);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnFlyUp);
	Input->BindAction(FlyDownAction, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnFlyDown);
	Input->BindAction(DropAction, ETriggerEvent::Started, this, &ACrossguardCharacter::OnDrop);
	// Triggered fires every frame while held: holding the button keeps attacking at the weapon's rate.
	Input->BindAction(AttackAction, ETriggerEvent::Started, this, &ACrossguardCharacter::OnAttackPressed);
	Input->BindAction(AttackAction, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnAttack);
	Input->BindAction(AttackAction, ETriggerEvent::Completed, this, &ACrossguardCharacter::OnAttackReleased);
	Input->BindAction(SecondaryActionInput, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnSecondary);
	Input->BindAction(SecondaryActionInput, ETriggerEvent::Completed, this, &ACrossguardCharacter::OnSecondaryReleased);
	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ACrossguardCharacter::TryInteract);
	Input->BindAction(WeaponSet1Action, ETriggerEvent::Started, this, &ACrossguardCharacter::OnWeaponSet1);
	Input->BindAction(WeaponSet2Action, ETriggerEvent::Started, this, &ACrossguardCharacter::OnWeaponSet2);
	Input->BindAction(Hotbar1Action, ETriggerEvent::Started, this, &ACrossguardCharacter::OnHotbar1);
	Input->BindAction(Hotbar2Action, ETriggerEvent::Started, this, &ACrossguardCharacter::OnHotbar2);
	Input->BindAction(Hotbar3Action, ETriggerEvent::Started, this, &ACrossguardCharacter::OnHotbar3);
	Input->BindAction(Hotbar4Action, ETriggerEvent::Started, this, &ACrossguardCharacter::OnHotbar4);
	Input->BindAction(Hotbar5Action, ETriggerEvent::Started, this, &ACrossguardCharacter::OnHotbar5);
	Input->BindAction(SkillWheelAction, ETriggerEvent::Started, this, &ACrossguardCharacter::OnSkillWheelOpened);
	Input->BindAction(SkillWheelAction, ETriggerEvent::Completed, this, &ACrossguardCharacter::OnSkillWheelClosed);
	Input->BindAction(CastSkillAction, ETriggerEvent::Started, this, &ACrossguardCharacter::OnCastSkill);
	Input->BindAction(SummonPortalAction, ETriggerEvent::Started, this, &ACrossguardCharacter::TrySummonPortal);
}

void ACrossguardCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	EnsureDefaultInput();

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const ULocalPlayer* LocalPlayer = PlayerController != nullptr ? PlayerController->GetLocalPlayer() : nullptr;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		Subsystem->RemoveMappingContext(DefaultMappingContext);
		Subsystem->AddMappingContext(DefaultMappingContext, 0);
	}
}

void ACrossguardCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator Yaw(0.0f, GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
}

void ACrossguardCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ACrossguardCharacter::OnAttack()
{
	if (!bUsingItem && InventoryComponent->GetReadiedItemId() == 0)
	{
		TryWeaponAttack();
	}
}

void ACrossguardCharacter::OnAttackPressed()
{
	if (InventoryComponent->GetReadiedItemId() != 0)
	{
		bUsingItem = true;
		InventoryComponent->RequestStartUse();
	}
}

void ACrossguardCharacter::OnAttackReleased()
{
	if (bUsingItem)
	{
		bUsingItem = false;
		InventoryComponent->RequestStopUse();
	}
}

void ACrossguardCharacter::OnFlyUp()
{
	if (GetCharacterMovement()->IsFlying())
	{
		AddMovementInput(FVector::UpVector, 1.0f);
	}
}

void ACrossguardCharacter::OnFlyDown()
{
	if (GetCharacterMovement()->IsFlying())
	{
		AddMovementInput(FVector::UpVector, -1.0f);
	}
}

void ACrossguardCharacter::OnDrop()
{
	const int32 Readied = InventoryComponent->GetReadiedItemId();
	if (Readied != 0)
	{
		InventoryComponent->RequestDropItem(Readied);
	}
	else if (bDebugHud)
	{
		ShowDebugMessage(TEXT("Ready an item with 3-7 to drop it"), FColor::Orange);
	}
}

void ACrossguardCharacter::OnWeaponSet1()
{
	EquipmentComponent->RequestSwitchWeaponSet(0);
}

void ACrossguardCharacter::OnWeaponSet2()
{
	EquipmentComponent->RequestSwitchWeaponSet(1);
}

void ACrossguardCharacter::OnHotbar1()
{
	InventoryComponent->RequestUseHotbarSlot(0);
}

void ACrossguardCharacter::OnHotbar2()
{
	InventoryComponent->RequestUseHotbarSlot(1);
}

void ACrossguardCharacter::OnHotbar3()
{
	InventoryComponent->RequestUseHotbarSlot(2);
}

void ACrossguardCharacter::OnHotbar4()
{
	InventoryComponent->RequestUseHotbarSlot(3);
}

void ACrossguardCharacter::OnHotbar5()
{
	InventoryComponent->RequestUseHotbarSlot(4);
}

void ACrossguardCharacter::OnSkillWheelOpened()
{
	SelectedSkillAtWheelOpen = LoadoutComponent->GetSelectedSkill();
	LoadoutComponent->SetSkillWheelOpen(true);
}

void ACrossguardCharacter::OnSkillWheelClosed()
{
	LoadoutComponent->SetSkillWheelOpen(false);
	// No wheel widget picked anything: a tap of Q selects the next skill (testing until the UI).
	if (bDebugHud && LoadoutComponent->GetSelectedSkill() == SelectedSkillAtWheelOpen)
	{
		const TArray<FName> Skills = LoadoutComponent->GetActivatableSkills();
		if (Skills.IsEmpty())
		{
			ShowDebugMessage(TEXT("No skills to select (check DebugSkills, Level and the Output Log)"), FColor::Red);
			return;
		}
		const int32 Current = Skills.IndexOfByKey(LoadoutComponent->GetSelectedSkill());
		const FName Next = Skills[(Current + 1) % Skills.Num()];
		LoadoutComponent->SetSelectedSkill(Next);
		ShowDebugMessage(FString::Printf(TEXT("Selected %s"), *Next.ToString()), FColor::Cyan);
	}
}

void ACrossguardCharacter::OnCastSkill()
{
	const FName Skill = LoadoutComponent->GetSelectedSkill();
	const bool bCast = LoadoutComponent->TryActivateSelectedSkill();
	if (bDebugHud)
	{
		ShowDebugMessage(Skill.IsNone() ? FString(TEXT("No skill selected: tap Q"))
			: bCast ? FString::Printf(TEXT("Cast %s"), *Skill.ToString())
			: FString::Printf(TEXT("%s refused (cooldown, wrong weapon, Muted, or dragon form)"), *Skill.ToString()),
			bCast ? FColor::Green : FColor::Orange);
	}
}

void ACrossguardCharacter::ShowDebugMessage(const FString& Message, const FColor& Color) const
{
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, Color, Message);
	}
}

void ACrossguardCharacter::DrawDebugHud()
{
	// Off once ACrossguardHUD (the placeholder UI) takes over.
	if (!bDebugHud || !IsLocallyControlled() || GEngine == nullptr)
	{
		return;
	}
	const UCrossguardAbilitySystemComponent* ASC = AbilitySystemComponent;
	const auto Value = [ASC](const FGameplayAttribute& Attribute) { return ASC->GetNumericAttribute(Attribute); };

	FString States;
	const auto AddState = [ASC, &States](const FGameplayTag& Tag, const TCHAR* Name)
	{
		if (ASC->HasMatchingGameplayTag(Tag))
		{
			States += FString(Name) + TEXT(" ");
		}
	};
	AddState(CrossguardTags::State_Dead, TEXT("Dead"));
	AddState(CrossguardTags::State_Muted, TEXT("Muted"));
	AddState(CrossguardTags::State_Slowed, TEXT("Slowed"));
	AddState(CrossguardTags::State_Rooted, TEXT("Rooted"));
	AddState(CrossguardTags::State_Stunned, TEXT("Stunned"));
	AddState(CrossguardTags::State_Blinded, TEXT("BLINDED"));
	AddState(CrossguardTags::State_Flying, TEXT("Flying"));
	AddState(CrossguardTags::State_Dragonified, TEXT("Dragon"));
	AddState(CrossguardTags::State_ChopDownReady, TEXT("ChopDownReady"));

	const UCrossguardEquipmentComponent* Equipment = EquipmentComponent;
	const FString Weapons = Equipment->IsDrawingWeapons() ? FString::Printf(TEXT("drawing set %d..."), Equipment->GetActiveWeaponSet() + 1)
		: Equipment->AreWeaponsReady() ? FString::Printf(TEXT("set %d in hand"), Equipment->GetActiveWeaponSet() + 1)
		: FString(TEXT("holstered (press 1 or 2)"));

	const TArray<FName> Skills = LoadoutComponent->GetActivatableSkills();
	FString SkillList;
	for (const FName& Skill : Skills)
	{
		SkillList += (Skill == LoadoutComponent->GetSelectedSkill() ? TEXT("[") + Skill.ToString() + TEXT("] ") : Skill.ToString() + TEXT(" "));
	}

	// Fixed keys: each line replaces itself.
	const float Life = 0.3f;
	GEngine->AddOnScreenDebugMessage(91004, Life, FColor::Yellow, FString::Printf(TEXT("States: %s"), States.IsEmpty() ? TEXT("-") : *States));
	GEngine->AddOnScreenDebugMessage(91003, Life, FColor::White, FString::Printf(TEXT("Skills (tap Q, E casts): %s"),
		SkillList.IsEmpty() ? TEXT("none") : *SkillList));
	GEngine->AddOnScreenDebugMessage(91002, Life, FColor::White, FString::Printf(TEXT("Weapons: %s"), *Weapons));
	GEngine->AddOnScreenDebugMessage(91001, Life, FColor::Green, FString::Printf(TEXT("HP %.0f / %.0f   Shield %.0f   Level %d"),
		Value(UCrossguardAttributeSet::GetHealthAttribute()), Value(UCrossguardAttributeSet::GetMaxHealthAttribute()),
		Value(UCrossguardAttributeSet::GetAttackShieldAttribute()), Level));
}

void ACrossguardCharacter::OnSecondary()
{
	TrySecondaryAction();
}

void ACrossguardCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACrossguardCharacter, Level);
	DOREPLIFETIME_CONDITION(ACrossguardCharacter, bLockpicking, COND_OwnerOnly);
	DOREPLIFETIME(ACrossguardCharacter, bPermanentlyDead);
}

UAbilitySystemComponent* ACrossguardCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

FGenericTeamId ACrossguardCharacter::GetGenericTeamId() const
{
	const ACrossguardPlayerState* Team = GetPlayerState<ACrossguardPlayerState>();
	return Team != nullptr ? Team->GetGenericTeamId() : FGenericTeamId::NoTeam;
}

void ACrossguardCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server.
	InitializeAbilitySystem();
	PullProgression();
	AbilitySystemComponent->InitializeAttributes(Level, AttributeAllocation);
	EquipmentComponent->InitializeEquipmentEffect();

	if (!bWeaponAttackGranted)
	{
		bWeaponAttackGranted = true;
		for (const TSubclassOf<UCrossguardWeaponAttackAbility>& AttackClass : {WeaponAttackAbilityClass, OffHandAttackAbilityClass})
		{
			if (AttackClass != nullptr)
			{
				AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AttackClass, 1, INDEX_NONE, this));
			}
		}
		if (GuardAbilityClass != nullptr)
		{
			AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(GuardAbilityClass, 1, INDEX_NONE, this));
		}
		if (AllowsDebugOptions())
		{
			ApplyDebugLoadout();
		}
	}
}

bool ACrossguardCharacter::AllowsDebugOptions() const
{
#if UE_BUILD_SHIPPING
	return !IsPlayerControlled();
#else
	return true;
#endif
}

void ACrossguardCharacter::ApplyDebugLoadout()
{
	const auto RollWeapon = [this](FCrossguardWeaponItem Item)
	{
		const FCrossguardWeaponRow* Row = EquipmentComponent->WeaponTable != nullptr && !Item.IsEmpty()
			? EquipmentComponent->WeaponTable->FindRow<FCrossguardWeaponRow>(Item.WeaponId, TEXT("ApplyDebugLoadout"), false)
			: nullptr;
		if (bRollDebugAffixes && Row != nullptr)
		{
			Item = UCrossguardItemLibrary::RollWeaponItem(EquipmentComponent->AffixTable, Item.WeaponId, *Row, Item.Rarity, FMath::Rand());
		}
		return Item;
	};

	// Main hand first: a two-handed main hand then rejects an off-hand item, as in play.
	if (!DebugMainHand.IsEmpty())
	{
		EquipmentComponent->Equip(ECrossguardEquipmentSlot::MainHand, RollWeapon(DebugMainHand));
	}
	if (!DebugOffHand.IsEmpty())
	{
		EquipmentComponent->Equip(ECrossguardEquipmentSlot::OffHand, RollWeapon(DebugOffHand));
	}

	for (FCrossguardArmorItem Piece : DebugArmor)
	{
		const FCrossguardArmorRow* Row = EquipmentComponent->ArmorTable != nullptr && !Piece.IsEmpty()
			? EquipmentComponent->ArmorTable->FindRow<FCrossguardArmorRow>(Piece.ArmorId, TEXT("ApplyDebugLoadout"), false)
			: nullptr;
		if (bRollDebugAffixes && Row != nullptr)
		{
			Piece = UCrossguardItemLibrary::RollArmorItem(EquipmentComponent->AffixTable, Piece.ArmorId, *Row, Piece.Rarity, FMath::Rand());
		}
		ECrossguardArmorSlot Slot = ECrossguardArmorSlot::Head;
		if (EquipmentComponent->GetArmorSlotFor(Piece.ArmorId, Slot))
		{
			EquipmentComponent->EquipArmor(Slot, Piece);
		}
	}

	for (const FCrossguardInventoryItem& Item : DebugInventory)
	{
		if (const int32 Left = InventoryComponent->AddItem(Item); Left > 0)
		{
			UE_LOG(LogCrossguard, Warning, TEXT("Debug inventory: %d of %s did not fit or has no data"), Left,
				*(Item.ItemId.IsNone() ? (Item.Weapon.IsEmpty() ? Item.Armor.ArmorId : Item.Weapon.WeaponId) : Item.ItemId).ToString());
		}
	}

	// Testing: the first consumables and throwables go on the hotbar (keys 3-7).
	int32 HotbarSlot = 0;
	for (const FCrossguardInventoryItem& Item : InventoryComponent->GetItems())
	{
		const bool bHotbarItem = Item.Category == ECrossguardItemCategory::Consumable || Item.Category == ECrossguardItemCategory::Throwable;
		const bool bAlreadyAssigned = InventoryComponent->GetHotbar().ContainsByPredicate([&Item](const FCrossguardHotbarSlot& Slot)
		{
			return Slot.ItemId == Item.ItemId && Slot.Rarity == Item.Rarity;
		});
		if (bHotbarItem && !bAlreadyAssigned && HotbarSlot < InventoryComponent->GetHotbar().Num())
		{
			InventoryComponent->RequestAssignHotbarSlot(HotbarSlot++, Item.Id);
		}
	}

	if (!DebugSkills.IsEmpty())
	{
		if (ACrossguardPlayerState* Progression = GetPlayerState<ACrossguardPlayerState>())
		{
			for (const FName& Skill : DebugSkills)
			{
				Progression->LearnSkillForTesting(Skill);
			}
		}
		LoadoutComponent->RequestLoadout(DebugSkills);
	}
}

bool ACrossguardCharacter::PullProgression()
{
	const ACrossguardPlayerState* Progression = GetPlayerState<ACrossguardPlayerState>();
	if ((bUseDebugProgression && AllowsDebugOptions()) || Progression == nullptr)
	{
		return false;
	}
	const int32 NewLevel = Progression->GetCharacterLevel();
	const FCrossguardAttributeAllocation& NewAllocation = Progression->GetAttributeAllocation();
	const bool bChanged = NewLevel != Level || NewAllocation.Strength != AttributeAllocation.Strength
		|| NewAllocation.Agility != AttributeAllocation.Agility || NewAllocation.Intelligence != AttributeAllocation.Intelligence;
	Level = NewLevel;
	AttributeAllocation = NewAllocation;
	return bChanged;
}

void ACrossguardCharacter::ApplyProgression()
{
	// XP earned in a raid does not change the level, so this does not refill Health mid-raid.
	if (HasAuthority() && PullProgression())
	{
		AbilitySystemComponent->InitializeAttributes(Level, AttributeAllocation);
	}
}

bool ACrossguardCharacter::TrySecondaryAction()
{
	switch (EquipmentComponent->GetSecondaryAction())
	{
	case ECrossguardSecondaryAction::OffHandAttack:
		return OffHandAttackAbilityClass != nullptr && AbilitySystemComponent->TryActivateAbilityByClass(OffHandAttackAbilityClass);
	case ECrossguardSecondaryAction::Block:
	case ECrossguardSecondaryAction::Parry:
		// Held: raised once, kept until released.
		if (AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Guarding))
		{
			return true;
		}
		return GuardAbilityClass != nullptr && AbilitySystemComponent->TryActivateAbilityByClass(GuardAbilityClass);
	case ECrossguardSecondaryAction::None:
		return false;
	}
	return false;
}

void ACrossguardCharacter::OnSecondaryReleased()
{
	const FGameplayTagContainer GuardTag(CrossguardTags::Ability_Guard);
	AbilitySystemComponent->CancelAbilities(&GuardTag);
}

bool ACrossguardCharacter::TryWeaponAttack()
{
	return WeaponAttackAbilityClass != nullptr && AbilitySystemComponent->TryActivateAbilityByClass(WeaponAttackAbilityClass);
}

void ACrossguardCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Owning client. Attribute values arrive through replication.
	InitializeAbilitySystem();
}

void ACrossguardCharacter::InitializeAbilitySystem()
{
	AbilitySystemComponent->InitAbilityActorInfo(this, this);

	if (!bMoveSpeedBound)
	{
		bMoveSpeedBound = true;
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UCrossguardAttributeSet::GetMoveSpeedAttribute())
			.AddUObject(this, &ACrossguardCharacter::OnMoveSpeedChanged);
	}
	ApplyMoveSpeed(AbilitySystemComponent->GetNumericAttribute(UCrossguardAttributeSet::GetMoveSpeedAttribute()));

	if (!bStealthBound)
	{
		bStealthBound = true;
		AbilitySystemComponent->RegisterGameplayTagEvent(CrossguardTags::State_Stealthed, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ACrossguardCharacter::OnStealthChanged);
	}
	// Already stealthed when this machine first sees it: start the re-checks too.
	OnStealthChanged(CrossguardTags::State_Stealthed, AbilitySystemComponent->GetTagCount(CrossguardTags::State_Stealthed));
}

void ACrossguardCharacter::OnStealthChanged(const FGameplayTag Tag, int32 Count)
{
	UpdateStealthVisibility();
	// Server: relevancy (IsNetRelevantFor) changes with stealth; replicate soon so enemy clients drop
	// the character, or get it back, without waiting for the next regular update.
	if (HasAuthority())
	{
		ForceNetUpdate();
		if (Count > 0)
		{
			CloseStealthHiddenChannels();
		}
	}
	// While stealthed, re-check twice a second: the local viewer's pawn or team may change. A
	// dedicated server draws nothing.
	if (Count > 0 && GetNetMode() != NM_DedicatedServer)
	{
		GetWorldTimerManager().SetTimer(StealthVisibilityTimer, this, &ACrossguardCharacter::UpdateStealthVisibility, 0.5f, true);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(StealthVisibilityTimer);
	}
}

bool ACrossguardCharacter::IsHiddenByStealthFrom(const AActor* RealViewer, const AActor* ViewTarget) const
{
	if (!CrossguardStealthRelevancy::CVarStealthHidesFromEnemies.GetValueOnGameThread()
		|| AbilitySystemComponent == nullptr || !AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Stealthed)
		|| RealViewer == nullptr)
	{
		return false;
	}
	// Always relevant to its own player (owner / view target), whatever the team.
	const bool bOwnViewer = RealViewer == GetController() || ViewTarget == this || IsOwnedBy(RealViewer)
		|| (ViewTarget != nullptr && IsOwnedBy(ViewTarget));
	// RealViewer is the viewing PlayerController; the team library resolves it through its player
	// state, so a spectating ally (no pawn) still counts. No design reveal radius yet
	// (design-decisions.md, Stealth): an enemy never gets it while stealthed.
	return !bOwnViewer && !UCrossguardTeamLibrary::AreAllies(RealViewer, this);
}

bool ACrossguardCharacter::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	// Server only (the net driver asks per client connection). Server-side AI never goes through
	// this: monsters' perception of stealth is separate. bAlwaysRelevant must stay false on
	// characters, or the net driver never asks; NetCullDistance only adds hiding on top of this.
	if (IsHiddenByStealthFrom(RealViewer, ViewTarget))
	{
		return false;
	}
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}

void ACrossguardCharacter::CloseStealthHiddenChannels()
{
	// Server, on stealth start. Not relevant any more is not enough: the classic net driver keeps an
	// open channel (and keeps sending the position) for RelevantTimeout (5 s) after the last relevant
	// frame. Close the enemy connections' channels now, as the net driver would after the timeout;
	// the client destroys its copy, and IsNetRelevantFor keeps it from reopening until stealth ends.
	// Classic net driver only: with a replication driver (ReplicationGraph) relevancy is decided by
	// its nodes and would reopen the channel; Iris has no actor channels. Both skip (the leak stays).
	if (!HasAuthority() || !CrossguardStealthRelevancy::CVarStealthHidesFromEnemies.GetValueOnGameThread())
	{
		return;
	}
	UNetDriver* NetDriver = GetNetDriver();
	if (NetDriver == nullptr || NetDriver->GetReplicationDriver() != nullptr)
	{
		return;
	}
#if defined(UE_WITH_IRIS) && UE_WITH_IRIS
	// Unbuilt guess (first-compile-triage.md, "Overnight code"): the 5.x accessor name.
	if (NetDriver->IsUsingIrisReplication())
	{
		return;
	}
#endif
	for (UNetConnection* Connection : NetDriver->ClientConnections)
	{
		if (Connection == nullptr)
		{
			continue;
		}
		UActorChannel* Channel = Connection->FindActorChannelRef(TWeakObjectPtr<AActor>(this));
		if (Channel == nullptr || Channel->Closing)
		{
			continue;
		}
		// The connection's viewers as the net driver builds them (FNetViewer): its player controller
		// (or owning actor) and view target, plus split-screen children. Hidden only if hidden from all.
		bool bHiddenFromAll = IsHiddenByStealthFrom(
			Connection->PlayerController != nullptr ? static_cast<const AActor*>(Connection->PlayerController.Get()) : Connection->OwningActor.Get(),
			Connection->ViewTarget.Get());
		for (const UChildConnection* Child : Connection->Children)
		{
			if (bHiddenFromAll && Child != nullptr)
			{
				bHiddenFromAll = IsHiddenByStealthFrom(
					Child->PlayerController != nullptr ? static_cast<const AActor*>(Child->PlayerController.Get()) : Child->OwningActor.Get(),
					Child->ViewTarget.Get());
			}
		}
		if (bHiddenFromAll)
		{
			Channel->Close(EChannelCloseReason::Relevancy);
		}
	}
}

void ACrossguardCharacter::ClientLaunch_Implementation(FVector Velocity)
{
	LaunchCharacter(Velocity, false, true);
}

void ACrossguardCharacter::UpdateStealthVisibility()
{
	bool bHideFromViewer = false;
	// Not your own character (an AI on the server counts as locally controlled, yet hides too).
	if (AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Stealthed) && !(IsPlayerControlled() && IsLocallyControlled()))
	{
		// The local viewer: its pawn, or its player state while spectating. No local player
		// (dedicated server): nothing to draw anyway.
		const APlayerController* Viewer = GetWorld()->GetFirstPlayerController();
		const AActor* ViewerSide = Viewer == nullptr ? nullptr
			: Viewer->GetPawn() != nullptr ? static_cast<const AActor*>(Viewer->GetPawn()) : static_cast<const AActor*>(Viewer->PlayerState.Get());
		bHideFromViewer = ViewerSide != nullptr && !UCrossguardTeamLibrary::AreAllies(ViewerSide, this);
	}
	// Everything attached to the capsule (body, weapons) follows.
	GetCapsuleComponent()->SetVisibility(!bHideFromViewer, true);
}

void ACrossguardCharacter::OnMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	ApplyMoveSpeed(Data.NewValue);
}

void ACrossguardCharacter::SetBaseWalkSpeed(float Speed)
{
	BaseWalkSpeed = FMath::Max(Speed, 0.0f);
	ApplyMoveSpeed(AbilitySystemComponent->GetNumericAttribute(UCrossguardAttributeSet::GetMoveSpeedAttribute()));
}

void ACrossguardCharacter::ApplyMoveSpeed(float MoveSpeed)
{
	// Server and owning client both set it so client-side movement prediction agrees.
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * FMath::Max(MoveSpeed, 0.0f);
	GetCharacterMovement()->MaxFlySpeed = GetCharacterMovement()->MaxWalkSpeed;
}

void ACrossguardCharacter::TryInteract()
{
	if (HasAuthority())
	{
		HandleInteract();
	}
	else
	{
		ServerInteract();
	}
}

void ACrossguardCharacter::ServerInteract_Implementation()
{
	const double Time = GetWorld()->GetTimeSeconds();
	if (LastInteractRequestTime >= 0.0 && Time - LastInteractRequestTime < InteractMinInterval)
	{
		UE_LOG(LogCrossguard, Verbose, TEXT("%s interact refused: too soon after the last one"), *GetName());
		return;
	}
	LastInteractRequestTime = Time;
	HandleInteract();
}

void ACrossguardCharacter::HandleInteract()
{
	if (bLeftRaid)
	{
		// Extracted: whatever it would pick up would never be part of the raid's result.
		UE_LOG(LogCrossguard, Log, TEXT("%s interact refused: extracted, no longer in the raid"), *GetName());
		return;
	}
	if (bLockpicking)
	{
		StopLockpicking();
		return;
	}
	if (AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		return;
	}
	AActor* Target = FindInteractTarget();
	ICrossguardInteractable* Interactable = Cast<ICrossguardInteractable>(Target);
	if (Interactable == nullptr)
	{
		return;
	}
	const UCrossguardLockComponent* Lock = Target->FindComponentByClass<UCrossguardLockComponent>();
	if (Lock != nullptr && Lock->IsLocked())
	{
		StartLockpicking(Target);
		return;
	}
	Interactable->Interact(this);

	ACrossguardLootContainer* Container = Cast<ACrossguardLootContainer>(Target);
	if (bDebugLootTakesAll && AllowsDebugOptions() && Container != nullptr && InventoryComponent->GetOpenContainer() == Container)
	{
		UE_LOG(LogCrossguard, Log, TEXT("%s opened %s (%d items)"), *GetName(), *Container->GetName(), Container->GetItems().Num());
		TArray<int32> Ids;
		for (const FCrossguardInventoryItem& Item : Container->GetItems())
		{
			Ids.Add(Item.Id);
		}
		for (const int32 Id : Ids)
		{
			InventoryComponent->RequestTakeFromContainer(Id);
		}
	}
}

AActor* ACrossguardCharacter::FindInteractTarget() const
{
	FVector EyeLocation;
	FRotator EyeRotation;
	GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector Forward = EyeRotation.Vector();

	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		const ICrossguardInteractable* Interactable = Cast<ICrossguardInteractable>(Candidate);
		if (Interactable == nullptr || Candidate == this)
		{
			continue;
		}
		const FVector ToCandidate = Candidate->GetActorLocation() - EyeLocation;
		const float Distance = FVector::Dist(GetActorLocation(), Candidate->GetActorLocation());
		// Roughly in front: within about 60 degrees of where the camera looks.
		if (Distance <= Interactable->GetInteractRange() && Distance < BestDistance
			&& FVector::DotProduct(ToCandidate.GetSafeNormal(), Forward) >= 0.5f)
		{
			Best = Candidate;
			BestDistance = Distance;
		}
	}
	return Best;
}

void ACrossguardCharacter::StartLockpicking(AActor* Target)
{
	const bool bHasSkill = AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::Ability_Lockpicking);
	if (!bHasSkill && InventoryComponent->CountItem(LockpickItemId) <= 0)
	{
		UE_LOG(LogCrossguard, Log, TEXT("%s cannot pick %s: no lockpick"), *GetName(), *GetNameSafe(Target));
		return;
	}
	const float Dexterity = AbilitySystemComponent->GetNumericAttribute(UCrossguardAttributeSet::GetDexterityAttribute());
	const float Seconds = static_cast<float>(Crossguard::LockpickSeconds(Crossguard::FEconomyConfig(), Dexterity));
	LockpickTarget = Target;
	bLockpicking = true;
	GetWorldTimerManager().SetTimer(LockpickTimer, this, &ACrossguardCharacter::FinishLockpicking, Seconds, false);
}

void ACrossguardCharacter::FinishLockpicking()
{
	AActor* Target = LockpickTarget.Get();
	StopLockpicking();

	ICrossguardInteractable* Interactable = Cast<ICrossguardInteractable>(Target);
	UCrossguardLockComponent* Lock = Target != nullptr ? Target->FindComponentByClass<UCrossguardLockComponent>() : nullptr;
	if (Interactable == nullptr || Lock == nullptr || AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Dead)
		|| FVector::Dist(GetActorLocation(), Target->GetActorLocation()) > Interactable->GetInteractRange())
	{
		return;
	}
	// The skill picks freely; otherwise one lockpick is used up.
	if (!AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::Ability_Lockpicking) && !InventoryComponent->ConsumeItem(LockpickItemId))
	{
		return;
	}
	Lock->SetLocked(false);
	Interactable->Interact(this);
}

void ACrossguardCharacter::StopLockpicking()
{
	GetWorldTimerManager().ClearTimer(LockpickTimer);
	LockpickTarget.Reset();
	bLockpicking = false;
}

void ACrossguardCharacter::HandleDeath()
{
	if (!HasAuthority())
	{
		return;
	}
	// Teammates have this long to revive; then the character is dead for the raid.
	GetWorldTimerManager().SetTimer(ReviveWindowTimer, this, &ACrossguardCharacter::OnReviveWindowExpired,
		static_cast<float>(Crossguard::FDeathConfig().ReviveWindowSeconds), false);
	// Skills in progress stop: Flight ends (the body falls), dragon form ends, channels stop.
	UCrossguardSkillAbility::CancelActiveSkills(AbilitySystemComponent);
	// The guard too (not a skill): its slow and State.Guarding go.
	const FGameplayTagContainer GuardTag(CrossguardTags::Ability_Guard);
	AbilitySystemComponent->CancelAbilities(&GuardTag);
	// Hands drop whatever they held; a revived character draws a weapon set again with 1 / 2.
	InventoryComponent->ReleaseHeldItem();
	EquipmentComponent->HolsterWeapons();
	if (BodyContainerClass == nullptr)
	{
		return;
	}
	StopLockpicking();
	TArray<FCrossguardInventoryItem> Dropped = InventoryComponent->TakeEverything();
	// Monsters also drop from their loot table.
	if (!IsPlayerControlled() && !LootTableId.IsNone())
	{
		if (ACrossguardGameMode* GameMode = GetWorld()->GetAuthGameMode<ACrossguardGameMode>())
		{
			Dropped.Append(GameMode->RollLoot(LootTableId));
		}
	}
	// Died again with items still left on the previous body (they did not fit at the revive): one
	// body holds everything.
	if (Body != nullptr)
	{
		Dropped.Append(Body->TakeAllItems());
		Body->Destroy();
		Body = nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Body = GetWorld()->SpawnActor<ACrossguardLootContainer>(BodyContainerClass, GetActorTransform(), Params);
	if (Body == nullptr)
	{
		UE_LOG(LogCrossguard, Warning, TEXT("%s died but its body container could not be spawned; %d items lost"), *GetName(), Dropped.Num());
		return;
	}
	Body->InitializeContents(Dropped);
	Body->SetBodyMesh(GetMesh()->GetSkeletalMeshAsset(), MeshRelativeTransform);

	if (DebugReviveDelay > 0.0f && AllowsDebugOptions())
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() { Revive(0.5f); }), DebugReviveDelay, false);
	}
}

bool ACrossguardCharacter::Revive(float HealthFraction)
{
	if (!HasAuthority() || bPermanentlyDead || !CanBeRevived() || !AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		return false;
	}
	GetWorldTimerManager().ClearTimer(ReviveWindowTimer);

	const float MaxHealth = AbilitySystemComponent->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute());
	const float Health = FMath::Max(1.0f, MaxHealth * FMath::Clamp(HealthFraction, 0.0f, 1.0f));
	AbilitySystemComponent->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(), Health);
	AttributeSet->ClearDead();

	// Whatever is still on the body comes back; anything that does not fit stays on it.
	if (Body != nullptr)
	{
		const TArray<FCrossguardInventoryItem> Leftover = InventoryComponent->RestoreFromBody(Body->TakeAllItems());
		if (Leftover.IsEmpty())
		{
			Body->Destroy();
			Body = nullptr;
		}
		else
		{
			Body->InitializeContents(Leftover);
		}
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s revived with %.0f HP"), *GetName(), Health);
	return true;
}

void ACrossguardCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Clients: AI characters get no OnRep_PlayerState, so set up their ability system here (tags
	// and attributes for visuals such as Stealth). Players repeat it harmlessly in OnRep_PlayerState.
	if (!HasAuthority())
	{
		InitializeAbilitySystem();
	}
	if (bDebugHud)
	{
		GetWorldTimerManager().SetTimer(DebugHudTimer, this, &ACrossguardCharacter::DrawDebugHud, 0.2f, true);
	}
	MeshRelativeTransform = GetMesh()->GetRelativeTransform();
	MeshCollisionProfile = GetMesh()->GetCollisionProfileName();
}

void ACrossguardCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The character is going away (e.g. its player left): the body stays as a ragdoll.
	if (HasAuthority() && Body != nullptr)
	{
		Body->ShowBodyMesh();
		Body = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ACrossguardCharacter::SetDeadVisuals(bool bDead)
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (bDead)
	{
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
		// Walk-through: the ragdoll and the body container are what others interact with.
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BodyMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		BodyMesh->SetSimulatePhysics(true);
		return;
	}

	BodyMesh->SetSimulatePhysics(false);
	BodyMesh->SetCollisionProfileName(MeshCollisionProfile);
	BodyMesh->AttachToComponent(Capsule, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	BodyMesh->SetRelativeTransform(MeshRelativeTransform);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

void ACrossguardCharacter::TrySummonPortal()
{
	if (HasAuthority())
	{
		ServerSummonPortal_Implementation();
	}
	else
	{
		ServerSummonPortal();
	}
}

void ACrossguardCharacter::ServerSummonPortal_Implementation()
{
	ACrossguardPlayerState* Progression = GetPlayerState<ACrossguardPlayerState>();
	const ACrossguardGameMode* GameMode = GetWorld()->GetAuthGameMode<ACrossguardGameMode>();
	if (Progression == nullptr || GameMode == nullptr || PortalClass == nullptr
		|| AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		return;
	}

	Crossguard::FPortalConfig Config;
	Config.UnlockSeconds = GameMode->PortalUnlockSeconds;
	const Crossguard::EPortalSummonError Error = Crossguard::CanSummonPortal(Config, Progression->IsInRaid(), GameMode->GetRaidElapsedSeconds(),
		ACrossguardPointOfInterest::IsInsideAny(GetWorld(), GetActorLocation()), Progression->HasSummonedPortal());
	if (Error != Crossguard::EPortalSummonError::None)
	{
		UE_LOG(LogCrossguard, Log, TEXT("%s cannot summon a portal: error %d"), *GetName(), static_cast<int32>(Error));
		return;
	}

	// At the feet, so the beam rises from the ground.
	const FVector Feet = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACrossguardExtractPortal* Portal = GetWorld()->SpawnActor<ACrossguardExtractPortal>(PortalClass, FTransform(GetActorRotation(), Feet), Params);
	if (Portal == nullptr)
	{
		return;
	}
	Portal->Initialize(this, UCrossguardTeamLibrary::GetTeamId(this));
	Progression->MarkPortalSummoned();
	UE_LOG(LogCrossguard, Log, TEXT("%s summoned an extract portal"), *GetName());
}

void ACrossguardCharacter::OnReviveWindowExpired()
{
	if (!HasAuthority() || bPermanentlyDead || !AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		return;
	}
	bPermanentlyDead = true;
	if (ACrossguardGameMode* GameMode = GetWorld()->GetAuthGameMode<ACrossguardGameMode>())
	{
		GameMode->HandlePermanentDeath(this);
	}
}

// --- Trading from the console (until a trade window exists) ---------------------------------------

void ACrossguardCharacter::TradeItems()
{
	for (const FCrossguardInventoryItem& Item : InventoryComponent->GetItems())
	{
		const FName What = Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.WeaponId
			: Item.Category == ECrossguardItemCategory::Armor ? Item.Armor.ArmorId : Item.ItemId;
		UE_LOG(LogCrossguard, Display, TEXT("Trade: item id %d = %s x%d"), Item.Id, *What.ToString(), Item.Count);
	}
}

void ACrossguardCharacter::TradeNearest()
{
	APawn* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ACrossguardCharacter> It(GetWorld()); It; ++It)
	{
		ACrossguardCharacter* Other = *It;
		const float Distance = FVector::Dist(GetActorLocation(), Other->GetActorLocation());
		// Other players: only player characters have a player state on clients.
		if (Other != this && Other->GetPlayerState() != nullptr && Distance < NearestDistance)
		{
			Nearest = Other;
			NearestDistance = Distance;
		}
	}
	UE_LOG(LogCrossguard, Display, TEXT("Trade: asking %s"), *GetNameSafe(Nearest));
	TradeComponent->RequestTrade(Nearest);
}

void ACrossguardCharacter::TradeRespond(bool bAccept) { TradeComponent->RespondToTrade(bAccept); }
void ACrossguardCharacter::TradeLock(bool bLocked) { TradeComponent->RequestLock(bLocked); }
void ACrossguardCharacter::TradeAccept() { TradeComponent->RequestAccept(); }
void ACrossguardCharacter::TradeCancel() { TradeComponent->RequestCancel(); }

void ACrossguardCharacter::TradeOffer(const FString& ItemIds, int32 Gold)
{
	TArray<FString> Parts;
	ItemIds.ParseIntoArray(Parts, TEXT(","));
	TArray<int32> Ids;
	for (const FString& Part : Parts)
	{
		Ids.Add(FCString::Atoi(*Part.TrimStartAndEnd()));
	}
	TradeComponent->RequestSetOffer(Ids, Gold);
}

// --- Guilds from the console (until a guild window exists) ----------------------------------------

namespace
{
	UCrossguardGuildComponent* GuildComponentOf(const APawn& Pawn)
	{
		const ACrossguardPlayerState* State = Pawn.GetPlayerState<ACrossguardPlayerState>();
		return State != nullptr ? State->GetGuild() : nullptr;
	}
}

void ACrossguardCharacter::GuildCreate(const FString& Name)
{
	if (UCrossguardGuildComponent* Guild = GuildComponentOf(*this))
	{
		Guild->RequestCreate(Name);
	}
}

void ACrossguardCharacter::GuildInviteNearest()
{
	UCrossguardGuildComponent* Guild = GuildComponentOf(*this);
	APlayerState* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ACrossguardCharacter> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(GetActorLocation(), It->GetActorLocation());
		if (*It != this && It->GetPlayerState() != nullptr && Distance < NearestDistance)
		{
			Nearest = It->GetPlayerState();
			NearestDistance = Distance;
		}
	}
	if (Guild != nullptr && Nearest != nullptr)
	{
		UE_LOG(LogCrossguard, Display, TEXT("Guild: inviting %s"), *Nearest->GetPlayerName());
		Guild->RequestInvite(Nearest);
	}
}

void ACrossguardCharacter::GuildAccept()
{
	UCrossguardGuildComponent* Guild = GuildComponentOf(*this);
	if (Guild != nullptr && !Guild->Invites.IsEmpty())
	{
		Guild->RequestAcceptInvite(Guild->Invites[0].GuildId);
	}
}

void ACrossguardCharacter::GuildLeave()
{
	if (UCrossguardGuildComponent* Guild = GuildComponentOf(*this))
	{
		Guild->RequestLeave();
	}
}

void ACrossguardCharacter::GuildInfo()
{
	const UCrossguardGuildComponent* Guild = GuildComponentOf(*this);
	if (Guild == nullptr)
	{
		return;
	}
	UE_LOG(LogCrossguard, Display, TEXT("Guild: %s, bank %lld gold"), Guild->IsInGuild() ? *Guild->GuildName : TEXT("none"), Guild->BankGold);
	for (const FCrossguardGuildMemberView& Member : Guild->Members)
	{
		UE_LOG(LogCrossguard, Display, TEXT("  member %s (%s) %s%s"), *Member.DisplayName, *Member.PlayerId,
			*StaticEnum<ECrossguardGuildRank>()->GetNameStringByValue(static_cast<int64>(Member.Rank)), Member.bOnline ? TEXT(", online") : TEXT(""));
	}
	for (const FCrossguardGuildBankItemView& Stack : Guild->BankItems)
	{
		const FCrossguardInventoryItem& Item = Stack.Item;
		const FName What = Item.Category == ECrossguardItemCategory::Weapon ? Item.Weapon.WeaponId
			: Item.Category == ECrossguardItemCategory::Armor ? Item.Armor.ArmorId : Item.ItemId;
		UE_LOG(LogCrossguard, Display, TEXT("  bank slot %d: %s x%d"), Stack.SlotId, *What.ToString(), Item.Count);
	}
	for (const FCrossguardGuildInviteView& Invite : Guild->Invites)
	{
		UE_LOG(LogCrossguard, Display, TEXT("  invite to %s from %s"), *Invite.GuildName, *Invite.InvitedBy);
	}
}

void ACrossguardCharacter::GuildRank(const FString& PlayerId, const FString& NewRank)
{
	UCrossguardGuildComponent* Guild = GuildComponentOf(*this);
	const int64 Value = StaticEnum<ECrossguardGuildRank>()->GetValueByNameString(NewRank);
	if (Guild != nullptr && Value >= 0 && Value <= static_cast<int64>(ECrossguardGuildRank::Leader))
	{
		Guild->RequestSetRank(PlayerId, static_cast<ECrossguardGuildRank>(Value));
	}
}

void ACrossguardCharacter::GuildKick(const FString& PlayerId)
{
	if (UCrossguardGuildComponent* Guild = GuildComponentOf(*this))
	{
		Guild->RequestKick(PlayerId);
	}
}

void ACrossguardCharacter::GuildGold(int32 Amount)
{
	if (UCrossguardGuildComponent* Guild = GuildComponentOf(*this))
	{
		Guild->RequestMoveGold(Amount);
	}
}

void ACrossguardCharacter::GuildDeposit(int32 InventoryItemId)
{
	if (UCrossguardGuildComponent* Guild = GuildComponentOf(*this))
	{
		Guild->RequestDepositItem(InventoryItemId);
	}
}

void ACrossguardCharacter::GuildWithdraw(int32 SlotId)
{
	if (UCrossguardGuildComponent* Guild = GuildComponentOf(*this))
	{
		Guild->RequestWithdrawItem(SlotId);
	}
}

void ACrossguardCharacter::GuildGive(const FString& PlayerId, int32 SlotId)
{
	if (UCrossguardGuildComponent* Guild = GuildComponentOf(*this))
	{
		Guild->RequestGiveItem(PlayerId, SlotId);
	}
}

// --- Parties from the console (until a party panel exists) ----------------------------------------

namespace CrossguardCharacterPartyConsole
{
	UCrossguardPartyComponent* PartyComponentOf(const APawn& Pawn)
	{
		const ACrossguardPlayerState* State = Pawn.GetPlayerState<ACrossguardPlayerState>();
		return State != nullptr ? State->GetParty() : nullptr;
	}

	// A member's id by name (any case) or id; empty if none.
	FString MemberIdOf(const UCrossguardPartyComponent& Party, const FString& NameOrId)
	{
		for (const FCrossguardPartyMemberView& Member : Party.GetMembers())
		{
			if (Member.PlayerId == NameOrId || Member.DisplayName.Equals(NameOrId, ESearchCase::IgnoreCase))
			{
				return Member.PlayerId;
			}
		}
		return FString();
	}

	// An inbox entry by 1-based number (as PartyInvites lists them), inviter's name (any case) or
	// party id; empty picks the first. INDEX_NONE if none matches.
	int32 InviteIndexOf(const UCrossguardPartyComponent& Party, const FString& Which)
	{
		const TArray<FCrossguardPartyInviteView>& Invites = Party.GetInvites();
		const FString Key = Which.TrimStartAndEnd();
		if (Key.IsEmpty())
		{
			return Invites.IsEmpty() ? INDEX_NONE : 0;
		}
		for (int32 Index = 0; Index < Invites.Num(); ++Index)
		{
			if (Invites[Index].PartyId == Key || Invites[Index].InvitedBy.Equals(Key, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		if (Key.IsNumeric())
		{
			const int32 Number = FCString::Atoi(*Key);
			return Number >= 1 && Number <= Invites.Num() ? Number - 1 : INDEX_NONE;
		}
		return INDEX_NONE;
	}
}

void ACrossguardCharacter::PartyInvite(const FString& PlayerName)
{
	UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this);
	const AGameStateBase* State = GetWorld()->GetGameState();
	if (Party == nullptr || State == nullptr)
	{
		return;
	}
	for (APlayerState* Other : State->PlayerArray)
	{
		if (Other != nullptr && Other != GetPlayerState() && Other->GetPlayerName().Equals(PlayerName, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogCrossguard, Display, TEXT("Party: inviting %s"), *Other->GetPlayerName());
			Party->RequestInvite(Other);
			return;
		}
	}
	UE_LOG(LogCrossguard, Display, TEXT("Party: no player named %s here (PartyInfo lists names)"), *PlayerName);
}

void ACrossguardCharacter::PartyAccept()
{
	UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this);
	if (Party != nullptr && !Party->GetInvites().IsEmpty())
	{
		Party->RequestAccept(Party->GetInvites()[0].PartyId);
	}
}

void ACrossguardCharacter::PartyInvites()
{
	const UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this);
	if (Party == nullptr)
	{
		return;
	}
	const TArray<FCrossguardPartyInviteView>& Invites = Party->GetInvites();
	UE_LOG(LogCrossguard, Display, TEXT("Party invites: %d"), Invites.Num());
	for (int32 Index = 0; Index < Invites.Num(); ++Index)
	{
		UE_LOG(LogCrossguard, Display, TEXT("  %d. from %s (%d members, party %s)"), Index + 1, *Invites[Index].InvitedBy,
			Invites[Index].MemberCount, *Invites[Index].PartyId);
	}
}

void ACrossguardCharacter::PartyDecline(const FString& Which)
{
	UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this);
	if (Party == nullptr)
	{
		return;
	}
	const int32 Index = CrossguardCharacterPartyConsole::InviteIndexOf(*Party, Which);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogCrossguard, Display, TEXT("Party: no invite '%s' (PartyInvites lists them)"), *Which);
		return;
	}
	UE_LOG(LogCrossguard, Display, TEXT("Party: declining the invite from %s"), *Party->GetInvites()[Index].InvitedBy);
	Party->RequestDecline(Party->GetInvites()[Index].PartyId);
}

void ACrossguardCharacter::PartyDeclineAll()
{
	if (UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this))
	{
		Party->RequestDeclineAll();
	}
}

void ACrossguardCharacter::PartyLeave()
{
	if (UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this))
	{
		Party->RequestLeave();
	}
}

void ACrossguardCharacter::PartyKick(const FString& Member)
{
	UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this);
	if (Party != nullptr)
	{
		// An unknown name goes as is: the server refuses it (TargetNotInParty).
		const FString Id = CrossguardCharacterPartyConsole::MemberIdOf(*Party, Member);
		Party->RequestKick(Id.IsEmpty() ? Member : Id);
	}
}

void ACrossguardCharacter::PartyPromote(const FString& Member)
{
	UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this);
	if (Party != nullptr)
	{
		const FString Id = CrossguardCharacterPartyConsole::MemberIdOf(*Party, Member);
		Party->RequestPromote(Id.IsEmpty() ? Member : Id);
	}
}

void ACrossguardCharacter::PartyInfo()
{
	const UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this);
	if (Party == nullptr)
	{
		return;
	}
	UE_LOG(LogCrossguard, Display, TEXT("Party: %s%s"), Party->IsInParty() ? *Party->GetPartyId() : TEXT("none"),
		Party->IsLeader() ? TEXT(" (you lead)") : TEXT(""));
	for (const FCrossguardPartyMemberView& Member : Party->GetMembers())
	{
		UE_LOG(LogCrossguard, Display, TEXT("  member %s (%s)%s"), *Member.DisplayName, *Member.PlayerId, Member.bLeader ? TEXT(", leader") : TEXT(""));
	}
	for (const FCrossguardPartyInviteView& Invite : Party->GetInvites())
	{
		UE_LOG(LogCrossguard, Display, TEXT("  invite from %s (%d members)"), *Invite.InvitedBy, Invite.MemberCount);
	}
	if (const AGameStateBase* State = GetWorld()->GetGameState())
	{
		for (const APlayerState* Other : State->PlayerArray)
		{
			if (Other != nullptr && Other != GetPlayerState())
			{
				UE_LOG(LogCrossguard, Display, TEXT("  here: %s"), *Other->GetPlayerName());
			}
		}
	}
}

void ACrossguardCharacter::PartyStartRaid()
{
	if (UCrossguardPartyComponent* Party = CrossguardCharacterPartyConsole::PartyComponentOf(*this))
	{
		Party->RequestStartRaid();
	}
}

bool ACrossguardCharacter::CanJumpInternal_Implementation() const
{
	return Super::CanJumpInternal_Implementation()
		&& (AbilitySystemComponent == nullptr || (!AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Stunned)
			&& !AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Rooted)));
}

void ACrossguardCharacter::NoteMeleeSwing(int32 InImpactPower, float InWeaponDamage)
{
	LastSwingTime = GetWorld()->GetTimeSeconds();
	LastSwingImpactPower = InImpactPower;
	LastSwingWeaponDamage = InWeaponDamage;
	LastSwingFacing = GetActorForwardVector();
}

bool ACrossguardCharacter::GetActiveSwing(double Now, double Window, int32& OutImpactPower, float& OutWeaponDamage, FVector& OutFacing) const
{
	if (LastSwingTime < 0.0 || Now - LastSwingTime > Window)
	{
		return false;
	}
	OutImpactPower = LastSwingImpactPower;
	OutWeaponDamage = LastSwingWeaponDamage;
	OutFacing = LastSwingFacing;
	return true;
}

void ACrossguardCharacter::MulticastWeaponClank_Implementation(FVector ImpactPoint)
{
	++WeaponClankCount;
	OnWeaponClank(ImpactPoint);
	OnWeaponClankNative.Broadcast(this, ImpactPoint);
}
