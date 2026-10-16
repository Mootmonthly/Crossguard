#pragma once

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "CoreMinimal.h"
#include "Game/CrossguardPlayerState.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardAttributesWidget.generated.h"

class UCrossguardUIButton;
class UTextBlock;
class UVerticalBox;

// The attributes window (placeholder, docs/ui.md; key K): level and attribute points (total,
// unspent), the current allocation and a draft with - / + per attribute, and the requests: Apply
// (spends unspent points: free), Respec with a token, Respec for <price> gold (the draft lowers an
// attribute). Buttons are greyed when the draft is unchanged or invalid, not affordable, or during a
// raid: hints only. The server decides (ACrossguardPlayerState::RequestAttributeAllocation);
// refusals show in the window.
UCLASS()
class CROSSGUARD_API UCrossguardAttributesWidget : public UCrossguardWindowWidget
{
	GENERATED_BODY()

public:
	UCrossguardAttributesWidget(const FObjectInitializer& ObjectInitializer);

	// The draft (what the requests ask for). Attribute: 0 Strength, 1 Agility, 2 Intelligence.
	// Never below 0 nor above the level's points in total.
	void AdjustDraft(int32 Attribute, int32 Delta);
	// Back to the current allocation.
	void ResetDraft();
	// Every point unassigned (a full respec once applied).
	void ClearDraft();
	const FCrossguardAttributeAllocation& GetDraft() const { return Draft; }

	// The buttons. Each sends the draft; the server checks and pays.
	void Apply();
	void RespecWithToken();
	void RespecWithGold();

	// Display hints, as the buttons show them.
	// The draft lowers an attribute: a respec (a token or gold), not a free Apply.
	bool DraftNeedsRespec() const;
	bool CanApply() const;
	bool CanRespecWithToken() const;
	bool CanRespecWithGold() const;

	// "Level 3: 36 points, 2 unspent" (tests).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetPointsLine() const { return PointsLine; }

	virtual void OnWindowOpened() override;

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

	UFUNCTION()
	void HandleProgressionRejected(ECrossguardProgressionError Error);

private:
	// Attribute points at the player's level; 0 without a player state.
	int32 PointsAtLevel() const;
	bool DraftIsValid() const;
	bool DraftDiffers() const;
	bool InRaid() const;
	void BindProgression();
	void Redraw();
	void Send(bool bPayWithGold);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PointsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> AttributeTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCrossguardUIButton>> MinusButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCrossguardUIButton>> PlusButtons;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardUIButton> ApplyButton;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardUIButton> TokenButton;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardUIButton> GoldButton;

	TWeakObjectPtr<ACrossguardPlayerState> BoundProgression;
	FCrossguardAttributeAllocation Draft;
	// The allocation the draft was last reset from: when the server's changes, the draft follows.
	FCrossguardAttributeAllocation DraftBase;
	bool bHasDraft = false;
	FString PointsLine;
};
