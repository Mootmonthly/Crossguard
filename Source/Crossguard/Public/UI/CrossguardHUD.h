#pragma once

#include "CoreMinimal.h"
#include "Game/CrossguardGuildComponent.h"
#include "GameFramework/HUD.h"
#include "Items/CrossguardTradeComponent.h"

#include "CrossguardHUD.generated.h"

class ACrossguardCharacter;
class UCrossguardAttributesWidget;
class UCrossguardGuildWidget;
class UCrossguardHudWidget;
class UCrossguardInventoryWidget;
class UCrossguardLoadoutComponent;
class UCrossguardPartyWidget;
class UCrossguardTeamListWidget;
class UCrossguardSkillWheelWidget;
class UCrossguardStashComponent;
class UCrossguardStashWidget;
class UCrossguardTradeWidget;
class UCrossguardVendorComponent;
class UCrossguardVendorWidget;
class UCrossguardWindowWidget;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;

// The game's HUD (set by ACrossguardGameMode): creates the placeholder UI for the local player only
// (docs/ui.md): the always-on HUD, the skill wheel (while Q is held) and the inventory, trade and
// guild windows, with their keys (I or Tab, T, U; Esc closes). While a window or the wheel is open
// the mouse cursor shows and mouse look stops. Replace any widget with a Widget Blueprint by setting
// its class here (a Blueprint subclass of this HUD, set as the game mode's HUD Class).
//
// Client-side only: it shows replicated state and forwards clicks to the components' requests;
// the server validates every one of them.
UCLASS()
class CROSSGUARD_API ACrossguardHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACrossguardHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardHudWidget> HudWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardSkillWheelWidget> SkillWheelClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardInventoryWidget> InventoryWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardTradeWidget> TradeWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardGuildWidget> GuildWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardStashWidget> StashWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardVendorWidget> VendorWidgetClass;

	// Turns off the character's on-screen debug text (bDebugHud): this HUD shows the same and more.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	bool bReplaceDebugHud = true;

	// Input. Leave empty for the defaults (the ruled keys, docs/design-decisions.md, Controls):
	// I or Tab inventory, T trade, U guild, Esc closes windows.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputMappingContext> UIMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> ToggleInventoryAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> ToggleTradeAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> ToggleGuildAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> CloseWindowsAction;

	// --- Attributes and party windows, team list (CrossguardHUDSocialWindows.cpp) ---
	// Keys (placeholders, docs/design-decisions.md, Controls): K attributes, P party. Empty classes
	// use the built-in widgets.

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardAttributesWidget> AttributesWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardPartyWidget> PartyWidgetClass;

	// The always-on teammate list (top right), with "(disconnected)" for teammates who are away.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardTeamListWidget> TeamListWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> ToggleAttributesAction;

	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|Input")
	TObjectPtr<UInputAction> TogglePartyAction;

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ToggleAttributes();

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ToggleParty();

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardAttributesWidget* GetAttributesWidget() const { return AttributesWidget; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardPartyWidget* GetPartyWidget() const { return PartyWidget; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardTeamListWidget* GetTeamListWidget() const { return TeamListWidget; }

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ToggleInventory();

	// No trade yet: asks the nearest player and opens the window. Otherwise opens or closes it.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ToggleTrade();

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ToggleGuild();

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void CloseAllWindows();

	void OpenWindow(UCrossguardWindowWidget* Window);
	void CloseWindow(UCrossguardWindowWidget* Window);

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	bool IsWindowOpen(const UCrossguardWindowWidget* Window) const;

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	bool IsAnyWindowOpen() const;

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardHudWidget* GetHudWidget() const { return HudWidget; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardInventoryWidget* GetInventoryWidget() const { return InventoryWidget; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardTradeWidget* GetTradeWidget() const { return TradeWidget; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardGuildWidget* GetGuildWidget() const { return GuildWidget; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardSkillWheelWidget* GetSkillWheel() const { return SkillWheel; }

	// --- Stash and vendor screens (CrossguardHUDHubWindows.cpp) ---

	// Shows the stash / vendor screen in place of any other window. F on a stash chest or vendor NPC
	// does it once the server agrees (UCrossguardStashComponent::OnStashOpened,
	// UCrossguardVendorComponent::OnVendorOpened). Source: that chest or NPC; the screen closes when
	// the character walks away from it, and in a raid. Null: no distance check (console commands).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ShowStashWindow(AActor* Source);

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ShowVendorWindow(AActor* Source);

	// Console (debug, placeholder): the stash / vendor screen without a chest or NPC. The server still
	// refuses every request in a raid.
	UFUNCTION(Exec)
	void OpenStash();

	UFUNCTION(Exec)
	void OpenVendor();

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardStashWidget* GetStashWidget() const { return StashWidget; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardVendorWidget* GetVendorWidget() const { return VendorWidget; }

protected:
	UFUNCTION()
	void HandleSkillWheelOpened();

	UFUNCTION()
	void HandleSkillWheelClosed();

	UFUNCTION()
	void HandleInventoryRejected(const FCrossguardInventoryResult& Result);

	UFUNCTION()
	void HandleTradeEnded(ECrossguardTradeEnd Reason);

	UFUNCTION()
	void HandleGuildRefused(ECrossguardGuildResult Result);

	UFUNCTION()
	void HandleStashOpened(AActor* Source);

	UFUNCTION()
	void HandleStashRejected(const FCrossguardInventoryResult& Result);

	UFUNCTION()
	void HandleVendorOpened(AActor* Vendor);

	UFUNCTION()
	void HandleSellRejected(const FCrossguardInventoryResult& Result);

	UFUNCTION()
	void HandleSold(int32 Gold);

private:
	// True for a local player with a viewport: the only case that gets widgets.
	bool CanShowUI() const;
	// Attributes and party windows, team list (CrossguardHUDSocialWindows.cpp).
	void CreateSocialWindows();
	void RemoveSocialWindows();
	void MakeSocialInputActions();
	void MapSocialKeys(UInputMappingContext* Context);
	void BindSocialInput(UEnhancedInputComponent& Input);
	void CreateWidgets();
	void EnsureDefaultInput();
	void BindInput();
	// Binds to the current character's components (they change on respawn) and the guild.
	void BindToPawn(ACrossguardCharacter* Character);
	void UnbindFromPawn();
	// Cursor and mouse look follow whether any window or the wheel is open.
	void UpdateInputMode();
	void ShowHudMessage(const FString& Message, const FLinearColor& Color) const;
	// Stash and vendor screens (CrossguardHUDHubWindows.cpp): made with the other widgets, bound to
	// the player state's stash and vendor components, closed in a raid or away from their source.
	void CreateHubWindows();
	void TickHubWindows();
	void RemoveHubWindows();
	void BindHubComponents(UCrossguardStashComponent* Stash, UCrossguardVendorComponent* Vendor);

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardHudWidget> HudWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardSkillWheelWidget> SkillWheel;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardAttributesWidget> AttributesWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardPartyWidget> PartyWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardTeamListWidget> TeamListWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardInventoryWidget> InventoryWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardTradeWidget> TradeWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardGuildWidget> GuildWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardStashWidget> StashWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardVendorWidget> VendorWidget;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCrossguardWindowWidget>> OpenWindows;

	TWeakObjectPtr<ACrossguardCharacter> BoundCharacter;
	TWeakObjectPtr<UCrossguardGuildComponent> BoundGuild;
	TWeakObjectPtr<UCrossguardStashComponent> BoundStash;
	TWeakObjectPtr<UCrossguardVendorComponent> BoundVendor;
	// The chest / NPC the open stash / vendor screen belongs to (null: opened from the console).
	TWeakObjectPtr<AActor> StashSource;
	TWeakObjectPtr<AActor> VendorSource;
	bool bStashHasSource = false;
	bool bVendorHasSource = false;
	bool bWidgetsCreated = false;
	bool bWheelOpen = false;
	// Whether this HUD currently holds the controller's look-input ignore (a counter in the engine).
	bool bIgnoringLook = false;
	bool bUIMode = false;
	// Last seen states, to open windows when a body or a trade opens.
	bool bHadOpenContainer = false;
	ECrossguardTradeState LastTradeState = ECrossguardTradeState::None;
};
