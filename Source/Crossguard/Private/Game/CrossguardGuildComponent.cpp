#include "Game/CrossguardGuildComponent.h"

#include "Crossguard.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Net/UnrealNetwork.h"

namespace
{
	UCrossguardGuildSubsystem* Guilds(const UActorComponent& Component)
	{
		const UWorld* World = Component.GetWorld();
		UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardGuildSubsystem>() : nullptr;
	}
}

UCrossguardGuildComponent::UCrossguardGuildComponent()
{
	SetIsReplicatedByDefault(true);
}

void UCrossguardGuildComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Name and rank for everyone (nameplates); the rest for the owner only.
	DOREPLIFETIME(UCrossguardGuildComponent, GuildId);
	DOREPLIFETIME(UCrossguardGuildComponent, GuildName);
	DOREPLIFETIME(UCrossguardGuildComponent, Rank);
	DOREPLIFETIME_CONDITION(UCrossguardGuildComponent, Members, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardGuildComponent, Invites, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardGuildComponent, BankGold, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UCrossguardGuildComponent, BankItems, COND_OwnerOnly);
}

void UCrossguardGuildComponent::BeginPlay()
{
	Super::BeginPlay();
	// Joining (or travelling to) a server: the guild it already knows you in.
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		if (UCrossguardGuildSubsystem* Subsystem = Guilds(*this))
		{
			Subsystem->RefreshPlayer(Cast<ACrossguardPlayerState>(GetOwner()));
		}
	}
}

void UCrossguardGuildComponent::Refuse(ECrossguardGuildResult Result)
{
	UE_LOG(LogCrossguard, Log, TEXT("Guild request from %s refused: %s"), *GetNameSafe(GetOwner()),
		*StaticEnum<ECrossguardGuildResult>()->GetNameStringByValue(static_cast<int64>(Result)));
	ClientRefused(Result);
}

void UCrossguardGuildComponent::ClientRefused_Implementation(ECrossguardGuildResult Result)
{
	OnGuildRefused.Broadcast(Result);
}

void UCrossguardGuildComponent::OnRep_Guild()
{
	// Until a guild window exists.
	UE_LOG(LogCrossguard, Display, TEXT("Guild: %s (%s), %d members, %d invites, bank %lld gold and %d stacks"),
		GuildName.IsEmpty() ? TEXT("none") : *GuildName, *StaticEnum<ECrossguardGuildRank>()->GetNameStringByValue(static_cast<int64>(Rank)),
		Members.Num(), Invites.Num(), BankGold, BankItems.Num());
	OnGuildChanged.Broadcast();
}

// Every request: run it on the subsystem, refuse with the result if it failed.
#define CROSSGUARD_GUILD_REQUEST(Call) \
	if (UCrossguardGuildSubsystem* Subsystem = Guilds(*this)) \
	{ \
		const ECrossguardGuildResult Result = Subsystem->Call; \
		if (Result != ECrossguardGuildResult::None) \
		{ \
			Refuse(Result); \
		} \
	}

void UCrossguardGuildComponent::ServerCreate_Implementation(const FString& Name)
{
	CROSSGUARD_GUILD_REQUEST(Create(Cast<ACrossguardPlayerState>(GetOwner()), Name))
}

void UCrossguardGuildComponent::ServerInvite_Implementation(APlayerState* Invitee)
{
	CROSSGUARD_GUILD_REQUEST(Invite(Cast<ACrossguardPlayerState>(GetOwner()), Cast<ACrossguardPlayerState>(Invitee)))
}

void UCrossguardGuildComponent::ServerAcceptInvite_Implementation(const FString& InGuildId)
{
	CROSSGUARD_GUILD_REQUEST(Accept(Cast<ACrossguardPlayerState>(GetOwner()), InGuildId))
}

void UCrossguardGuildComponent::ServerDeclineInvite_Implementation(const FString& InGuildId)
{
	CROSSGUARD_GUILD_REQUEST(Decline(Cast<ACrossguardPlayerState>(GetOwner()), InGuildId))
}

void UCrossguardGuildComponent::ServerKick_Implementation(const FString& PlayerId)
{
	CROSSGUARD_GUILD_REQUEST(Kick(Cast<ACrossguardPlayerState>(GetOwner()), PlayerId))
}

void UCrossguardGuildComponent::ServerSetRank_Implementation(const FString& PlayerId, ECrossguardGuildRank NewRank)
{
	CROSSGUARD_GUILD_REQUEST(SetRank(Cast<ACrossguardPlayerState>(GetOwner()), PlayerId, NewRank))
}

void UCrossguardGuildComponent::ServerLeave_Implementation()
{
	CROSSGUARD_GUILD_REQUEST(Leave(Cast<ACrossguardPlayerState>(GetOwner())))
}

void UCrossguardGuildComponent::ServerMoveGold_Implementation(int32 Amount)
{
	CROSSGUARD_GUILD_REQUEST(MoveGold(Cast<ACrossguardPlayerState>(GetOwner()), Amount))
}

void UCrossguardGuildComponent::ServerDepositItem_Implementation(int32 InventoryItemId)
{
	CROSSGUARD_GUILD_REQUEST(DepositItem(Cast<ACrossguardPlayerState>(GetOwner()), InventoryItemId))
}

void UCrossguardGuildComponent::ServerWithdrawItem_Implementation(int32 SlotId)
{
	CROSSGUARD_GUILD_REQUEST(WithdrawItem(Cast<ACrossguardPlayerState>(GetOwner()), SlotId))
}

void UCrossguardGuildComponent::ServerGiveGold_Implementation(const FString& PlayerId, int32 Amount)
{
	CROSSGUARD_GUILD_REQUEST(GiveGold(Cast<ACrossguardPlayerState>(GetOwner()), PlayerId, Amount))
}

void UCrossguardGuildComponent::ServerGiveItem_Implementation(const FString& PlayerId, int32 SlotId)
{
	CROSSGUARD_GUILD_REQUEST(GiveItem(Cast<ACrossguardPlayerState>(GetOwner()), PlayerId, SlotId))
}

#undef CROSSGUARD_GUILD_REQUEST
