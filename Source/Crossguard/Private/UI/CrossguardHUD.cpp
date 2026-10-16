#include "UI/CrossguardHUD.h"

#include "Blueprint/UserWidget.h"
#include "Character/CrossguardCharacter.h"
#include "Crossguard.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "UI/CrossguardGuildWidget.h"
#include "UI/CrossguardHudWidget.h"
#include "UI/CrossguardInventoryWidget.h"
#include "UI/CrossguardSkillWheelWidget.h"
#include "UI/CrossguardStashWidget.h"
#include "UI/CrossguardTradeWidget.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardHUDLocal
{
	// Viewport layers: HUD below the wheel below the windows.
	constexpr int32 HudZOrder = 0;
	constexpr int32 WheelZOrder = 5;
	constexpr int32 WindowZOrder = 10;
	// Above the character's default mapping context (0).
	constexpr int32 UIMappingPriority = 1;

	const FLinearColor Refusal(1.0f, 0.55f, 0.3f);
	const FLinearColor Success(0.5f, 1.0f, 0.5f);

	// Creates a widget of Class (or WidgetT itself when unset), builds its tree and adds it to the
	// viewport, hidden unless bShown.
	template <typename WidgetT>
	WidgetT* CreateUIWidget(APlayerController* Owner, TSubclassOf<WidgetT> Class, int32 ZOrder, bool bShown)
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

ACrossguardHUD::ACrossguardHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	HudWidgetClass = UCrossguardHudWidget::StaticClass();
	SkillWheelClass = UCrossguardSkillWheelWidget::StaticClass();
	InventoryWidgetClass = UCrossguardInventoryWidget::StaticClass();
	TradeWidgetClass = UCrossguardTradeWidget::StaticClass();
	GuildWidgetClass = UCrossguardGuildWidget::StaticClass();
}

bool ACrossguardHUD::CanShowUI() const
{
	const UWorld* World = GetWorld();
	return PlayerOwner != nullptr && PlayerOwner->IsLocalController() && PlayerOwner->GetLocalPlayer() != nullptr && World != nullptr
		&& World->GetGameViewport() != nullptr;
}

void ACrossguardHUD::BeginPlay()
{
	Super::BeginPlay();
	if (!CanShowUI())
	{
		return;
	}
	CreateWidgets();
	EnsureDefaultInput();
	BindInput();
	// The input mode outlives a map change: coming from the main menu it is still UI only (no keys, no
	// look). Start in game input; UpdateInputMode switches from here as windows open and close.
	if (PlayerOwner != nullptr)
	{
		PlayerOwner->SetInputMode(FInputModeGameOnly());
		PlayerOwner->SetShowMouseCursor(false);
		bUIMode = false;
	}
}

void ACrossguardHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromPawn();
	RemoveHubWindows();
	RemoveSocialWindows();
	if (bWidgetsCreated)
	{
		CloseAllWindows();
		if (bWheelOpen)
		{
			HandleSkillWheelClosed();
		}
		for (UUserWidget* Widget : TArray<UUserWidget*>{ HudWidget.Get(), SkillWheel.Get(), InventoryWidget.Get(), TradeWidget.Get(), GuildWidget.Get() })
		{
			if (Widget != nullptr)
			{
				Widget->RemoveFromParent();
			}
		}
		const ULocalPlayer* LocalPlayer = PlayerOwner != nullptr ? PlayerOwner->GetLocalPlayer() : nullptr;
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			Subsystem->RemoveMappingContext(UIMappingContext);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ACrossguardHUD::CreateWidgets()
{
	using CrossguardHUDLocal::CreateUIWidget;
	HudWidget = CreateUIWidget(PlayerOwner, HudWidgetClass, CrossguardHUDLocal::HudZOrder, true);
	SkillWheel = CreateUIWidget(PlayerOwner, SkillWheelClass, CrossguardHUDLocal::WheelZOrder, false);
	InventoryWidget = CreateUIWidget(PlayerOwner, InventoryWidgetClass, CrossguardHUDLocal::WindowZOrder, false);
	TradeWidget = CreateUIWidget(PlayerOwner, TradeWidgetClass, CrossguardHUDLocal::WindowZOrder, false);
	GuildWidget = CreateUIWidget(PlayerOwner, GuildWidgetClass, CrossguardHUDLocal::WindowZOrder, false);
	CreateHubWindows();
	CreateSocialWindows();
	bWidgetsCreated = true;
}

void ACrossguardHUD::EnsureDefaultInput()
{
	const auto MakeAction = [this](TObjectPtr<UInputAction>& Action, const TCHAR* Name)
	{
		if (Action == nullptr)
		{
			Action = NewObject<UInputAction>(this, Name);
			Action->ValueType = EInputActionValueType::Boolean;
		}
	};
	MakeAction(ToggleInventoryAction, TEXT("IA_ToggleInventory"));
	MakeAction(ToggleTradeAction, TEXT("IA_ToggleTrade"));
	MakeAction(ToggleGuildAction, TEXT("IA_ToggleGuild"));
	MakeAction(CloseWindowsAction, TEXT("IA_CloseWindows"));
	MakeSocialInputActions();
	if (UIMappingContext != nullptr)
	{
		return;
	}
	// The ruled keys (docs/design-decisions.md, Controls).
	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_UI"));
	Context->MapKey(ToggleInventoryAction, EKeys::I);
	Context->MapKey(ToggleInventoryAction, EKeys::Tab);
	Context->MapKey(ToggleTradeAction, EKeys::T);
	Context->MapKey(ToggleGuildAction, EKeys::U);
	Context->MapKey(CloseWindowsAction, EKeys::Escape);
	MapSocialKeys(Context);
	UIMappingContext = Context;
}

void ACrossguardHUD::BindInput()
{
	// An input component of the project's default class (Enhanced Input), pushed on the
	// controller's input stack next to the pawn's.
	EnableInput(PlayerOwner);
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent))
	{
		Input->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &ACrossguardHUD::ToggleInventory);
		Input->BindAction(ToggleTradeAction, ETriggerEvent::Started, this, &ACrossguardHUD::ToggleTrade);
		Input->BindAction(ToggleGuildAction, ETriggerEvent::Started, this, &ACrossguardHUD::ToggleGuild);
		Input->BindAction(CloseWindowsAction, ETriggerEvent::Started, this, &ACrossguardHUD::CloseAllWindows);
		BindSocialInput(*Input);
	}
	else
	{
		UE_LOG(LogCrossguard, Warning, TEXT("CrossguardHUD: no Enhanced Input component; UI keys are not bound"));
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerOwner->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(UIMappingContext, CrossguardHUDLocal::UIMappingPriority);
	}
}

void ACrossguardHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bWidgetsCreated || PlayerOwner == nullptr)
	{
		return;
	}

	ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(PlayerOwner->GetPawn());
	if (Character != BoundCharacter.Get())
	{
		UnbindFromPawn();
		BindToPawn(Character);
	}
	// The player state (and its guild) can arrive after the pawn.
	const ACrossguardPlayerState* PlayerState = PlayerOwner->GetPlayerState<ACrossguardPlayerState>();
	UCrossguardGuildComponent* Guild = PlayerState != nullptr ? PlayerState->GetGuild() : nullptr;
	if (Guild != BoundGuild.Get())
	{
		if (UCrossguardGuildComponent* Old = BoundGuild.Get())
		{
			Old->OnGuildRefused.RemoveDynamic(this, &ACrossguardHUD::HandleGuildRefused);
		}
		if (Guild != nullptr)
		{
			Guild->OnGuildRefused.AddUniqueDynamic(this, &ACrossguardHUD::HandleGuildRefused);
		}
		BoundGuild = Guild;
	}
	TickHubWindows();
	if (Character == nullptr)
	{
		bHadOpenContainer = false;
		LastTradeState = ECrossguardTradeState::None;
		return;
	}

	// A body or chest was opened (F): show it next to the inventory.
	const UCrossguardInventoryComponent* Inventory = Character->GetInventoryComponent();
	const bool bHasOpenContainer = Inventory != nullptr && Inventory->GetOpenContainer() != nullptr;
	if (bHasOpenContainer && !bHadOpenContainer && !IsWindowOpen(InventoryWidget))
	{
		OpenWindow(InventoryWidget);
	}
	bHadOpenContainer = bHasOpenContainer;

	// Trade: a request is announced; an accepted trade opens the window on both screens.
	const UCrossguardTradeComponent* Trade = Character->GetTradeComponent();
	const ECrossguardTradeState TradeState = Trade != nullptr ? Trade->GetTradeView().State : ECrossguardTradeState::None;
	if (TradeState != LastTradeState)
	{
		if (TradeState == ECrossguardTradeState::RequestReceived)
		{
			ShowHudMessage(FString::Printf(TEXT("%s wants to trade: press T"), *CrossguardUI::PlayerNameOf(Trade->GetTradeView().Partner)),
				FLinearColor(1.0f, 0.9f, 0.4f));
		}
		else if (TradeState == ECrossguardTradeState::Open)
		{
			OpenWindow(TradeWidget);
		}
		else if (TradeState == ECrossguardTradeState::None && LastTradeState == ECrossguardTradeState::Open)
		{
			CloseWindow(TradeWidget);
		}
		LastTradeState = TradeState;
	}
}

void ACrossguardHUD::BindToPawn(ACrossguardCharacter* Character)
{
	BoundCharacter = Character;
	if (Character == nullptr)
	{
		return;
	}
	if (bReplaceDebugHud)
	{
		Character->bDebugHud = false;
	}
	if (UCrossguardInventoryComponent* Inventory = Character->GetInventoryComponent())
	{
		Inventory->OnInventoryRejected.AddUniqueDynamic(this, &ACrossguardHUD::HandleInventoryRejected);
	}
	if (UCrossguardTradeComponent* Trade = Character->GetTradeComponent())
	{
		Trade->OnTradeEnded.AddUniqueDynamic(this, &ACrossguardHUD::HandleTradeEnded);
	}
	if (UCrossguardLoadoutComponent* Loadout = Character->GetLoadoutComponent())
	{
		Loadout->OnSkillWheelOpened.AddUniqueDynamic(this, &ACrossguardHUD::HandleSkillWheelOpened);
		Loadout->OnSkillWheelClosed.AddUniqueDynamic(this, &ACrossguardHUD::HandleSkillWheelClosed);
	}
}

void ACrossguardHUD::UnbindFromPawn()
{
	if (ACrossguardCharacter* Character = BoundCharacter.Get())
	{
		if (UCrossguardInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			Inventory->OnInventoryRejected.RemoveDynamic(this, &ACrossguardHUD::HandleInventoryRejected);
		}
		if (UCrossguardTradeComponent* Trade = Character->GetTradeComponent())
		{
			Trade->OnTradeEnded.RemoveDynamic(this, &ACrossguardHUD::HandleTradeEnded);
		}
		if (UCrossguardLoadoutComponent* Loadout = Character->GetLoadoutComponent())
		{
			Loadout->OnSkillWheelOpened.RemoveDynamic(this, &ACrossguardHUD::HandleSkillWheelOpened);
			Loadout->OnSkillWheelClosed.RemoveDynamic(this, &ACrossguardHUD::HandleSkillWheelClosed);
		}
	}
	if (UCrossguardGuildComponent* Guild = BoundGuild.Get())
	{
		Guild->OnGuildRefused.RemoveDynamic(this, &ACrossguardHUD::HandleGuildRefused);
	}
	BoundGuild = nullptr;
	BoundCharacter = nullptr;
	// The wheel belongs to the old pawn's Q.
	if (bWheelOpen)
	{
		HandleSkillWheelClosed();
	}
}

void ACrossguardHUD::HandleSkillWheelOpened()
{
	if (SkillWheel == nullptr)
	{
		return;
	}
	bWheelOpen = true;
	SkillWheel->Refresh();
	SkillWheel->SetVisibility(ESlateVisibility::Visible);
	UpdateInputMode();
	// The wheel is centred on the crosshair: start the cursor there.
	int32 Width = 0;
	int32 Height = 0;
	PlayerOwner->GetViewportSize(Width, Height);
	PlayerOwner->SetMouseLocation(Width / 2, Height / 2);
}

void ACrossguardHUD::HandleSkillWheelClosed()
{
	// The selection was made by hovering (UCrossguardSkillWheelWidget); E casts it.
	bWheelOpen = false;
	if (SkillWheel != nullptr)
	{
		SkillWheel->SetVisibility(ESlateVisibility::Collapsed);
	}
	UpdateInputMode();
}

void ACrossguardHUD::HandleInventoryRejected(const FCrossguardInventoryResult& Result)
{
	FString Message = FString::Printf(TEXT("Inventory: %s"), *CrossguardUI::UIEnumName(Result.Error));
	if (Result.EquipError != ECrossguardEquipError::None)
	{
		Message += FString::Printf(TEXT(" (%s)"), *CrossguardUI::UIEnumName(Result.EquipError));
	}
	if (Result.ProgressionError != ECrossguardProgressionError::None)
	{
		Message += FString::Printf(TEXT(" (%s)"), *CrossguardUI::UIEnumName(Result.ProgressionError));
	}
	ShowHudMessage(Message, CrossguardHUDLocal::Refusal);
	if (InventoryWidget != nullptr)
	{
		InventoryWidget->ShowStatus(Message, CrossguardHUDLocal::Refusal);
	}
	if (StashWidget != nullptr)
	{
		StashWidget->ShowStatus(Message, CrossguardHUDLocal::Refusal);
	}
}

void ACrossguardHUD::HandleTradeEnded(ECrossguardTradeEnd Reason)
{
	const bool bCompleted = Reason == ECrossguardTradeEnd::Completed;
	const FString Message = bCompleted ? FString(TEXT("Trade completed")) : FString::Printf(TEXT("Trade ended: %s"), *CrossguardUI::UIEnumName(Reason));
	const FLinearColor Color = bCompleted ? CrossguardHUDLocal::Success : CrossguardHUDLocal::Refusal;
	ShowHudMessage(Message, Color);
	if (TradeWidget != nullptr)
	{
		TradeWidget->ShowStatus(Message, Color);
	}
}

void ACrossguardHUD::HandleGuildRefused(ECrossguardGuildResult Result)
{
	ShowHudMessage(FString::Printf(TEXT("Guild: refused (%s)"), *CrossguardUI::UIEnumName(Result)), CrossguardHUDLocal::Refusal);
	if (GuildWidget != nullptr)
	{
		GuildWidget->ShowRefusal(Result);
	}
}

void ACrossguardHUD::ShowHudMessage(const FString& Message, const FLinearColor& Color) const
{
	if (HudWidget != nullptr)
	{
		HudWidget->ShowMessage(Message, Color);
	}
}

void ACrossguardHUD::ToggleInventory()
{
	if (IsWindowOpen(InventoryWidget))
	{
		CloseWindow(InventoryWidget);
	}
	else
	{
		OpenWindow(InventoryWidget);
	}
}

void ACrossguardHUD::ToggleTrade()
{
	if (IsWindowOpen(TradeWidget))
	{
		CloseWindow(TradeWidget);
		return;
	}
	const ACrossguardCharacter* Character = PlayerOwner != nullptr ? Cast<ACrossguardCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const UCrossguardTradeComponent* Trade = Character != nullptr ? Character->GetTradeComponent() : nullptr;
	if (TradeWidget != nullptr && Trade != nullptr && Trade->GetTradeView().State == ECrossguardTradeState::None)
	{
		TradeWidget->AskNearestPlayer();
	}
	OpenWindow(TradeWidget);
}

void ACrossguardHUD::ToggleGuild()
{
	if (IsWindowOpen(GuildWidget))
	{
		CloseWindow(GuildWidget);
	}
	else
	{
		OpenWindow(GuildWidget);
	}
}

void ACrossguardHUD::CloseAllWindows()
{
	const TArray<TObjectPtr<UCrossguardWindowWidget>> Windows = OpenWindows;
	for (UCrossguardWindowWidget* Window : Windows)
	{
		CloseWindow(Window);
	}
}

bool ACrossguardHUD::IsWindowOpen(const UCrossguardWindowWidget* Window) const
{
	return Window != nullptr && OpenWindows.Contains(Window);
}

bool ACrossguardHUD::IsAnyWindowOpen() const
{
	return !OpenWindows.IsEmpty();
}

void ACrossguardHUD::OpenWindow(UCrossguardWindowWidget* Window)
{
	if (Window == nullptr || IsWindowOpen(Window))
	{
		return;
	}
	OpenWindows.Add(Window);
	Window->Refresh();
	Window->SetVisibility(ESlateVisibility::Visible);
	Window->OnWindowOpened();
	UpdateInputMode();
}

void ACrossguardHUD::CloseWindow(UCrossguardWindowWidget* Window)
{
	if (Window == nullptr || OpenWindows.Remove(Window) == 0)
	{
		return;
	}
	Window->SetVisibility(ESlateVisibility::Collapsed);
	Window->OnWindowClosed();
	UpdateInputMode();
}

void ACrossguardHUD::UpdateInputMode()
{
	const bool bWantUI = bWheelOpen || IsAnyWindowOpen();
	if (PlayerOwner == nullptr || bWantUI == bUIMode)
	{
		return;
	}
	bUIMode = bWantUI;
	if (bWantUI)
	{
		// Keys still reach the game (walk, hotbar, Q, E); the mouse drives the UI.
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerOwner->SetInputMode(Mode);
		PlayerOwner->SetShowMouseCursor(true);
		if (!bIgnoringLook)
		{
			PlayerOwner->SetIgnoreLookInput(true);
			bIgnoringLook = true;
		}
	}
	else
	{
		PlayerOwner->SetInputMode(FInputModeGameOnly());
		PlayerOwner->SetShowMouseCursor(false);
		if (bIgnoringLook)
		{
			PlayerOwner->SetIgnoreLookInput(false);
			bIgnoringLook = false;
		}
	}
}
