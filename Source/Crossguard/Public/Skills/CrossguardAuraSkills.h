#pragma once

#include "AbilitySystem/CrossguardPassiveAbility.h"
#include "CoreMinimal.h"

#include "CrossguardAuraSkills.generated.h"

class UAbilitySystemComponent;

// [Multiscale] (Passive): you and every party member (and party minions) have +15% Attack and Magic
// Resistance, wherever they are. Two Multiscales do not stack (State.Multiscale). Off while you are
// Muted or dead. Also part of [The Mithril Knight]. Server: rechecked every second (joins, deaths, Mute).
UCLASS()
class CROSSGUARD_API UCrossguardMultiscaleAbility : public UCrossguardPassiveAbility
{
	GENERATED_BODY()

public:
	UCrossguardMultiscaleAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void RefreshAura();
	void RemoveAll();

	FTimerHandle RefreshTimer;
	TMap<TWeakObjectPtr<UAbilitySystemComponent>, FActiveGameplayEffectHandle> Applied;
	FDelegateHandle MutedHandle;
	FDelegateHandle DeadHandle;
};

// [Hunter's Tracking] (Passive): see enemy players' footsteps from the last 30 s
// (UCrossguardTrackingComponent on your screen only). Off while Muted or dead. The extra hearing range
// waits for the sound system.
UCLASS()
class CROSSGUARD_API UCrossguardHuntersTrackingAbility : public UCrossguardPassiveAbility
{
	GENERATED_BODY()

public:
	UCrossguardHuntersTrackingAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void OnMutedChanged(const FGameplayTag Tag, int32 Count);
	// The marker effect carries Passive.HuntersTracking to the owner's client, where the footprints
	// are drawn.
	void UpdateMarker();

	FActiveGameplayEffectHandle MarkerHandle;
	FDelegateHandle MutedHandle;
	FDelegateHandle DeadHandle;
};
