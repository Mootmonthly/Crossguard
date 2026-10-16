#pragma once

#include "CoreMinimal.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardHudWidget.generated.h"

class AActor;
class ACrossguardCharacter;
class UHorizontalBox;
class UProgressBar;
class UTextBlock;

// The always-on HUD (placeholder, docs/ui.md): Health and shield, status effects, level, raid XP
// and gold, the weapon set, hotbar keys 3-7, the selected skill and cooldowns, the interact prompt,
// a channel bar (gathering, lockpicking, extracting), the downed timer, spectating, and messages.
// Reads replicated state only; the bars that the server does not replicate (lockpicking,
// extraction zone, downed timer) are counted on this machine from when they started, so they are
// approximate.
UCLASS()
class CROSSGUARD_API UCrossguardHudWidget : public UCrossguardWidgetBase
{
	GENERATED_BODY()

public:
	UCrossguardHudWidget(const FObjectInitializer& ObjectInitializer);

	// A message under the crosshair for Seconds (refusals, trade results).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ShowMessage(const FString& Message, FLinearColor Color, float Seconds = 3.0f);

	// What the last Refresh showed (tests).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetHealthLine() const { return HealthLine; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetHotbarLine() const { return HotbarLine; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetStatusLine() const { return StatusLine; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetInteractPrompt() const { return InteractPrompt; }

	// "F: Loot body" for what F would use now; empty if nothing. Mirrors the character's choice
	// (nearest interactable in range, roughly in front), for display only.
	static FString DescribeInteraction(const ACrossguardCharacter* Character, const AActor* Target);
	static AActor* FindInteractTarget(const ACrossguardCharacter* Character);

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	void RefreshVitals(ACrossguardCharacter* Character);
	void RefreshHotbar(ACrossguardCharacter* Character);
	void RefreshSkills(ACrossguardCharacter* Character);
	void RefreshChannel(ACrossguardCharacter* Character);
	void RefreshDeathAndSpectating(ACrossguardCharacter* Character);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ShieldText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ProgressText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WeaponText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> HotbarTexts;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SkillText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PromptText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> ChannelBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChannelText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CenterText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;

	FString HealthLine;
	FString HotbarLine;
	FString StatusLine;
	FString InteractPrompt;

	// Local clocks for bars the server does not replicate (world seconds; < 0: not running).
	double DownedSince = -1.0;
	double LockpickSince = -1.0;
	double ExtractZoneSince = -1.0;
	double MessageUntil = 0.0;
	// The interact prompt walks every actor: not on every refresh.
	double NextPromptUpdate = 0.0;
};
