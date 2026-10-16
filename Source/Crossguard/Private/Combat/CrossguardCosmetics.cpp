#include "Combat/CrossguardCosmetics.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

void FCrossguardProjectileVisual::MergeFrom(const FCrossguardProjectileVisual& Other)
{
	if (!Other.Mesh.IsNull())
	{
		Mesh = Other.Mesh;
		MeshScale = Other.MeshScale;
		MeshRotation = Other.MeshRotation;
	}
	if (!Other.TrailVfx.IsNull())
	{
		TrailVfx = Other.TrailVfx;
	}
	if (!Other.ImpactVfx.IsNull())
	{
		ImpactVfx = Other.ImpactVfx;
	}
	if (!Other.ImpactSound.IsNull())
	{
		ImpactSound = Other.ImpactSound;
	}
}

namespace CrossguardCosmetics
{
	bool CanPlay(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext != nullptr ? WorldContext->GetWorld() : nullptr;
		return World != nullptr && World->GetNetMode() != NM_DedicatedServer;
	}

	void PlaySoundAt(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location)
	{
		if (Sound.IsNull() || !CanPlay(WorldContext))
		{
			return;
		}
		if (USoundBase* Loaded = Sound.LoadSynchronous())
		{
			UGameplayStatics::PlaySoundAtLocation(WorldContext, Loaded, Location);
		}
	}

	void SpawnFxAt(const UObject* WorldContext, const TSoftObjectPtr<UNiagaraSystem>& System, const FVector& Location, const FRotator& Rotation)
	{
		if (System.IsNull() || !CanPlay(WorldContext))
		{
			return;
		}
		if (UNiagaraSystem* Loaded = System.LoadSynchronous())
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, Loaded, Location, Rotation);
		}
	}

	UNiagaraComponent* SpawnFxAttached(const TSoftObjectPtr<UNiagaraSystem>& System, USceneComponent* Parent)
	{
		if (System.IsNull() || Parent == nullptr || !CanPlay(Parent))
		{
			return nullptr;
		}
		UNiagaraSystem* Loaded = System.LoadSynchronous();
		return Loaded != nullptr ? UNiagaraFunctionLibrary::SpawnSystemAttached(Loaded, Parent, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, true) : nullptr;
	}
}
