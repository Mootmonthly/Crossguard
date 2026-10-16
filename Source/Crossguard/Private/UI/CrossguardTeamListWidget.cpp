#include "UI/CrossguardTeamListWidget.h"

#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/GameStateBase.h"

UCrossguardTeamListWidget::UCrossguardTeamListWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RefreshInterval = 0.5f;
}

void UCrossguardTeamListWidget::BuildTree()
{
	// Display only: clicks go through to the game.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanel* Root = Make<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	Panel = MakePanel(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), 6.0f);
	List = MakeColumn();
	Panel->SetContent(List);
	// Top right, clear of the engine's debug text (top left) and the HUD's centre lines.
	UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(1.0f, 0.0f));
	PanelSlot->SetAlignment(FVector2D(1.0f, 0.0f));
	PanelSlot->SetPosition(FVector2D(-16.0f, 16.0f));
	PanelSlot->SetAutoSize(true);
	Panel->SetVisibility(ESlateVisibility::Collapsed);
}

void UCrossguardTeamListWidget::NativeRefresh()
{
	const ACrossguardPlayerState* Self = GetCrossguardPlayerState();
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	TArray<FString> NewLines;
	TArray<bool> Away;
	if (Self != nullptr && GameState != nullptr && Self->GetTeamId() >= 0)
	{
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			const ACrossguardPlayerState* Mate = Cast<ACrossguardPlayerState>(Other);
			if (Mate == nullptr || Mate == Self || Mate->GetTeamId() != Self->GetTeamId())
			{
				continue;
			}
			NewLines.Add(Mate->IsDisconnected() ? Mate->GetPlayerName() + TEXT(" (disconnected)") : Mate->GetPlayerName());
			Away.Add(Mate->IsDisconnected());
		}
	}
	// The away state is in the text, so equal lines draw the same.
	if (NewLines == Lines)
	{
		return;
	}
	Lines = MoveTemp(NewLines);
	List->ClearChildren();
	if (Lines.IsEmpty())
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	AddTo(List, MakeText(TEXT("Team"), 12, FLinearColor(1.0f, 0.85f, 0.5f)));
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		AddTo(List, MakeText(Lines[Index], 11, Away[Index] ? FLinearColor(1.0f, 0.7f, 0.35f) : FLinearColor::White));
	}
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
}
