#pragma once

#include "CoreMinimal.h"
#include "Items/CrossguardItemTypes.h"
#include "UObject/SoftObjectPtr.h"

#include "CrossguardCosmetics.generated.h"

class UAnimInstance;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class USkeletalMesh;
class USoundBase;
class UStaticMesh;

// Art hooks (docs/art-placeholder-plan.md, "How the art hooks into the code"). Everything here is
// cosmetic: it never changes a rule, and it runs on every machine that renders (never on a dedicated
// server). Asset references are soft: an empty one means "nothing", which is the default everywhere.

// What started (FCrossguardAttackCue::Kind).
UENUM(BlueprintType)
enum class ECrossguardAttackKind : uint8
{
	// A melee swing (weapon or claws).
	Melee,
	// A bow / crossbow / staff shot, a wand's bolt, a monster's arrow.
	Ranged,
	// A thrown item (knife, axe, bottle).
	Throw,
	// A skill cast (SkillId says which).
	Cast,
};

// An attack starting, for animations, whooshes and the placeholder swing (ACrossguardCharacter::OnAttackStarted).
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardAttackCue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	ECrossguardAttackKind Kind = ECrossguardAttackKind::Melee;

	// False: natural attacks (claws, fists) and casts without a weapon.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	bool bHasWeapon = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	ECrossguardWeaponType WeaponType = ECrossguardWeaponType::Sword;

	// Which hand (dual wielding: OffHand for the right-click attack).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	ECrossguardEquipmentSlot Slot = ECrossguardEquipmentSlot::MainHand;

	// Counts up with every attack the character starts: pick a montage with AttackIndex % 3
	// (MM_Attack_01-03) for alternating swings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	int32 AttackIndex = 0;

	// Seconds until the next attack can start (the weapon's interval after Action Speed). 0: unknown
	// (skill casts): use the animation's own length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	float Duration = 0.0f;

	// Cast: the skill id ("fireball").
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	FName SkillId;
};

// What a projectile looks and sounds like. Set on a projectile Blueprint's defaults, or filled from data
// (an ammo or throwable row in Data/Items/Items.json: projectileMesh, trailVfx, impactVfx, impactSound).
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardProjectileVisual
{
	GENERATED_BODY()

	// Static mesh flying with the projectile (no collision). Its X axis points along the flight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	FVector MeshScale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	FRotator MeshRotation = FRotator::ZeroRotator;

	// Niagara system attached for the flight (a trail, a fireball's flames).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	TSoftObjectPtr<UNiagaraSystem> TrailVfx;

	// Played where it hits or explodes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	TSoftObjectPtr<UNiagaraSystem> ImpactVfx;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	TSoftObjectPtr<USoundBase> ImpactSound;

	// Fields set in Other replace these (data over Blueprint defaults).
	void MergeFrom(const FCrossguardProjectileVisual& Other);
};

// A monster's or minion's body from data (Data/Mobs/Mobs.json mesh fields, a summon ability's
// settings). Empty Mesh: the placeholder cylinder stays.
USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardBodyVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	// An Anim Blueprint's generated class (e.g. /Game/Monsters/ABP_Skeleton.ABP_Skeleton_C).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	TSoftClassPtr<UAnimInstance> AnimClass;

	// On top of the actor's scale (a mob's `scale` grows the capsule too; this only the mesh).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	float MeshScale = 1.0f;

	// cm from the capsule's bottom centre (where the feet go).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	FVector MeshOffset = FVector::ZeroVector;

	// Degrees. -90 suits meshes facing +Y (the UE mannequin); 0 meshes facing +X.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	float MeshYaw = -90.0f;

	bool HasMesh() const { return !Mesh.IsNull(); }
};

// Playing the soft references. All of them do nothing for an empty reference, without a world, and on
// a dedicated server. Assets load synchronously the first time (placeholder: a hitch on first use).
namespace CrossguardCosmetics
{
	// False on a dedicated server (nothing renders or plays sound there).
	CROSSGUARD_API bool CanPlay(const UObject* WorldContext);

	CROSSGUARD_API void PlaySoundAt(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location);

	CROSSGUARD_API void SpawnFxAt(const UObject* WorldContext, const TSoftObjectPtr<UNiagaraSystem>& System, const FVector& Location,
		const FRotator& Rotation = FRotator::ZeroRotator);

	// Attached to Parent (kept relative), destroyed with it.
	CROSSGUARD_API UNiagaraComponent* SpawnFxAttached(const TSoftObjectPtr<UNiagaraSystem>& System, USceneComponent* Parent);
}
