#pragma once

#include "CoreMinimal.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardUIButton.generated.h"

class UBorder;
class UTextBlock;

// A clickable box with a label: the placeholder UI's button, item cell and grid square. Left and
// right clicks and hovering call the C++ callbacks set by the window that made it.
UCLASS()
class CROSSGUARD_API UCrossguardUIButton : public UCrossguardWidgetBase
{
	GENERATED_BODY()

public:
	UCrossguardUIButton(const FObjectInitializer& ObjectInitializer);

	void SetLabel(const FString& InLabel);
	void SetFontSize(int32 Size);
	// Space between the box's edge and its label (default 5 x 2).
	void SetLabelPadding(const FMargin& InPadding);
	// True: the label wraps at the box's width (item cells with a long name).
	void SetLabelWrap(bool bWrap);
	// Background colour (the hover highlight brightens it).
	void SetColor(const FLinearColor& InColor);
	void SetLabelColor(const FLinearColor& InColor);
	// False: greyed out and clicks do nothing (the server would refuse anyway, e.g. a Member
	// withdrawing from the guild bank).
	void SetActive(bool bInActive);

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetLabel() const { return Label; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	bool IsActive() const { return bActive; }

	// Runs the left-click callback (tests, gamepad later).
	void Click();

	TFunction<void()> OnClicked;
	TFunction<void()> OnRightClicked;
	TFunction<void()> OnHovered;
	// Set: a left press starts a drag once the mouse moves (the click then fires on release instead
	// of on press). Returns the operation, or null for no drag.
	TFunction<UDragDropOperation*(const FGeometry&, const FPointerEvent&)> OnDragStart;
	// Set: something dropped on this box. Returns true if it took it.
	TFunction<bool(UDragDropOperation*)> OnDropped;

protected:
	virtual void BuildTree() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	void ApplyLook();

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Background;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LabelText;

	FString Label;
	FLinearColor Color = FLinearColor(0.12f, 0.12f, 0.14f, 0.95f);
	FLinearColor LabelColor = FLinearColor::White;
	int32 FontSize = 11;
	bool bActive = true;
	bool bHovered = false;
};
