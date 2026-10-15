#include "Items/CrossguardTradeComponent.h"

#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Crossguard.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	constexpr float TradeCheckSeconds = 0.5f;

	const Crossguard::FTradeConfig& TradeConfig()
	{
		static const Crossguard::FTradeConfig Config;
		return Config;
	}

	UCrossguardTradeComponent* TradeOf(const AActor* Actor)
	{
		return Actor != nullptr ? Actor->FindComponentByClass<UCrossguardTradeComponent>() : nullptr;
	}
}

UCrossguardTradeComponent::UCrossguardTradeComponent()
{
	SetIsReplicatedByDefault(true);
}

void UCrossguardTradeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// The offers are nobody else's business.
	DOREPLIFETIME_CONDITION(UCrossguardTradeComponent, View, COND_OwnerOnly);
}

void UCrossguardTradeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetOwner() != nullptr && GetOwner()->HasAuthority() && View.State != ECrossguardTradeState::None)
	{
		EndTrade(ECrossguardTradeEnd::PartnerGone);
	}
	Super::EndPlay(EndPlayReason);
}

// --- Client requests ------------------------------------------------------------------------------

void UCrossguardTradeComponent::RequestTrade(APawn* Target) { ServerRequestTrade(Target); }
void UCrossguardTradeComponent::RespondToTrade(bool bAccept) { ServerRespondToTrade(bAccept); }
void UCrossguardTradeComponent::RequestSetOffer(const TArray<int32>& ItemIds, int32 Gold) { ServerSetOffer(ItemIds, Gold); }
void UCrossguardTradeComponent::RequestLock(bool bLocked) { ServerLock(bLocked); }
void UCrossguardTradeComponent::RequestAccept() { ServerAccept(); }
void UCrossguardTradeComponent::RequestCancel() { ServerCancel(); }

// --- Server ---------------------------------------------------------------------------------------

void UCrossguardTradeComponent::ServerRequestTrade_Implementation(APawn* Target)
{
	UCrossguardTradeComponent* Other = TradeOf(Target);
	const APawn* Self = Cast<APawn>(GetOwner());
	// Players only (monsters and minions carry the component as characters, but do not trade).
	if (Other == nullptr || Self == nullptr || !Self->IsPlayerControlled() || !Target->IsPlayerControlled())
	{
		return;
	}
	const double Distance = FVector::Dist(GetOwner()->GetActorLocation(), Target->GetActorLocation());
	const Crossguard::ETradeError Error = Crossguard::CanStartTrade(TradeConfig(), Other == this, Distance,
		IsOwnerAlive() && Other->IsOwnerAlive(), View.State != ECrossguardTradeState::None || Other->View.State != ECrossguardTradeState::None);
	if (Error != Crossguard::ETradeError::None)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Trade request %s -> %s refused: %d"), *GetNameSafe(GetOwner()), *GetNameSafe(Target), static_cast<int32>(Error));
		return;
	}
	Partner = Other;
	Other->Partner = this;
	Side = 0;
	Other->Side = 1;
	RequestTime = GetWorld()->GetTimeSeconds();
	Other->RequestTime = RequestTime;
	View.State = ECrossguardTradeState::RequestSent;
	View.Partner = Target;
	Other->View.State = ECrossguardTradeState::RequestReceived;
	Other->View.Partner = Cast<APawn>(GetOwner());
	StartChecks();
	// A listen-server host gets no OnRep.
	for (const UCrossguardTradeComponent* Trader : {static_cast<const UCrossguardTradeComponent*>(this), static_cast<const UCrossguardTradeComponent*>(Other)})
	{
		if (const APawn* Pawn = Cast<APawn>(Trader->GetOwner()); Pawn != nullptr && Pawn->IsLocallyControlled())
		{
			Trader->LogView();
		}
	}
	OnTradeChanged.Broadcast();
	Other->OnTradeChanged.Broadcast();
}

void UCrossguardTradeComponent::ServerRespondToTrade_Implementation(bool bAccept)
{
	UCrossguardTradeComponent* Other = Partner.Get();
	if (View.State != ECrossguardTradeState::RequestReceived || Other == nullptr)
	{
		return;
	}
	if (!bAccept)
	{
		EndTrade(ECrossguardTradeEnd::Declined);
		return;
	}
	// One session, shared by both sides.
	Session = MakeShared<Crossguard::FTradeSession>();
	Other->Session = Session;
	View.State = ECrossguardTradeState::Open;
	Other->View.State = ECrossguardTradeState::Open;
	RefreshViews();
}

void UCrossguardTradeComponent::ServerSetOffer_Implementation(const TArray<int32>& ItemIds, int32 Gold)
{
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	if (View.State != ECrossguardTradeState::Open || !Session.IsValid() || Inventory == nullptr)
	{
		return;
	}
	Crossguard::FTradeOffer Offer;
	Offer.Gold = Gold;
	TArray<FCrossguardInventoryItem> Stacks;
	for (const int32 ItemId : ItemIds)
	{
		// Only stacks actually in the inventory (not worn, not in hand).
		FCrossguardInventoryItem Stack;
		if (!Inventory->FindItem(ItemId, Stack) || ItemId == Inventory->GetReadiedItemId())
		{
			return;
		}
		Offer.ItemIds.push_back(ItemId);
		Stacks.Add(Stack);
	}
	const Crossguard::ETradeError Error = Session->SetOffer(TradeConfig(), Side, Offer, GetBankedGold(), IsInRaid());
	if (Error != Crossguard::ETradeError::None)
	{
		UE_LOG(LogCrossguard, Log, TEXT("Trade offer from %s refused: %d"), *GetNameSafe(GetOwner()), static_cast<int32>(Error));
		return;
	}
	OfferedStacks = Stacks;
	RefreshViews();
}

void UCrossguardTradeComponent::ServerLock_Implementation(bool bLocked)
{
	if (View.State != ECrossguardTradeState::Open || !Session.IsValid())
	{
		return;
	}
	bLocked ? Session->Lock(Side) : Session->Unlock(Side);
	RefreshViews();
}

void UCrossguardTradeComponent::ServerAccept_Implementation()
{
	if (View.State != ECrossguardTradeState::Open || !Session.IsValid() || Session->Accept(Side) != Crossguard::ETradeError::None)
	{
		return;
	}
	if (Session->IsComplete())
	{
		Complete();
		return;
	}
	RefreshViews();
}

void UCrossguardTradeComponent::ServerCancel_Implementation()
{
	if (View.State != ECrossguardTradeState::None)
	{
		EndTrade(View.State == ECrossguardTradeState::RequestReceived ? ECrossguardTradeEnd::Declined : ECrossguardTradeEnd::Cancelled);
	}
}

void UCrossguardTradeComponent::Complete()
{
	UCrossguardTradeComponent* Other = Partner.Get();
	UCrossguardInventoryComponent* MyInventory = GetInventory();
	UCrossguardInventoryComponent* TheirInventory = Other != nullptr ? Other->GetInventory() : nullptr;
	if (Other == nullptr || MyInventory == nullptr || TheirInventory == nullptr)
	{
		EndTrade(ECrossguardTradeEnd::PartnerGone);
		return;
	}
	// What CheckTrade watches, checked again now: an accept can land between checks.
	if (!IsOwnerAlive() || !Other->IsOwnerAlive())
	{
		EndTrade(ECrossguardTradeEnd::PartnerGone);
		return;
	}
	if (FVector::Dist(GetOwner()->GetActorLocation(), Other->GetOwner()->GetActorLocation()) > TradeConfig().MaxDistanceCm)
	{
		EndTrade(ECrossguardTradeEnd::TooFar);
		return;
	}
	// A stack changed since it was offered: back to looking, not a swap of something else.
	if (!OfferUnchanged() || !Other->OfferUnchanged())
	{
		ResetAfterChange();
		return;
	}
	const auto ToArray = [](const std::vector<int>& Ids)
	{
		TArray<int32> Out;
		for (const int Id : Ids)
		{
			Out.Add(Id);
		}
		return Out;
	};
	const Crossguard::FTradeOffer& MyOffer = Session->GetOffer(Side);
	const Crossguard::FTradeOffer& TheirOffer = Session->GetOffer(Other->Side);
	const TArray<int32> MyIds = ToArray(MyOffer.ItemIds);
	const TArray<int32> TheirIds = ToArray(TheirOffer.ItemIds);

	// The stacks as they are now (each side's dry run reports them), then both checked before either
	// changes: all or nothing.
	TArray<FCrossguardInventoryItem> MyItems;
	TArray<FCrossguardInventoryItem> TheirItems;
	const bool bMineThere = MyInventory->ExchangeItems(MyIds, {}, false, &MyItems);
	const bool bTheirsThere = TheirInventory->ExchangeItems(TheirIds, {}, false, &TheirItems);
	ACrossguardPlayerState* MyState = Cast<APawn>(GetOwner()) != nullptr ? Cast<APawn>(GetOwner())->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	ACrossguardPlayerState* TheirState = Cast<APawn>(Other->GetOwner()) != nullptr ? Cast<APawn>(Other->GetOwner())->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	// Gold still banked, and room for it on the other side (banked gold is an int32).
	const bool bAnyGold = MyOffer.Gold > 0 || TheirOffer.Gold > 0;
	const bool bGold = MyOffer.Gold <= GetBankedGold() && TheirOffer.Gold <= Other->GetBankedGold()
		&& (!bAnyGold || (MyState != nullptr && TheirState != nullptr && TheirState->CanReceiveGold(FMath::Max<int64>(MyOffer.Gold - TheirOffer.Gold, 0))
			&& MyState->CanReceiveGold(FMath::Max<int64>(TheirOffer.Gold - MyOffer.Gold, 0))));
	if (!bMineThere || !bTheirsThere || !bGold || !MyInventory->ExchangeItems(MyIds, TheirItems, false)
		|| !TheirInventory->ExchangeItems(TheirIds, MyItems, false))
	{
		EndTrade(ECrossguardTradeEnd::NoRoom);
		return;
	}
	MyInventory->ExchangeItems(MyIds, TheirItems, true);
	TheirInventory->ExchangeItems(TheirIds, MyItems, true);

	if (MyState != nullptr && TheirState != nullptr)
	{
		if (MyOffer.Gold > 0 && MyState->SpendGold(static_cast<int32>(MyOffer.Gold)))
		{
			TheirState->AddGold(static_cast<int32>(MyOffer.Gold));
		}
		if (TheirOffer.Gold > 0 && TheirState->SpendGold(static_cast<int32>(TheirOffer.Gold)))
		{
			MyState->AddGold(static_cast<int32>(TheirOffer.Gold));
		}
	}
	UE_LOG(LogCrossguard, Log, TEXT("Trade %s <-> %s: %d stacks + %lld gold for %d stacks + %lld gold"), *GetNameSafe(GetOwner()),
		*GetNameSafe(Other->GetOwner()), MyItems.Num(), static_cast<int64>(MyOffer.Gold), TheirItems.Num(), static_cast<int64>(TheirOffer.Gold));
	EndTrade(ECrossguardTradeEnd::Completed);
}

void UCrossguardTradeComponent::RefreshViews(bool bLog)
{
	UCrossguardTradeComponent* Other = Partner.Get();
	if (Other == nullptr || !Session.IsValid())
	{
		return;
	}
	for (UCrossguardTradeComponent* Trader : {this, Other})
	{
		const UCrossguardTradeComponent* Them = Trader == this ? Other : this;
		const Crossguard::FTradeOffer& Mine = Session->GetOffer(Trader->Side);
		const Crossguard::FTradeOffer& Theirs = Session->GetOffer(Them->Side);
		FCrossguardTradeView& Out = Trader->View;
		Out.MyItemIds.Reset();
		for (const int Id : Mine.ItemIds)
		{
			Out.MyItemIds.Add(Id);
		}
		Out.MyGold = static_cast<int32>(Mine.Gold);
		// Copies of their stacks, so the item details show.
		Out.TheirItems.Reset();
		if (const UCrossguardInventoryComponent* TheirInventory = Them->GetInventory())
		{
			for (const int Id : Theirs.ItemIds)
			{
				FCrossguardInventoryItem Item;
				if (TheirInventory->FindItem(Id, Item))
				{
					Out.TheirItems.Add(Item);
				}
			}
		}
		Out.TheirGold = static_cast<int32>(Theirs.Gold);
		Out.bMyLocked = Session->IsLocked(Trader->Side);
		Out.bTheirLocked = Session->IsLocked(Them->Side);
		Out.bMyAccepted = Session->HasAccepted(Trader->Side);
		Out.bTheirAccepted = Session->HasAccepted(Them->Side);
		// A listen-server host gets no OnRep.
		if (const APawn* Pawn = Cast<APawn>(Trader->GetOwner()); bLog && Pawn != nullptr && Pawn->IsLocallyControlled())
		{
			Trader->LogView();
		}
		Trader->OnTradeChanged.Broadcast();
	}
}

void UCrossguardTradeComponent::StartChecks()
{
	UCrossguardTradeComponent* Other = Partner.Get();
	for (UCrossguardTradeComponent* Trader : {this, Other})
	{
		if (Trader != nullptr)
		{
			Trader->GetWorld()->GetTimerManager().SetTimer(Trader->CheckTimer,
				FTimerDelegate::CreateUObject(Trader, &UCrossguardTradeComponent::CheckTrade), TradeCheckSeconds, true);
		}
	}
}

void UCrossguardTradeComponent::CheckTrade()
{
	UCrossguardTradeComponent* Other = Partner.Get();
	if (View.State == ECrossguardTradeState::None)
	{
		GetWorld()->GetTimerManager().ClearTimer(CheckTimer);
		return;
	}
	if (Other == nullptr || !IsOwnerAlive() || !Other->IsOwnerAlive())
	{
		EndTrade(ECrossguardTradeEnd::PartnerGone);
		return;
	}
	if (FVector::Dist(GetOwner()->GetActorLocation(), Other->GetOwner()->GetActorLocation()) > TradeConfig().MaxDistanceCm)
	{
		EndTrade(ECrossguardTradeEnd::TooFar);
		return;
	}
	if (View.State != ECrossguardTradeState::Open && GetWorld()->GetTimeSeconds() - RequestTime > TradeConfig().RequestSeconds)
	{
		EndTrade(ECrossguardTradeEnd::RequestExpired);
		return;
	}
	// Offered stacks can move or be used up meanwhile: the copies follow, and the trade checks again
	// on completion.
	if (View.State == ECrossguardTradeState::Open && Side == 0)
	{
		if (!OfferUnchanged() || !Other->OfferUnchanged())
		{
			ResetAfterChange();
		}
		else
		{
			RefreshViews(false);
		}
	}
}

void UCrossguardTradeComponent::EndTrade(ECrossguardTradeEnd Reason)
{
	UCrossguardTradeComponent* Other = Partner.Get();
	ResetLocal();
	UE_LOG(LogCrossguard, Display, TEXT("Trade of %s ended: %d"), *GetNameSafe(GetOwner()), static_cast<int32>(Reason));
	ClientTradeEnded(Reason);
	OnTradeEnded.Broadcast(Reason);
	if (Other != nullptr && Other->Partner.Get() == this)
	{
		Other->ResetLocal();
		Other->ClientTradeEnded(Reason);
		Other->OnTradeEnded.Broadcast(Reason);
	}
}

void UCrossguardTradeComponent::ResetLocal()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckTimer);
	}
	Partner.Reset();
	Session.Reset();
	OfferedStacks.Reset();
	View = FCrossguardTradeView();
	OnTradeChanged.Broadcast();
}

// --- Client ---------------------------------------------------------------------------------------

void UCrossguardTradeComponent::ClientTradeEnded_Implementation(ECrossguardTradeEnd Reason)
{
	// The listen-server host already heard it through EndTrade.
	if (GetOwner() != nullptr && !GetOwner()->HasAuthority())
	{
		OnTradeEnded.Broadcast(Reason);
	}
}

void UCrossguardTradeComponent::OnRep_View()
{
	LogView();
	OnTradeChanged.Broadcast();
}

void UCrossguardTradeComponent::LogView() const
{
	// Until a trade window exists.
	UE_LOG(LogCrossguard, Display, TEXT("Trade: state %d with %s; mine %d stacks + %d gold (locked %d, accepted %d); theirs %d stacks + %d gold (locked %d, accepted %d)"),
		static_cast<int32>(View.State), *GetNameSafe(View.Partner), View.MyItemIds.Num(), View.MyGold, View.bMyLocked, View.bMyAccepted,
		View.TheirItems.Num(), View.TheirGold, View.bTheirLocked, View.bTheirAccepted);
}

// --- Helpers --------------------------------------------------------------------------------------

UCrossguardInventoryComponent* UCrossguardTradeComponent::GetInventory() const
{
	return GetOwner() != nullptr ? GetOwner()->FindComponentByClass<UCrossguardInventoryComponent>() : nullptr;
}

bool UCrossguardTradeComponent::IsOwnerAlive() const
{
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	return GetOwner() != nullptr && !GetOwner()->IsActorBeingDestroyed() && ASC != nullptr && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
}

bool UCrossguardTradeComponent::IsInRaid() const
{
	const ACrossguardGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ACrossguardGameMode>() : nullptr;
	return GameMode == nullptr || GameMode->bIsRaid;
}

int32 UCrossguardTradeComponent::GetBankedGold() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const ACrossguardPlayerState* State = Pawn != nullptr ? Pawn->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	return State != nullptr ? State->GetGold() : 0;
}

bool UCrossguardTradeComponent::OfferUnchanged() const
{
	const UCrossguardInventoryComponent* Inventory = GetInventory();
	if (Inventory == nullptr)
	{
		return OfferedStacks.IsEmpty();
	}
	for (const FCrossguardInventoryItem& Offered : OfferedStacks)
	{
		FCrossguardInventoryItem Now;
		if (!Inventory->FindItem(Offered.Id, Now) || Now.Count != Offered.Count || Now.ItemId != Offered.ItemId || Now.Rarity != Offered.Rarity
			|| Now.Weapon.WeaponId != Offered.Weapon.WeaponId || Now.Armor.ArmorId != Offered.Armor.ArmorId)
		{
			return false;
		}
	}
	return true;
}

void UCrossguardTradeComponent::ResetAfterChange()
{
	UCrossguardTradeComponent* Other = Partner.Get();
	if (!Session.IsValid() || Other == nullptr)
	{
		return;
	}
	// Each offer again as the stacks are now: changed ones at their new count, gone ones dropped
	// (gold too if it is no longer banked). Both sides unlock.
	Session->Unlock(0);
	Session->Unlock(1);
	for (UCrossguardTradeComponent* Trader : {this, Other})
	{
		const UCrossguardInventoryComponent* Inventory = Trader->GetInventory();
		Crossguard::FTradeOffer Offer;
		Offer.Gold = Session->GetOffer(Trader->Side).Gold;
		TArray<FCrossguardInventoryItem> Stacks;
		for (const FCrossguardInventoryItem& Offered : Trader->OfferedStacks)
		{
			FCrossguardInventoryItem Current;
			if (Inventory != nullptr && Inventory->FindItem(Offered.Id, Current))
			{
				Offer.ItemIds.push_back(Current.Id);
				Stacks.Add(Current);
			}
		}
		if (Session->SetOffer(TradeConfig(), Trader->Side, Offer, Trader->GetBankedGold(), Trader->IsInRaid()) != Crossguard::ETradeError::None)
		{
			Offer.Gold = 0;
			Session->SetOffer(TradeConfig(), Trader->Side, Offer, Trader->GetBankedGold(), Trader->IsInRaid());
		}
		Trader->OfferedStacks = Stacks;
	}
	UE_LOG(LogCrossguard, Display, TEXT("Trade %s <-> %s: an offered stack changed; both unlocked"), *GetNameSafe(GetOwner()), *GetNameSafe(Other->GetOwner()));
	RefreshViews();
}
