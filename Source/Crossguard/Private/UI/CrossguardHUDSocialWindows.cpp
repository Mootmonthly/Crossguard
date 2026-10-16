// ACrossguardHUD: the attributes window (K), the party window (P) and the always-on team list
// (docs/ui.md). Kept apart from CrossguardHUD.cpp; the HUD calls these from CreateWidgets, EndPlay,
// EnsureDefaultInput and BindInput.

#include "UI/CrossguardHUD.h"

#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UI/CrossguardAttributesWidget.h"
#include "UI/CrossguardPartyWidget.h"
#include "UI/CrossguardTeamListWidget.h"

namespace CrossguardHUDSocialLocal
{
	// The same layers as CrossguardHUD.cpp: the team list with the HUD, windows above the wheel.
	constexpr int32 TeamListZOrder = 1;
	constexpr int32 SocialWindowZOrder = 10;

	// Class, or WidgetT itself when unset; tree built; added hidden unless bShown.
	template <typename WidgetT>
	WidgetT* CreateSocialWidget(APlayerController* Owner, TSubclassOf<WidgetT> Class, int32 ZOrder, bool bShown)
	{
		UClass* Chosen = Class != nullptr ? Class.Get() : WidgetT::StaticClass();
		WidgetT* Widget = CreateWidget<WidgetT>(Owner, Chosen);
		if (Widget == nullptr)
		{
			return nullptr;
		}
		Widget->EnsureTree();
		if (!bShown)
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
		Widget->AddToViewport(ZOrder);
		return Widget;
	}
}

void ACrossguardHUD::CreateSocialWindows()
{
	using CrossguardHUDSocialLocal::CreateSocialWidget;
	AttributesWidget = CreateSocialWidget(PlayerOwner, AttributesWidgetClass, CrossguardHUDSocialLocal::SocialWindowZOrder, false);
	PartyWidget = CreateSocialWidget(PlayerOwner, PartyWidgetClass, CrossguardHUDSocialLocal::SocialWindowZOrder, false);
	TeamListWidget = CreateSocialWidget(PlayerOwner, TeamListWidgetClass, CrossguardHUDSocialLocal::TeamListZOrder, true);
}

void ACrossguardHUD::RemoveSocialWindows()
{
	for (UCrossguardWindowWidget* Window : TArray<UCrossguardWindowWidget*>{ AttributesWidget.Get(), PartyWidget.Get() })
	{
		CloseWindow(Window);
		if (Window != nullptr)
		{
			Window->RemoveFromParent();
		}
	}
	if (TeamListWidget != nullptr)
	{
		TeamListWidget->RemoveFromParent();
	}
}

void ACrossguardHUD::MakeSocialInputActions()
{
	const auto MakeAction = [this](TObjectPtr<UInputAction>& Action, const TCHAR* Name)
	{
		if (Action == nullptr)
		{
			Action = NewObject<UInputAction>(this, Name);
			Action->ValueType = EInputActionValueType::Boolean;
		}
	};
	MakeAction(ToggleAttributesAction, TEXT("IA_ToggleAttributes"));
	MakeAction(TogglePartyAction, TEXT("IA_ToggleParty"));
}

void ACrossguardHUD::MapSocialKeys(UInputMappingContext* Context)
{
	// Placeholder keys (docs/design-decisions.md, Controls): K attributes, P party.
	if (Context != nullptr)
	{
		Context->MapKey(ToggleAttributesAction, EKeys::K);
		Context->MapKey(TogglePartyAction, EKeys::P);
	}
}

void ACrossguardHUD::BindSocialInput(UEnhancedInputComponent& Input)
{
	Input.BindAction(ToggleAttributesAction, ETriggerEvent::Started, this, &ACrossguardHUD::ToggleAttributes);
	Input.BindAction(TogglePartyAction, ETriggerEvent::Started, this, &ACrossguardHUD::ToggleParty);
}

void ACrossguardHUD::ToggleAttributes()
{
	if (IsWindowOpen(AttributesWidget))
	{
		CloseWindow(AttributesWidget);
	}
	else
	{
		OpenWindow(AttributesWidget);
	}
}

void ACrossguardHUD::ToggleParty()
{
	if (IsWindowOpen(PartyWidget))
	{
		CloseWindow(PartyWidget);
	}
	else
	{
		OpenWindow(PartyWidget);
	}
}
