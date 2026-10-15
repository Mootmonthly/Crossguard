#include "Character/CrossguardCharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "Crossguard.h"
#include "CrossguardRules/Economy.h"
#include "CrossguardRules/Portal.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Game/CrossguardExtractPortal.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardPointOfInterest.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
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
	if (!IsLocallyControlled() || GEngine == nullptr)
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
		ApplyDebugLoadout();
	}
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
	if (bUseDebugProgression || Progression == nullptr)
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
	case ECrossguardSecondaryAction::None:
		return false;
	}
	return false;
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
		ServerInteract_Implementation();
	}
	else
	{
		ServerInteract();
	}
}

void ACrossguardCharacter::ServerInteract_Implementation()
{
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
	if (bDebugLootTakesAll && Container != nullptr && InventoryComponent->GetOpenContainer() == Container)
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

	if (DebugReviveDelay > 0.0f)
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
