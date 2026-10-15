#include "Character/CrossguardCharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "Combat/CrossguardWeaponAttackAbility.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
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
