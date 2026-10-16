// ACrossguardHUD, continued: the stash and vendor screens (docs/ui.md). Kept apart from
// CrossguardHUD.cpp, which only calls CreateHubWindows, TickHubWindows and RemoveHubWindows.

#include "UI/CrossguardHUD.h"

#include "Blueprint/UserWidget.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CrossguardInteractable.h"
#include "Interaction/CrossguardVendorNPC.h"
#include "Items/CrossguardStashComponent.h"
#include "Items/CrossguardVendorComponent.h"
#include "UI/CrossguardStashWidget.h"
#include "UI/CrossguardUIHelpers.h"
#include "UI/CrossguardVendorWidget.h"

namespace CrossguardHUDHubLocal
{
	// As CrossguardHUD.cpp: windows above the HUD and the wheel.
	constexpr int32 HubWindowZOrder = 10;
	// A screen opened at a chest / NPC closes once its character is this far past the interact range.
	constexpr float CloseSlack = 150.0f;

	const FLinearColor Refusal(1.0f, 0.55f, 0.3f);
	const FLinearColor Success(0.5f, 1.0f, 0.5f);

	// Creates a hidden window of Class (or WidgetT itself when unset), its tree built, in the viewport.
	template <typename WidgetT>
	WidgetT* MakeHubWindow(APlayerController* Owner, TSubclassOf<WidgetT> Class)
	{
		UClass* Chosen = Class != nullptr ? Class.Get() : WidgetT::StaticClass();
		WidgetT* Widget = CreateWidget<WidgetT>(Owner, Chosen);
		if (Widget == nullptr)
		{
			return nullptr;
		}
		Widget->EnsureTree();
		Widget->SetVisibility(ESlateVisibility::Collapsed);
		Widget->AddToViewport(HubWindowZOrder);
		return Widget;
	}

	FString DescribeRefusal(const TCHAR* What, const FCrossguardInventoryResult& Result)
	{
		FString Message = FString::Printf(TEXT("%s: %s"), What, *CrossguardUI::UIEnumName(Result.Error));
		if (Result.ProgressionError != ECrossguardProgressionError::None)
		{
			Message += FString::Printf(TEXT(" (%s)"), *CrossguardUI::UIEnumName(Result.ProgressionError));
		}
		return Message;
	}

	// Whether a screen opened at Source (bHasSource) should close now.
	bool ShouldCloseHubWindow(const APawn* Pawn, bool bInRaid, bool bHasSource, const AActor* Source)
	{
		if (bInRaid || Pawn == nullptr)
		{
			return true;
		}
		if (!bHasSource)
		{
			return false;
		}
		const ICrossguardInteractable* Interactable = Cast<ICrossguardInteractable>(Source);
		const float Range = Interactable != nullptr ? Interactable->GetInteractRange() : 250.0f;
		return Source == nullptr || FVector::Dist(Pawn->GetActorLocation(), Source->GetActorLocation()) > Range + CloseSlack;
	}
}

void ACrossguardHUD::CreateHubWindows()
{
	StashWidget = CrossguardHUDHubLocal::MakeHubWindow(PlayerOwner, StashWidgetClass);
	VendorWidget = CrossguardHUDHubLocal::MakeHubWindow(PlayerOwner, VendorWidgetClass);
}

void ACrossguardHUD::RemoveHubWindows()
{
	BindHubComponents(nullptr, nullptr);
	for (UUserWidget* Widget : TArray<UUserWidget*>{ StashWidget.Get(), VendorWidget.Get() })
	{
		if (Widget != nullptr)
		{
			Widget->RemoveFromParent();
		}
	}
}

void ACrossguardHUD::BindHubComponents(UCrossguardStashComponent* Stash, UCrossguardVendorComponent* Vendor)
{
	if (Stash != BoundStash.Get())
	{
		if (UCrossguardStashComponent* Old = BoundStash.Get())
		{
			Old->OnStashOpened.RemoveDynamic(this, &ACrossguardHUD::HandleStashOpened);
			Old->OnStashRejected.RemoveDynamic(this, &ACrossguardHUD::HandleStashRejected);
		}
		if (Stash != nullptr)
		{
			Stash->OnStashOpened.AddUniqueDynamic(this, &ACrossguardHUD::HandleStashOpened);
			Stash->OnStashRejected.AddUniqueDynamic(this, &ACrossguardHUD::HandleStashRejected);
		}
		BoundStash = Stash;
	}
	if (Vendor != BoundVendor.Get())
	{
		if (UCrossguardVendorComponent* Old = BoundVendor.Get())
		{
			Old->OnVendorOpened.RemoveDynamic(this, &ACrossguardHUD::HandleVendorOpened);
			Old->OnSellRejected.RemoveDynamic(this, &ACrossguardHUD::HandleSellRejected);
			Old->OnSold.RemoveDynamic(this, &ACrossguardHUD::HandleSold);
		}
		if (Vendor != nullptr)
		{
			Vendor->OnVendorOpened.AddUniqueDynamic(this, &ACrossguardHUD::HandleVendorOpened);
			Vendor->OnSellRejected.AddUniqueDynamic(this, &ACrossguardHUD::HandleSellRejected);
			Vendor->OnSold.AddUniqueDynamic(this, &ACrossguardHUD::HandleSold);
		}
		BoundVendor = Vendor;
	}
}

void ACrossguardHUD::TickHubWindows()
{
	// The player state (stash and vendor live on it) can arrive after the HUD, and changes on travel.
	const ACrossguardPlayerState* PlayerState = PlayerOwner != nullptr ? PlayerOwner->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	BindHubComponents(PlayerState != nullptr ? PlayerState->GetStash() : nullptr, PlayerState != nullptr ? PlayerState->GetVendor() : nullptr);

	// Closed in a raid (the server refuses everything there anyway), without a character, or once
	// the character walked away from the chest / NPC.
	const APawn* Pawn = PlayerOwner != nullptr ? PlayerOwner->GetPawn() : nullptr;
	const bool bInRaid = PlayerState != nullptr && PlayerState->IsInRaid();
	if (IsWindowOpen(StashWidget) && CrossguardHUDHubLocal::ShouldCloseHubWindow(Pawn, bInRaid, bStashHasSource, StashSource.Get()))
	{
		CloseWindow(StashWidget);
		if (bInRaid)
		{
			ShowHudMessage(TEXT("Stash: between raids only"), CrossguardHUDHubLocal::Refusal);
		}
	}
	if (IsWindowOpen(VendorWidget) && CrossguardHUDHubLocal::ShouldCloseHubWindow(Pawn, bInRaid, bVendorHasSource, VendorSource.Get()))
	{
		CloseWindow(VendorWidget);
		if (bInRaid)
		{
			ShowHudMessage(TEXT("Vendor: between raids only"), CrossguardHUDHubLocal::Refusal);
		}
	}
}

void ACrossguardHUD::ShowStashWindow(AActor* Source)
{
	if (StashWidget == nullptr)
	{
		return;
	}
	const ACrossguardPlayerState* PlayerState = PlayerOwner != nullptr ? PlayerOwner->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (PlayerState != nullptr && PlayerState->IsInRaid())
	{
		ShowHudMessage(TEXT("Stash: between raids only"), CrossguardHUDHubLocal::Refusal);
		return;
	}
	StashSource = Source;
	bStashHasSource = Source != nullptr;
	// One full-screen window at a time.
	CloseAllWindows();
	OpenWindow(StashWidget);
}

void ACrossguardHUD::ShowVendorWindow(AActor* Source)
{
	if (VendorWidget == nullptr)
	{
		return;
	}
	const ACrossguardPlayerState* PlayerState = PlayerOwner != nullptr ? PlayerOwner->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (PlayerState != nullptr && PlayerState->IsInRaid())
	{
		ShowHudMessage(TEXT("Vendor: between raids only"), CrossguardHUDHubLocal::Refusal);
		return;
	}
	VendorSource = Source;
	bVendorHasSource = Source != nullptr;
	const ACrossguardVendorNPC* Npc = Cast<ACrossguardVendorNPC>(Source);
	VendorWidget->SetVendorName(Npc != nullptr ? Npc->VendorName : FString());
	CloseAllWindows();
	OpenWindow(VendorWidget);
}

void ACrossguardHUD::OpenStash()
{
	ShowStashWindow(nullptr);
}

void ACrossguardHUD::OpenVendor()
{
	ShowVendorWindow(nullptr);
}

void ACrossguardHUD::HandleStashOpened(AActor* Source)
{
	ShowStashWindow(Source);
}

void ACrossguardHUD::HandleVendorOpened(AActor* Vendor)
{
	ShowVendorWindow(Vendor);
}

void ACrossguardHUD::HandleStashRejected(const FCrossguardInventoryResult& Result)
{
	const FString Message = CrossguardHUDHubLocal::DescribeRefusal(TEXT("Stash"), Result);
	ShowHudMessage(Message, CrossguardHUDHubLocal::Refusal);
	if (StashWidget != nullptr)
	{
		StashWidget->ShowStatus(Message, CrossguardHUDHubLocal::Refusal);
	}
}

void ACrossguardHUD::HandleSellRejected(const FCrossguardInventoryResult& Result)
{
	const FString Message = CrossguardHUDHubLocal::DescribeRefusal(TEXT("Vendor"), Result);
	ShowHudMessage(Message, CrossguardHUDHubLocal::Refusal);
	if (VendorWidget != nullptr)
	{
		VendorWidget->ShowStatus(Message, CrossguardHUDHubLocal::Refusal);
	}
}

void ACrossguardHUD::HandleSold(int32 Gold)
{
	const FString Message = FString::Printf(TEXT("Sold for %d gold"), Gold);
	ShowHudMessage(Message, CrossguardHUDHubLocal::Success);
	if (VendorWidget != nullptr)
	{
		VendorWidget->ShowStatus(Message, CrossguardHUDHubLocal::Success);
	}
}
