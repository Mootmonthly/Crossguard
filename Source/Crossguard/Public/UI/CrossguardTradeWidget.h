#pragma once

#include "CoreMinimal.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardTradeWidget.generated.h"

class UCrossguardTradeComponent;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

// The trade window (placeholder, docs/ui.md, docs/trading.md): ask the nearest player, answer a
// request, then both offers side by side: click your stacks to offer or take them back, set gold,
// lock, accept. Every button is a request to UCrossguardTradeComponent; the server validates it and
// swaps everything or nothing.
UCLASS()
class CROSSGUARD_API UCrossguardTradeWidget : public UCrossguardWindowWidget
{
	GENERATED_BODY()

public:
	UCrossguardTradeWidget(const FObjectInitializer& ObjectInitializer);

	// The status line of the last Refresh ("No trade", "Waiting for ...", ...).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetStateLine() const { return StateLine; }

	// Rows of your items, your offer and their offer after the last Refresh.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetMyItemRowCount() const { return MyItemRows; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetMyOfferRowCount() const { return MyOfferRows; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetTheirOfferRowCount() const { return TheirOfferRows; }

	// Asks the nearest player within the trade distance (the server checks it again).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void AskNearestPlayer();

	// Adds or removes one of your stacks from your offer (the whole offer is re-sent).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ToggleOffered(int32 ItemId);

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	UCrossguardTradeComponent* GetTrade() const;
	FString BuildSignature() const;
	void Redraw();
	int32 ReadGoldField() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StateText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Controls;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MyItemsList;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MyOfferList;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> TheirOfferList;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> GoldField;

	FString StateLine;
	FString LastSignature;
	int32 MyItemRows = 0;
	int32 MyOfferRows = 0;
	int32 TheirOfferRows = 0;
};
