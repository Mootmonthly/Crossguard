#pragma once

#include "CoreMinimal.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardSkillWheelWidget.generated.h"

class UCanvasPanel;
class UCrossguardUIButton;
class UTextBlock;

// The skill wheel (hold Q; placeholder for the radial art): the castable skills (Active, Spell,
// Art) in a ring around the crosshair with their cooldowns. Hovering one selects it
// (UCrossguardLoadoutComponent::SetSelectedSkill, local to this client); releasing Q closes the
// wheel and E casts the selection. The HUD shows it while the loadout's wheel is open.
UCLASS()
class CROSSGUARD_API UCrossguardSkillWheelWidget : public UCrossguardWidgetBase
{
	GENERATED_BODY()

public:
	UCrossguardSkillWheelWidget(const FObjectInitializer& ObjectInitializer);

	// Entries drawn by the last Refresh.
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetEntryCount() const { return Entries.Num(); }

	// Selects the skill of entry Index, as hovering it does (tests, gamepad later).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|UI")
	void SelectEntry(int32 Index);

	// Pixels from the centre to the entries.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crossguard|UI")
	float Radius = 170.0f;

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Ring;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CenterText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCrossguardUIButton>> Entries;

	// The skills the entries were built for (rebuilt when they change).
	TArray<FName> EntrySkills;
};
