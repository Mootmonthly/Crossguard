#pragma once

#include "CoreMinimal.h"
#include "Game/CrossguardGuildComponent.h"
#include "GameFramework/HUD.h"
#include "Items/CrossguardTradeComponent.h"

#include "CrossguardHUD.generated.h"

class ACrossguardCharacter;
class UCrossguardGuildWidget;
class UCrossguardHudWidget;
class UCrossguardInventoryWidget;
class UCrossguardLoadoutComponent;
class UCrossguardSkillWheelWidget;
class UCrossguardTradeWidget;
class UCrossguardWindowWidget;
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

	// Turns off the character's on-screen debug text (bDebugHud): this HUD shows the same and more.
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	bool bReplaceDebugHud = true;

	// Input. Leave empty for the defaults (placeholder keys, docs/design-decisions.md, Controls):
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

private:
	// True for a local player with a viewport: the only case that gets widgets.
	bool CanShowUI() const;
	void CreateWidgets();
	void EnsureDefaultInput();
	void BindInput();
	// Binds to the current character's components (they change on respawn) and the guild.
	void BindToPawn(ACrossguardCharacter* Character);
	void UnbindFromPawn();
	// Cursor and mouse look follow whether any window or the wheel is open.
	void UpdateInputMode();
	void ShowHudMessage(const FString& Message, const FLinearColor& Color) const;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardHudWidget> HudWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardSkillWheelWidget> SkillWheel;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardInventoryWidget> InventoryWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardTradeWidget> TradeWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardGuildWidget> GuildWidget;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCrossguardWindowWidget>> OpenWindows;

	TWeakObjectPtr<ACrossguardCharacter> BoundCharacter;
	TWeakObjectPtr<UCrossguardGuildComponent> BoundGuild;
	bool bWidgetsCreated = false;
	bool bWheelOpen = false;
	// Whether this HUD currently holds the controller's look-input ignore (a counter in the engine).
	bool bIgnoringLook = false;
	bool bUIMode = false;
	// Last seen states, to open windows when a body or a trade opens.
	bool bHadOpenContainer = false;
	ECrossguardTradeState LastTradeState = ECrossguardTradeState::None;
};
