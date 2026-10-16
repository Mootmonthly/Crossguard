#pragma once

#include "CoreMinimal.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardMainMenuWidget.generated.h"

class UCrossguardSettingsSave;
class UCrossguardUIButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

// The title screen (placeholder, docs/ui.md, docs/hub-and-menu.md), shown by ACrossguardMenuHUD on
// the menu map: Play (host the hub as a listen server: ACrossguardMenuGameMode::BuildHostUrl), Join
// (an address field; ClientTravel to it), Settings (mouse sensitivity, field of view, master volume:
// UCrossguardSettingsSave), Quit. The reason the player came back (e.g. "Your raid is over.") shows
// in the status line. Character select is not built (saves are per player id, not per character).
UCLASS()
class CROSSGUARD_API UCrossguardMainMenuWidget : public UCrossguardWidgetBase
{
	GENERATED_BODY()

public:
	UCrossguardMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	// --- What the buttons do ---

	// DEV-ONLY (listen server): hosts the hub: a listen server on the hub map with ?Hub. Refused (status
	// line) outside development mode (Crossguard::IsDevListenMode) or if the map is missing.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void Play();

	// Connects to the address in the field.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void Join();

	// Shows or hides the settings panel.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ToggleSettings();

	// Writes the settings and applies them to the local player.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void SaveSettings();

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void Quit();

	// --- Read back (tests, Blueprint subclasses) ---

	// The URL Play would open: the menu game mode's hub map (default /Game/Maps/L_Hub) + ?listen?Hub.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetPlayUrl() const;

	// The URL Join would open from the address field; empty if the field holds no valid address.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetJoinUrl() const;

	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void SetJoinAddress(const FString& Address);

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetStatusLine() const { return StatusLine; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	bool IsSettingsOpen() const { return bSettingsOpen; }

	// "Mouse sensitivity 1.00 | Field of view 90 | Master volume 100%".
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetSettingsLine() const;

	// The settings being edited (loaded on construction).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardSettingsSave* GetSettings() const { return Settings; }

	// The labels of the main buttons, top to bottom ("Play", "Join", "Settings", "Quit").
	TArray<FString> GetMainButtonLabels() const;
	// The main button with this label, or null.
	UCrossguardUIButton* FindMainButton(const FString& Label) const;

	// One step of a setting: Which 0 sensitivity (0.1), 1 field of view (5 degrees), 2 volume (10%);
	// Direction -1 or +1. Clamped; saved only by SaveSettings.
	void StepSetting(int32 Which, int32 Direction);

protected:
	virtual void NativeConstruct() override;
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	void ShowStatus(const FString& Message, const FLinearColor& Color);
	// Starts a travel to Url on this machine (like the "open" console command).
	void TravelTo(const FString& Url);
	UCrossguardSettingsSave* EnsureSettings();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCrossguardUIButton>> MainButtons;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> AddressField;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> SettingsPanel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SettingsText;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardSettingsSave> Settings;

	FString StatusLine;
	bool bSettingsOpen = false;
};
