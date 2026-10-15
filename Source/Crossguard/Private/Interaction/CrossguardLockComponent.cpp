#include "Interaction/CrossguardLockComponent.h"

#include "Net/UnrealNetwork.h"

UCrossguardLockComponent::UCrossguardLockComponent()
{
	SetIsReplicatedByDefault(true);
}

void UCrossguardLockComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCrossguardLockComponent, bLocked);
}

void UCrossguardLockComponent::SetLocked(bool bNewLocked)
{
	if (GetOwner() != nullptr && GetOwner()->HasAuthority() && bLocked != bNewLocked)
	{
		bLocked = bNewLocked;
		OnLockChanged.Broadcast();
	}
}

void UCrossguardLockComponent::OnRep_Locked()
{
	OnLockChanged.Broadcast();
}
