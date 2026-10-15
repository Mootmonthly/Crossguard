#include "Character/CrossguardCharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
}

UAbilitySystemComponent* ACrossguardCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACrossguardCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server.
	InitializeAbilitySystem();
	AbilitySystemComponent->InitializeAttributes(Level, AttributeAllocation);
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
