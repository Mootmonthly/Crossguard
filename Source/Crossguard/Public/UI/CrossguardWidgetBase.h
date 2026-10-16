#pragma once

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "CoreMinimal.h"

#include "CrossguardWidgetBase.generated.h"

class ACrossguardCharacter;
class ACrossguardPlayerState;
class UBorder;
class UCanvasPanel;
class UCrossguardUIButton;
class UEditableTextBox;
class UHorizontalBox;
class UPanelWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

// Base of the placeholder UI (docs/ui.md). The widget tree is built in C++ (BuildTree) when the
// class has none of its own, so no assets are needed. A Widget Blueprint subclass with its own
// design keeps its design instead (the C++ tree is then not built; its graph reads the same
// components). Refresh redraws from the owning player's components; while visible, NativeTick
// refreshes every RefreshInterval seconds. Display only: every action is a request to a component,
// which the server validates.
UCLASS(Abstract)
class CROSSGUARD_API UCrossguardWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	// Redraws from the game state now.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void Refresh();

	// The owning player's character; null while spectating or dead for the raid.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	ACrossguardCharacter* GetCrossguardCharacter() const;

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	ACrossguardPlayerState* GetCrossguardPlayerState() const;

	// True if the C++ tree was built (false for a Blueprint subclass with its own design).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	bool IsBuiltInCode() const { return bBuiltInCode; }

	// Seconds between refreshes while visible. 0: only when Refresh is called.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crossguard|UI")
	float RefreshInterval = 0.25f;

	// After every Refresh (Blueprint subclasses draw here).
	UFUNCTION(BlueprintImplementableEvent, Category = "Crossguard|UI")
	void OnRefreshed();

	// Builds the C++ tree now if the class has none (the first Refresh or TakeWidget does it too).
	void EnsureTree();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Puts the tree into WidgetTree (set WidgetTree->RootWidget). Called once.
	virtual void BuildTree() {}
	// Redraws the C++ tree. Only called when it was built.
	virtual void NativeRefresh() {}

	// Construction helpers for BuildTree and NativeRefresh.
	template <typename WidgetT>
	WidgetT* Make()
	{
		return WidgetTree->ConstructWidget<WidgetT>(WidgetT::StaticClass());
	}
	UTextBlock* MakeText(const FString& Text, int32 FontSize = 12, const FLinearColor& Color = FLinearColor::White);
	UCrossguardUIButton* MakeButton(const FString& Label, TFunction<void()> OnClicked, int32 FontSize = 11);
	UBorder* MakePanel(const FLinearColor& Background, float PanelPadding = 6.0f);
	UVerticalBox* MakeColumn();
	UHorizontalBox* MakeRow();
	UEditableTextBox* MakeTextField(const FString& Hint);
	// Adds Child to a vertical or horizontal box with a small gap; any other panel: AddChild.
	static void AddTo(UPanelWidget* Parent, UWidget* Child, float Gap = 2.0f);
	static void SetText(UTextBlock* Text, const FString& Value);

private:
	bool bTreeChecked = false;
	bool bBuiltInCode = false;
	float SinceRefresh = 0.0f;
};

// A full-screen window (inventory, trade, guild): a dark panel in the middle with a title and a
// close button. Clicks anywhere on it stay in the UI (no attacks while it is open). The HUD opens
// and closes it and switches the mouse cursor (ACrossguardHUD).
UCLASS(Abstract)
class CROSSGUARD_API UCrossguardWindowWidget : public UCrossguardWidgetBase
{
	GENERATED_BODY()

public:
	// Asks the HUD to close it (or just hides it without a Crossguard HUD).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void RequestClose();

	// A line of feedback at the bottom of the window (refusals, hints).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void ShowStatus(const FString& Message, FLinearColor Color);

	// The HUD tells it it opened or closed.
	virtual void OnWindowOpened() {}
	virtual void OnWindowClosed() {}

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// Builds the frame: full-screen root, centered panel of Size, title, close button. Subclasses
	// call it first in BuildTree and fill Body.
	void BuildFrame(const FString& Title, FVector2D Size);

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Body;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;
};
