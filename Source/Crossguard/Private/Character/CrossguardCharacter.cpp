#include "Character/CrossguardCharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "Camera/CameraComponent.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Net/UnrealNetwork.h"
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
	WeaponAttackAbilityClass = UCrossguardWeaponAttackAbility::StaticClass();
	OffHandAttackAbilityClass = UCrossguardOffHandAttackAbility::StaticClass();

	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;

	// Face the camera direction (melee aiming, strafing) rather than the movement direction.
	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 300.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 50.0f, 60.0f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
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
	Input->BindAction(AttackAction, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnAttack);
	Input->BindAction(SecondaryActionInput, ETriggerEvent::Triggered, this, &ACrossguardCharacter::OnSecondary);
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
	TryWeaponAttack();
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
