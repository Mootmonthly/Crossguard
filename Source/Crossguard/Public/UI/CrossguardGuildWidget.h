#pragma once

#include "CoreMinimal.h"
#include "Game/CrossguardGuildComponent.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardGuildWidget.generated.h"

class UCrossguardUIButton;
class UHorizontalBox;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

// The guild window (placeholder, docs/ui.md, docs/guild-rules.md): create a guild or answer
// invites; the roster with ranks and the buttons your rank allows (invite, kick, promote, demote,
// make leader, leave); the bank's gold and items with deposit and withdraw. Buttons your rank does
// not allow are greyed out: a hint only, the server decides (UCrossguardGuildComponent requests).
UCLASS()
class CROSSGUARD_API UCrossguardGuildWidget : public UCrossguardWindowWidget
{
	GENERATED_BODY()

public:
	UCrossguardGuildWidget(const FObjectInitializer& ObjectInitializer);

	// Rows drawn by the last Refresh (tests).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetMemberRowCount() const { return MemberRows; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetBankRowCount() const { return BankRows; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetHeaderLine() const { return HeaderLine; }

	// Shows a guild refusal (ACrossguardHUD forwards OnGuildRefused).
	void ShowRefusal(ECrossguardGuildResult Result);

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	UCrossguardGuildComponent* GetGuild() const;
	FString BuildSignature() const;
	void Redraw();
	int32 ReadAmountField() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PermissionText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> TopControls;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MemberList;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> BankList;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> DepositList;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NameField;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> AmountField;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> CreateRow;

	UPROPERTY(Transient)
	TObjectPtr<UCrossguardUIButton> WithdrawGoldButton;

	FString HeaderLine;
	FString LastSignature;
	int32 MemberRows = 0;
	int32 BankRows = 0;
};
