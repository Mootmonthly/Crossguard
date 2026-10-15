#include "Skills/CrossguardAuraSkills.h"

#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Character/CrossguardCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardTeamLibrary.h"
#include "TimerManager.h"

namespace
{
	// Data/Skills/Skills.json.
	constexpr float MultiscaleResistance = 0.15f;
	constexpr float MultiscaleRefreshSeconds = 1.0f;
}

// --- Multiscale -----------------------------------------------------------------------------------

UCrossguardMultiscaleAbility::UCrossguardMultiscaleAbility()
{
	SkillId = TEXT("multiscale");
}

void UCrossguardMultiscaleAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (IsActive() && ActorInfo->IsNetAuthority())
	{
		// Mute and death switch it off at once; the timer covers joins and leaves.
		const auto Refresh = [this](const FGameplayTag, int32) { RefreshAura(); };
		MutedHandle = ActorInfo->AbilitySystemComponent->RegisterGameplayTagEvent(CrossguardTags::State_Muted, EGameplayTagEventType::NewOrRemoved)
			.AddWeakLambda(this, Refresh);
		DeadHandle = ActorInfo->AbilitySystemComponent->RegisterGameplayTagEvent(CrossguardTags::State_Dead, EGameplayTagEventType::NewOrRemoved)
			.AddWeakLambda(this, Refresh);
		RefreshAura();
		GetWorld()->GetTimerManager().SetTimer(RefreshTimer, FTimerDelegate::CreateUObject(this, &UCrossguardMultiscaleAbility::RefreshAura),
			MultiscaleRefreshSeconds, true);
	}
}

void UCrossguardMultiscaleAbility::RefreshAura()
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (Avatar == nullptr || ASC == nullptr)
	{
		return;
	}
	if (ASC->HasMatchingGameplayTag(CrossguardTags::State_Muted) || ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		RemoveAll();
		return;
	}

	// Everyone on the owner's side: the owner, party members, party minions.
	TSet<UAbilitySystemComponent*> Wanted;
	for (TActorIterator<ACrossguardCharacter> It(Avatar->GetWorld()); It; ++It)
	{
		ACrossguardCharacter* Candidate = *It;
		if (Candidate == Avatar || UCrossguardTeamLibrary::AreAllies(Avatar, Candidate))
		{
			Wanted.Add(Candidate->GetAbilitySystemComponent());
		}
	}

	// Gone or no longer on the side: remove.
	for (auto It = Applied.CreateIterator(); It; ++It)
	{
		UAbilitySystemComponent* Target = It->Key.Get();
		// Removed by something else: forget it so it is applied again below.
		if (Target != nullptr && Target->GetActiveGameplayEffect(It->Value) == nullptr)
		{
			It.RemoveCurrent();
			continue;
		}
		if (Target == nullptr || !Wanted.Contains(Target))
		{
			if (Target != nullptr)
			{
				Target->RemoveActiveGameplayEffect(It->Value);
			}
			It.RemoveCurrent();
		}
	}

	for (UAbilitySystemComponent* Target : Wanted)
	{
		// Already ours, or another Multiscale covers them (no stacking).
		if (Target == nullptr || Applied.Contains(Target) || Target->HasMatchingGameplayTag(CrossguardTags::State_Multiscale))
		{
			continue;
		}
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCrossguardSkillStatsEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
		if (!Spec.IsValid())
		{
			continue;
		}
		for (const FName& Name : UCrossguardSkillStatsEffect::GetStatNames())
		{
			Spec.Data->SetSetByCallerMagnitude(Name, 0.0f);
		}
		Spec.Data->SetSetByCallerMagnitude(FName(TEXT("AttackResistance")), MultiscaleResistance);
		Spec.Data->SetSetByCallerMagnitude(FName(TEXT("MagicResistance")), MultiscaleResistance);
		Spec.Data->DynamicGrantedTags.AddTag(CrossguardTags::State_Multiscale);
		Applied.Add(Target, Target->ApplyGameplayEffectSpecToSelf(*Spec.Data));
	}
}

void UCrossguardMultiscaleAbility::RemoveAll()
{
	for (const TPair<TWeakObjectPtr<UAbilitySystemComponent>, FActiveGameplayEffectHandle>& Pair : Applied)
	{
		if (UAbilitySystemComponent* Target = Pair.Key.Get())
		{
			Target->RemoveActiveGameplayEffect(Pair.Value);
		}
	}
	Applied.Reset();
}

void UCrossguardMultiscaleAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	if (UAbilitySystemComponent* ASC = ActorInfo != nullptr ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		ASC->RegisterGameplayTagEvent(CrossguardTags::State_Muted, EGameplayTagEventType::NewOrRemoved).Remove(MutedHandle);
		ASC->RegisterGameplayTagEvent(CrossguardTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeadHandle);
	}
	MutedHandle.Reset();
	DeadHandle.Reset();
	RemoveAll();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// --- Hunter's Tracking ----------------------------------------------------------------------------

UCrossguardHuntersTrackingAbility::UCrossguardHuntersTrackingAbility()
{
	SkillId = TEXT("hunters_tracking");
}

void UCrossguardHuntersTrackingAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (IsActive() && ASC != nullptr)
	{
		MutedHandle = ASC->RegisterGameplayTagEvent(CrossguardTags::State_Muted, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UCrossguardHuntersTrackingAbility::OnMutedChanged);
		DeadHandle = ASC->RegisterGameplayTagEvent(CrossguardTags::State_Dead, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UCrossguardHuntersTrackingAbility::OnMutedChanged);
		UpdateMarker();
	}
}

void UCrossguardHuntersTrackingAbility::OnMutedChanged(const FGameplayTag Tag, int32 Count)
{
	UpdateMarker();
}

void UCrossguardHuntersTrackingAbility::UpdateMarker()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC == nullptr)
	{
		return;
	}
	const bool bWanted = IsActive() && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Muted)
		&& !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
	if (bWanted && !MarkerHandle.IsValid())
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCrossguardMarkerEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
		if (Spec.IsValid())
		{
			Spec.Data->DynamicGrantedTags.AddTag(CrossguardTags::Passive_HuntersTracking);
			MarkerHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		}
	}
	else if (!bWanted && MarkerHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(MarkerHandle);
		MarkerHandle.Invalidate();
	}
}

void UCrossguardHuntersTrackingAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UAbilitySystemComponent* ASC = ActorInfo != nullptr ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		ASC->RegisterGameplayTagEvent(CrossguardTags::State_Muted, EGameplayTagEventType::NewOrRemoved).Remove(MutedHandle);
		ASC->RegisterGameplayTagEvent(CrossguardTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeadHandle);
		if (MarkerHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(MarkerHandle);
		}
	}
	MutedHandle.Reset();
	DeadHandle.Reset();
	MarkerHandle.Invalidate();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
