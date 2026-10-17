#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Items/CrossguardItemTypes.h"
#include "UObject/SoftObjectPtr.h"

#include "CrossguardWeaponVisualsComponent.generated.h"

class ACrossguardCharacter;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
struct FCrossguardAttackCue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardWeaponMeshesChanged);

// Shows the drawn weapon set (docs/art-placeholder-plan.md, "How the art hooks into the code"). Each
// hand whose weapon row has a `mesh` (Data/Items/Weapons.json; or MeshOverrides) gets two static mesh
// components:
// - first person, seen only by the character's own player: on the first-person arms (a skeletal mesh
//   component tagged FirstPersonArmsTag, at the hand socket), else on the camera at
//   FirstPersonCameraOffset (the "first feel test" placement);
// - third person, seen by everyone else: on the body mesh at the hand socket (`attachSocket`; default
//   hand_r / hand_l).
// Shown while the set is drawn and in hand; hidden while drawing, holstered or holding an item. An
// empty mesh shows nothing. Runs on every machine from the replicated equipment
// (OnEquipmentChanged); nothing on a dedicated server. Cosmetic only.
//
// Placeholder swing (bPlaceholderSwing): until animations exist, an attack swings the first-person
// mesh of its hand through an arc (melee) or pulls it back (ranged) over the attack.
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardWeaponVisualsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardWeaponVisualsComponent();

	// Code-driven swing of the first-person weapon (placeholder until animations; on by default).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crossguard|Art")
	bool bPlaceholderSwing = true;

	// Degrees the placeholder melee swing turns the weapon down and back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crossguard|Art", meta = (ClampMin = "0"))
	float PlaceholderSwingDegrees = 70.0f;

	// cm the placeholder ranged recoil pulls the weapon back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crossguard|Art", meta = (ClampMin = "0"))
	float PlaceholderRecoil = 8.0f;

	// The first-person arms: a skeletal mesh component on the character with this component tag.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Art")
	FName FirstPersonArmsTag = TEXT("FirstPersonArms");

	// Without arms: where each hand's weapon sits relative to the camera (cm; X forward, Y right, Z up).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Art")
	FVector FirstPersonCameraOffset = FVector(35.0f, 18.0f, -22.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Art")
	FVector FirstPersonCameraOffsetOffHand = FVector(35.0f, -18.0f, -22.0f);

	// Without arms: the weapon's tilt in front of the camera, before the row's own rotation (placeholder:
	// the blade leans 45 degrees forward, away from the view), and its size there (times the row's scale).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Art")
	FRotator FirstPersonCameraTilt = FRotator(-45.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Art", meta = (ClampMin = "0.01"))
	float FirstPersonCameraScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Art")
	FName MainHandSocket = TEXT("hand_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crossguard|Art")
	FName OffHandSocket = TEXT("hand_l");

	// Weapon id -> mesh, over the weapon table's `mesh` (a Blueprint-only way to try a mesh, and tests).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crossguard|Art")
	TMap<FName, TSoftObjectPtr<UStaticMesh>> MeshOverrides;

	// The meshes were rebuilt or shown / hidden (e.g. to set First Person Primitive Type on them).
	UPROPERTY(BlueprintAssignable, Category = "Crossguard|Art")
	FCrossguardWeaponMeshesChanged OnWeaponMeshesChanged;

	// The hand's mesh component (first or third person); null when that hand shows nothing.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Art")
	UStaticMeshComponent* GetWeaponMesh(ECrossguardEquipmentSlot Slot, bool bFirstPerson) const;

	// Rebuilds from the equipment now (also runs on every equipment change).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Art")
	void RefreshWeapons();

	// Throws the meshes away and builds them again from the weapon rows, even for the same weapons
	// (CgReloadArtData: a row's mesh or relativeTransform changed).
	void RebuildWeapons();

	// A placeholder swing is moving a weapon.
	bool IsSwinging() const { return SwingSlot.IsSet(); }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FHandVisual
	{
		FName WeaponId;
		TWeakObjectPtr<UStaticMeshComponent> FirstPerson;
		TWeakObjectPtr<UStaticMeshComponent> ThirdPerson;
		// The first-person mesh's resting place (the swing returns it there).
		FTransform FirstPersonRest;
	};

	UFUNCTION()
	void OnEquipmentChanged();
	void OnAttackStarted(ACrossguardCharacter* Character, const FCrossguardAttackCue& Cue);

	void RefreshHand(ECrossguardEquipmentSlot Slot, bool bShow);
	void ClearHand(FHandVisual& Visual);
	void EndSwing();
	FHandVisual& Hand(ECrossguardEquipmentSlot Slot) { return Slot == ECrossguardEquipmentSlot::MainHand ? MainHand : OffHand; }
	const FHandVisual& Hand(ECrossguardEquipmentSlot Slot) const { return Slot == ECrossguardEquipmentSlot::MainHand ? MainHand : OffHand; }
	// The first-person arms (tagged), else null.
	USceneComponent* FindFirstPersonArms() const;

	FHandVisual MainHand;
	FHandVisual OffHand;
	FDelegateHandle AttackStartedHandle;

	// The running placeholder swing.
	TOptional<ECrossguardEquipmentSlot> SwingSlot;
	bool bSwingRanged = false;
	float SwingElapsed = 0.0f;
	float SwingDuration = 0.0f;
};
