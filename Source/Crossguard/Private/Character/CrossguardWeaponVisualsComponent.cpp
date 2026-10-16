#include "Character/CrossguardWeaponVisualsComponent.h"

#include "Camera/CameraComponent.h"
#include "Character/CrossguardCharacter.h"
#include "Combat/CrossguardCosmetics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Items/CrossguardEquipmentComponent.h"

namespace CrossguardWeaponVisualsLocal
{
	// Placeholder swing length without a known attack interval, and its limits (seconds).
	constexpr float DefaultSwingSeconds = 0.4f;
	constexpr float MinSwingSeconds = 0.15f;
	constexpr float MaxSwingSeconds = 0.8f;
	// The swing ends a little before the next attack may start.
	constexpr float SwingShareOfInterval = 0.8f;
	// cm the melee swing pushes the weapon forward at its deepest.
	constexpr float SwingReach = 10.0f;
}

UCrossguardWeaponVisualsComponent::UCrossguardWeaponVisualsComponent()
{
	// Ticks only during a placeholder swing.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

void UCrossguardWeaponVisualsComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!CrossguardCosmetics::CanPlay(this))
	{
		return;
	}
	if (UCrossguardEquipmentComponent* Equipment = GetOwner()->FindComponentByClass<UCrossguardEquipmentComponent>())
	{
		Equipment->OnEquipmentChanged.AddDynamic(this, &UCrossguardWeaponVisualsComponent::OnEquipmentChanged);
	}
	if (ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(GetOwner()))
	{
		AttackStartedHandle = Character->OnAttackStartedNative.AddUObject(this, &UCrossguardWeaponVisualsComponent::OnAttackStarted);
	}
	RefreshWeapons();
}

void UCrossguardWeaponVisualsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(GetOwner()))
	{
		Character->OnAttackStartedNative.Remove(AttackStartedHandle);
	}
	ClearHand(MainHand);
	ClearHand(OffHand);
	Super::EndPlay(EndPlayReason);
}

void UCrossguardWeaponVisualsComponent::OnEquipmentChanged()
{
	RefreshWeapons();
}

UStaticMeshComponent* UCrossguardWeaponVisualsComponent::GetWeaponMesh(ECrossguardEquipmentSlot Slot, bool bFirstPerson) const
{
	const FHandVisual& Visual = Hand(Slot);
	return bFirstPerson ? Visual.FirstPerson.Get() : Visual.ThirdPerson.Get();
}

void UCrossguardWeaponVisualsComponent::RefreshWeapons()
{
	const UCrossguardEquipmentComponent* Equipment = GetOwner() != nullptr ? GetOwner()->FindComponentByClass<UCrossguardEquipmentComponent>() : nullptr;
	if (Equipment == nullptr || !CrossguardCosmetics::CanPlay(this))
	{
		return;
	}
	const bool bShow = Equipment->AreWeaponsReady();
	RefreshHand(ECrossguardEquipmentSlot::MainHand, bShow);
	RefreshHand(ECrossguardEquipmentSlot::OffHand, bShow);
	OnWeaponMeshesChanged.Broadcast();
}

USceneComponent* UCrossguardWeaponVisualsComponent::FindFirstPersonArms() const
{
	if (FirstPersonArmsTag.IsNone())
	{
		return nullptr;
	}
	TArray<USkeletalMeshComponent*> Meshes;
	GetOwner()->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh->ComponentHasTag(FirstPersonArmsTag))
		{
			return Mesh;
		}
	}
	return nullptr;
}

void UCrossguardWeaponVisualsComponent::ClearHand(FHandVisual& Visual)
{
	if (SwingSlot.IsSet() && &Hand(SwingSlot.GetValue()) == &Visual)
	{
		EndSwing();
	}
	if (UStaticMeshComponent* FirstPerson = Visual.FirstPerson.Get())
	{
		FirstPerson->DestroyComponent();
	}
	if (UStaticMeshComponent* ThirdPerson = Visual.ThirdPerson.Get())
	{
		ThirdPerson->DestroyComponent();
	}
	Visual = FHandVisual();
}

void UCrossguardWeaponVisualsComponent::RefreshHand(ECrossguardEquipmentSlot Slot, bool bShow)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCrossguardEquipmentComponent* Equipment = GetOwner()->FindComponentByClass<UCrossguardEquipmentComponent>();
	FHandVisual& Visual = Hand(Slot);
	const FName WeaponId = Equipment->GetItem(Slot).WeaponId;

	// Same weapon as before: only its visibility changes (a draw or a holster).
	if (!WeaponId.IsNone() && WeaponId == Visual.WeaponId && (Visual.FirstPerson.IsValid() || Visual.ThirdPerson.IsValid()))
	{
		if (UStaticMeshComponent* FirstPerson = Visual.FirstPerson.Get())
		{
			FirstPerson->SetVisibility(bShow);
		}
		if (UStaticMeshComponent* ThirdPerson = Visual.ThirdPerson.Get())
		{
			ThirdPerson->SetVisibility(bShow);
		}
		return;
	}
	ClearHand(Visual);

	const FCrossguardWeaponRow* Row = Equipment->GetWeaponRow(Slot);
	if (WeaponId.IsNone() || Row == nullptr || Character == nullptr)
	{
		return;
	}
	const TSoftObjectPtr<UStaticMesh>* Override = MeshOverrides.Find(WeaponId);
	const TSoftObjectPtr<UStaticMesh> MeshRef = Override != nullptr ? *Override : Row->Mesh;
	UStaticMesh* Mesh = MeshRef.IsNull() ? nullptr : MeshRef.LoadSynchronous();
	if (Mesh == nullptr)
	{
		return;
	}
	const FName Socket = !Row->AttachSocket.IsNone() ? Row->AttachSocket
		: Slot == ECrossguardEquipmentSlot::MainHand ? MainHandSocket : OffHandSocket;
	const FTransform OnSocket(Row->MeshRotation, Row->MeshOffset, Row->MeshScale);

	const auto MakeMesh = [this, Mesh](const TCHAR* Suffix, ECrossguardEquipmentSlot ForSlot)
	{
		const FString Name = FString::Printf(TEXT("WeaponMesh_%s_%s"), ForSlot == ECrossguardEquipmentSlot::MainHand ? TEXT("Main") : TEXT("Off"), Suffix);
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), UStaticMeshComponent::StaticClass(), FName(*Name)));
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->RegisterComponent();
		return Component;
	};

	// Third person: on the body's hand, for everyone but its own player (the body is hidden from them).
	if (USkeletalMeshComponent* Body = Character->GetMesh())
	{
		UStaticMeshComponent* ThirdPerson = MakeMesh(TEXT("TP"), Slot);
		ThirdPerson->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		ThirdPerson->SetRelativeTransform(OnSocket);
		ThirdPerson->SetOwnerNoSee(true);
		ThirdPerson->SetVisibility(bShow);
		Visual.ThirdPerson = ThirdPerson;
	}

	// First person: on the arms' hand, else in front of the camera; only its own player sees it.
	USceneComponent* Arms = FindFirstPersonArms();
	UCameraComponent* Camera = Arms == nullptr ? GetOwner()->FindComponentByClass<UCameraComponent>() : nullptr;
	if (Arms != nullptr || Camera != nullptr)
	{
		UStaticMeshComponent* FirstPerson = MakeMesh(TEXT("FP"), Slot);
		if (Arms != nullptr)
		{
			FirstPerson->AttachToComponent(Arms, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
			FirstPerson->SetRelativeTransform(OnSocket);
		}
		else
		{
			const FVector Offset = Slot == ECrossguardEquipmentSlot::MainHand ? FirstPersonCameraOffset : FirstPersonCameraOffsetOffHand;
			FirstPerson->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			FirstPerson->SetRelativeTransform(FTransform(Row->MeshRotation, Offset, Row->MeshScale));
		}
		FirstPerson->SetOnlyOwnerSee(true);
		FirstPerson->SetCastShadow(false);
		FirstPerson->SetVisibility(bShow);
		Visual.FirstPerson = FirstPerson;
		Visual.FirstPersonRest = FirstPerson->GetRelativeTransform();
	}
	Visual.WeaponId = WeaponId;
}

void UCrossguardWeaponVisualsComponent::OnAttackStarted(ACrossguardCharacter* Character, const FCrossguardAttackCue& Cue)
{
	using namespace CrossguardWeaponVisualsLocal;
	if (!bPlaceholderSwing || (Cue.Kind != ECrossguardAttackKind::Melee && Cue.Kind != ECrossguardAttackKind::Ranged))
	{
		return;
	}
	const UStaticMeshComponent* FirstPerson = Hand(Cue.Slot).FirstPerson.Get();
	if (FirstPerson == nullptr || !FirstPerson->IsVisible())
	{
		return;
	}
	EndSwing();
	SwingSlot = Cue.Slot;
	bSwingRanged = Cue.Kind == ECrossguardAttackKind::Ranged;
	SwingElapsed = 0.0f;
	SwingDuration = FMath::Clamp(Cue.Duration > 0.0f ? Cue.Duration * SwingShareOfInterval : DefaultSwingSeconds, MinSwingSeconds, MaxSwingSeconds);
	SetComponentTickEnabled(true);
}

void UCrossguardWeaponVisualsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	using namespace CrossguardWeaponVisualsLocal;
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!SwingSlot.IsSet())
	{
		SetComponentTickEnabled(false);
		return;
	}
	const FHandVisual& Visual = Hand(SwingSlot.GetValue());
	UStaticMeshComponent* FirstPerson = Visual.FirstPerson.Get();
	SwingElapsed += DeltaTime;
	if (FirstPerson == nullptr || SwingElapsed >= SwingDuration)
	{
		EndSwing();
		return;
	}
	// Out and back: 0 -> 1 -> 0 over the swing.
	const float Wave = FMath::Sin(SwingElapsed / SwingDuration * PI);
	FTransform Pose = Visual.FirstPersonRest;
	if (bSwingRanged)
	{
		Pose.AddToTranslation(FVector(-PlaceholderRecoil * Wave, 0.0f, 0.0f));
	}
	else
	{
		// Down and forward through an arc, turned in the parent's frame (the camera's: pitch down).
		Pose.SetRotation(FQuat(FRotator(-PlaceholderSwingDegrees * Wave, 0.0f, 0.0f)) * Visual.FirstPersonRest.GetRotation());
		Pose.AddToTranslation(FVector(SwingReach * Wave, 0.0f, 0.0f));
	}
	FirstPerson->SetRelativeTransform(Pose);
}

void UCrossguardWeaponVisualsComponent::EndSwing()
{
	if (SwingSlot.IsSet())
	{
		const FHandVisual& Visual = Hand(SwingSlot.GetValue());
		if (UStaticMeshComponent* FirstPerson = Visual.FirstPerson.Get())
		{
			FirstPerson->SetRelativeTransform(Visual.FirstPersonRest);
		}
	}
	SwingSlot.Reset();
	SetComponentTickEnabled(false);
}
