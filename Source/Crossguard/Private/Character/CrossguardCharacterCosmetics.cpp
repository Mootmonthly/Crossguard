// ACrossguardCharacter's art hooks (docs/art-placeholder-plan.md, "How the art hooks into the code"):
// attack started, attack hit, damage taken, clank and death cosmetics. Nothing here changes a rule.

#include "Character/CrossguardCharacter.h"

#include "Combat/CrossguardCosmetics.h"
#include "Components/SkeletalMeshComponent.h"

void ACrossguardCharacter::NotifyAttackStarted(FCrossguardAttackCue Cue, bool bPredictedByOwner)
{
	if (HasAuthority())
	{
		Cue.AttackIndex = NextAttackIndex++;
		MulticastAttackStarted(Cue, bPredictedByOwner);
	}
	else if (IsLocallyControlled())
	{
		// The owning client predicting its own attack: played at once; the server's copy is skipped.
		Cue.AttackIndex = NextAttackIndex++;
		HandleAttackStarted(Cue);
	}
}

void ACrossguardCharacter::MulticastAttackStarted_Implementation(FCrossguardAttackCue Cue, bool bPredictedByOwner)
{
	if (bPredictedByOwner && !HasAuthority() && IsLocallyControlled())
	{
		return;
	}
	HandleAttackStarted(Cue);
}

void ACrossguardCharacter::HandleAttackStarted(const FCrossguardAttackCue& Cue)
{
	++AttackStartedCount;
	CrossguardCosmetics::PlaySoundAt(this, SwingSound, GetActorLocation());
	OnAttackStarted(Cue);
	OnAttackStartedNative.Broadcast(this, Cue);
}

void ACrossguardCharacter::NotifyDamageTaken(float Amount, AActor* Source, const FVector& HitLocation, bool bBlocked, bool bDirectHit)
{
	if (HasAuthority())
	{
		MulticastDamageTaken(Amount, Source, HitLocation, bBlocked, bDirectHit);
	}
}

void ACrossguardCharacter::MulticastDamageTaken_Implementation(float Amount, AActor* Source, FVector_NetQuantize HitLocation, bool bBlocked,
	bool bDirectHit)
{
	++DamageTakenCount;
	CrossguardCosmetics::SpawnFxAt(this, HitVfx, HitLocation);
	OnDamageTaken(Amount, Source, HitLocation, bBlocked);
	OnDamageTakenNative.Broadcast(this, Amount, Source, HitLocation, bBlocked);
	// The attacker's side of a direct hit (its hit-confirm sound), on the same message.
	if (bDirectHit && Amount > 0.0f)
	{
		if (ACrossguardCharacter* Attacker = Cast<ACrossguardCharacter>(Source); Attacker != nullptr && Attacker != this)
		{
			Attacker->HandleAttackHit(HitLocation, this);
		}
	}
}

void ACrossguardCharacter::HandleAttackHit(const FVector& HitLocation, AActor* Target)
{
	CrossguardCosmetics::PlaySoundAt(this, HitSound, HitLocation);
	OnAttackHit(HitLocation, Target);
	OnAttackHitNative.Broadcast(this, HitLocation, Target);
}

void ACrossguardCharacter::PlayClankCosmetics(const FVector& ImpactPoint)
{
	CrossguardCosmetics::PlaySoundAt(this, BlockSound, ImpactPoint);
	CrossguardCosmetics::SpawnFxAt(this, ClankVfx, ImpactPoint);
}

void ACrossguardCharacter::PlayDeathCosmetics()
{
	CrossguardCosmetics::PlaySoundAt(this, DeathSound, GetActorLocation());
}

void ACrossguardCharacter::SetBodyMeshRelativeTransform(const FTransform& Transform)
{
	MeshRelativeTransform = Transform;
	GetMesh()->SetRelativeTransform(Transform);
	// Network smoothing on clients puts the mesh back to the offset cached at spawn every frame:
	// without this a mesh's yaw from data is lost on other players' machines (they walk sideways).
	CacheInitialMeshOffset(Transform.GetLocation(), Transform.Rotator());
}
