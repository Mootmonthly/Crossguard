#include "Skills/CrossguardAuraSkills.h"

#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Character/CrossguardAICharacter.h"
#include "Character/CrossguardCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardTeamLibrary.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "TimerManager.h"

namespace
{
	// Data/Skills/Skills.json.
	constexpr float MultiscaleResistance = 0.15f;
	constexpr float MultiscaleRefreshSeconds = 1.0f;
	// Assumption: a monster's aura reaches monsters this close (cm), not every monster in the world.
	constexpr float MonsterAuraRange = 3000.0f;
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
		PlateHandle = ActorInfo->AbilitySystemComponent->RegisterGameplayTagEvent(CrossguardTags::Equipment_Armor_Plate, EGameplayTagEventType::NewOrRemoved)
			.AddWeakLambda(this, Refresh);
		// Plate in every slot for [The Mithril Knight]'s Multiscale.
		if (UCrossguardEquipmentComponent* Equipment = ActorInfo->AvatarActor.IsValid()
				? ActorInfo->AvatarActor->FindComponentByClass<UCrossguardEquipmentComponent>()
				: nullptr)
		{
			Equipment->OnEquipmentChanged.AddUniqueDynamic(this, &UCrossguardMultiscaleAbility::OnOwnerEquipmentChanged);
		}
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
	// Off while Muted or dead, without Plate armor (Skills.json requirement), and while the bundle it
	// comes with lacks its requirement ([The Mithril Knight]: Plate in every slot). An AI's skills have
	// no requirements.
	if (ASC->HasMatchingGameplayTag(CrossguardTags::State_Muted) || ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead)
		|| (!ASC->HasMatchingGameplayTag(CrossguardTags::Equipment_Armor_Plate) && !Avatar->IsA<ACrossguardAICharacter>())
		|| !IsSwitchedOnByLoadout(Avatar, SkillId))
	{
		RemoveAll();
		return;
	}

	// Everyone on the owner's side: the owner, party members, party minions.
	TSet<UAbilitySystemComponent*> Wanted;
	for (TActorIterator<ACrossguardCharacter> It(Avatar->GetWorld()); It; ++It)
	{
		ACrossguardCharacter* Candidate = *It;
		// Monsters are all one team: theirs reaches only those near them.
		const bool bInReach = UCrossguardTeamLibrary::GetTeamId(Avatar) != UCrossguardTeamLibrary::MonsterTeamId
			|| FVector::Dist(Avatar->GetActorLocation(), Candidate->GetActorLocation()) <= MonsterAuraRange;
		if (Candidate == Avatar || (UCrossguardTeamLibrary::AreAllies(Avatar, Candidate) && bInReach))
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

void UCrossguardMultiscaleAbility::OnOwnerEquipmentChanged()
{
	RefreshAura();
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
		ASC->RegisterGameplayTagEvent(CrossguardTags::Equipment_Armor_Plate, EGameplayTagEventType::NewOrRemoved).Remove(PlateHandle);
	}
	if (UCrossguardEquipmentComponent* Equipment = ActorInfo != nullptr && ActorInfo->AvatarActor.IsValid()
			? ActorInfo->AvatarActor->FindComponentByClass<UCrossguardEquipmentComponent>()
			: nullptr)
	{
		Equipment->OnEquipmentChanged.RemoveDynamic(this, &UCrossguardMultiscaleAbility::OnOwnerEquipmentChanged);
	}
	MutedHandle.Reset();
	DeadHandle.Reset();
	PlateHandle.Reset();
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
