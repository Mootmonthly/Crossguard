#include "Character/CrossguardCharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "Crossguard.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardLootContainer.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Skills/CrossguardLoadoutComponent.h"

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
	WeaponAttackAbilityClass = UCrossguardWeaponAttackAbility::StaticClass();
	OffHandAttackAbilityClass = UCrossguardOffHandAttackAbility::StaticClass();

	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;

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
	MakeAction(ReadyItemAction, TEXT("IA_ReadyItem"), EInputActionValueType::Boolean);

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
	Context->MapKey(AttackAction, EKeys::LeftMouseButton);
	Context->MapKey(SecondaryActionInput, EKeys::RightMouseButton);
	Context->MapKey(InteractAction, EKeys::E);
	Context->MapKey(ReadyItemAction, EKeys::Q);
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
	// Triggered fires every frame while held: holding the button keeps attacking at the weapon's rate.
	Input->BindAction(AttackAction, ETriggerEvent::Started, this, &ACrossguardCharacter::OnAttackPressed);
	Input->BindAction(AttackAction, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnAttack);
	Input->BindAction(AttackAction, ETriggerEvent::Completed, this, &ACrossguardCharacter::OnAttackReleased);
	Input->BindAction(SecondaryActionInput, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnSecondary);
	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ACrossguardCharacter::TryInteract);
	Input->BindAction(ReadyItemAction, ETriggerEvent::Started, this, &ACrossguardCharacter::OnReadyItem);
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

void ACrossguardCharacter::OnReadyItem()
{
	InventoryComponent->RequestReadyNextConsumable();
}

void ACrossguardCharacter::OnSecondary()
{
	TrySecondaryAction();
}

void ACrossguardCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACrossguardCharacter, Level);
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
}

void ACrossguardCharacter::OnMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	ApplyMoveSpeed(Data.NewValue);
}

void ACrossguardCharacter::ApplyMoveSpeed(float MoveSpeed)
{
	// Server and owning client both set it so client-side movement prediction agrees.
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * FMath::Max(MoveSpeed, 0.0f);
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
	ACrossguardLootContainer* Target = FindInteractTarget();
	if (Target == nullptr || !InventoryComponent->OpenLootContainer(Target))
	{
		return;
	}
	UE_LOG(LogCrossguard, Log, TEXT("%s opened %s (%d items)"), *GetName(), *Target->GetName(), Target->GetItems().Num());

	if (bDebugLootTakesAll)
	{
		TArray<int32> Ids;
		for (const FCrossguardInventoryItem& Item : Target->GetItems())
		{
			Ids.Add(Item.Id);
		}
		for (const int32 Id : Ids)
		{
			InventoryComponent->RequestTakeFromContainer(Id);
		}
	}
}

ACrossguardLootContainer* ACrossguardCharacter::FindInteractTarget() const
{
	FVector EyeLocation;
	FRotator EyeRotation;
	GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector Forward = EyeRotation.Vector();

	ACrossguardLootContainer* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ACrossguardLootContainer> It(GetWorld()); It; ++It)
	{
		ACrossguardLootContainer* Container = *It;
		const FVector ToContainer = Container->GetActorLocation() - EyeLocation;
		const float Distance = FVector::Dist(GetActorLocation(), Container->GetActorLocation());
		// Roughly in front: within about 60 degrees of where the camera looks.
		if (Distance <= Container->InteractRange && Distance < BestDistance && FVector::DotProduct(ToContainer.GetSafeNormal(), Forward) >= 0.5f)
		{
			Best = Container;
			BestDistance = Distance;
		}
	}
	return Best;
}

void ACrossguardCharacter::HandleDeath()
{
	if (!HasAuthority() || BodyContainerClass == nullptr)
	{
		return;
	}
	TArray<FCrossguardInventoryItem> Dropped = InventoryComponent->TakeEverything();
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
	if (!HasAuthority() || !AbilitySystemComponent->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		return false;
	}

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
