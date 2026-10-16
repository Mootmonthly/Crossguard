#include "UI/CrossguardSkillWheelWidget.h"

#include "Character/CrossguardCharacter.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "UI/CrossguardUIButton.h"
#include "UI/CrossguardUIHelpers.h"

UCrossguardSkillWheelWidget::UCrossguardSkillWheelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Cooldowns tick down while it is open.
	RefreshInterval = 0.1f;
}

void UCrossguardSkillWheelWidget::BuildTree()
{
	SetVisibility(ESlateVisibility::Visible);
	Ring = Make<UCanvasPanel>();
	WidgetTree->RootWidget = Ring;
	CenterText = MakeText(TEXT("No skills"), 14, FLinearColor(1.0f, 0.85f, 0.5f));
	CenterText->SetJustification(ETextJustify::Center);
	UCanvasPanelSlot* CenterSlot = Ring->AddChildToCanvas(CenterText);
	CenterSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	CenterSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CenterSlot->SetAutoSize(true);
}

void UCrossguardSkillWheelWidget::SelectEntry(int32 Index)
{
	UCrossguardLoadoutComponent* Loadout = GetCrossguardCharacter() != nullptr ? GetCrossguardCharacter()->GetLoadoutComponent() : nullptr;
	if (Loadout != nullptr && EntrySkills.IsValidIndex(Index))
	{
		Loadout->SetSelectedSkill(EntrySkills[Index]);
		Refresh();
	}
}

void UCrossguardSkillWheelWidget::NativeRefresh()
{
	ACrossguardCharacter* Character = GetCrossguardCharacter();
	UCrossguardLoadoutComponent* Loadout = Character != nullptr ? Character->GetLoadoutComponent() : nullptr;
	const TArray<FName> Skills = Loadout != nullptr ? Loadout->GetActivatableSkills() : TArray<FName>();

	if (Skills != EntrySkills)
	{
		for (UCrossguardUIButton* Entry : Entries)
		{
			if (Entry != nullptr)
			{
				Entry->RemoveFromParent();
			}
		}
		Entries.Reset();
		EntrySkills = Skills;
		for (int32 Index = 0; Index < Skills.Num(); ++Index)
		{
			UCrossguardUIButton* Entry = MakeButton(FString(), nullptr, 12);
			// Hover selects (docs/design-decisions.md, Controls: "hover an activatable skill").
			Entry->OnHovered = [this, Index]() { SelectEntry(Index); };
			Entry->OnClicked = [this, Index]() { SelectEntry(Index); };
			const float Angle = 2.0f * UE_PI * Index / Skills.Num() - 0.5f * UE_PI;
			UCanvasPanelSlot* EntrySlot = Ring->AddChildToCanvas(Entry);
			EntrySlot->SetAnchors(FAnchors(0.5f, 0.5f));
			EntrySlot->SetAlignment(FVector2D(0.5f, 0.5f));
			EntrySlot->SetPosition(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			EntrySlot->SetSize(FVector2D(130.0f, 48.0f));
			Entries.Add(Entry);
		}
	}

	const UAbilitySystemComponent* ASC = Character != nullptr ? Character->GetAbilitySystemComponent() : nullptr;
	const FName Selected = Loadout != nullptr ? Loadout->GetSelectedSkill() : NAME_None;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		UCrossguardUIButton* Entry = Entries[Index];
		if (Entry == nullptr || !EntrySkills.IsValidIndex(Index))
		{
			continue;
		}
		const FName Skill = EntrySkills[Index];
		const float Remaining = CrossguardUI::SkillCooldownRemaining(ASC, Skill);
		const FString Name = CrossguardUI::SkillName(Skill, Loadout);
		Entry->SetLabel(Remaining > 0.0f ? FString::Printf(TEXT("%s\n%.1f s"), *Name, Remaining) : Name);
		Entry->SetColor(Skill == Selected ? FLinearColor(0.75f, 0.6f, 0.15f, 0.95f) : FLinearColor(0.1f, 0.1f, 0.14f, 0.9f));
		Entry->SetLabelColor(Remaining > 0.0f ? FLinearColor(0.6f, 0.6f, 0.6f) : FLinearColor::White);
	}
	SetText(CenterText, Skills.IsEmpty() ? FString(TEXT("No skills to cast"))
		: Selected.IsNone() ? FString(TEXT("Hover a skill"))
		: FString::Printf(TEXT("E: %s"), *CrossguardUI::SkillName(Selected, Loadout)));
}
