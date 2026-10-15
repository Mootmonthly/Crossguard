#include "Character/CrossguardAICharacter.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemGlobals.h"
#include "Combat/CrossguardSpellProjectile.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Skills/CrossguardSpellSkills.h"
#include "UObject/ConstructorHelpers.h"

ACrossguardAICharacter::ACrossguardAICharacter()
{
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	FireballClass = ACrossguardSpellProjectile::StaticClass();
	// AI turns toward where it moves.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	// The base character hides its mesh from its owner (first person); an AI character's owner may
	// be a player (a summoner), who must see it.
	GetMesh()->SetOwnerNoSee(false);

	// Visible without any art: a cylinder the size of the capsule.
	PlaceholderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderMesh"));
	PlaceholderMesh->SetupAttachment(GetCapsuleComponent());
	PlaceholderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		PlaceholderMesh->SetStaticMesh(Cylinder.Object);
		// The engine cylinder is 100 cm wide and 100 cm tall, centered.
		const float Radius = GetCapsuleComponent()->GetUnscaledCapsuleRadius();
		const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		PlaceholderMesh->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, HalfHeight / 50.0f));
	}
}

void ACrossguardAICharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// Fixed Health instead of level-based attributes, kept when buffs change the attributes.
	GetCrossguardAbilitySystemComponent()->SetFixedMaxHealth(FixedHealth);
	RestoreFullHealth();
}

void ACrossguardAICharacter::RestoreFullHealth()
{
	UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	if (HasAuthority() && ASC != nullptr && IsAlive())
	{
		ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(),
			ASC->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute()));
	}
}

bool ACrossguardAICharacter::IsAlive() const
{
	const UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	return !IsActorBeingDestroyed() && ASC != nullptr && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
}

bool ACrossguardAICharacter::TryAttack(AActor* Target)
{
	UCrossguardAbilitySystemComponent* ASC = GetCrossguardAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	const double Now = GetWorld()->GetTimeSeconds();
	if (!HasAuthority() || !IsAlive() || ASC == nullptr || TargetASC == nullptr || Now < NextAttackTime)
	{
		return false;
	}

	if (CombatRole == ECrossguardCombatRole::Mage)
	{
		FVector Eyes;
		FRotator Unused;
		GetActorEyesViewPoint(Eyes, Unused);
		const FVector Direction = (Target->GetActorLocation() - Eyes).GetSafeNormal();
		UCrossguardFireballAbility::LaunchFireball(ASC, FireballClass, Eyes, Direction);
		NextAttackTime = Now + FireballInterval;
		return true;
	}

	if (FVector::Dist(GetActorLocation(), Target->GetActorLocation()) > AttackRange)
	{
		return false;
	}
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, AttackDamage);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, 0.0f);
		ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
	}
	NextAttackTime = Now + AttackInterval;
	return true;
}
