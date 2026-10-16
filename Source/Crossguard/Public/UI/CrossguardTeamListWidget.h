#pragma once

#include "CoreMinimal.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardTeamListWidget.generated.h"

class UBorder;
class UVerticalBox;

// A small always-on list of your teammates (placeholder, docs/ui.md), top right: everyone on your
// team (the party in the hub, the raid team in a raid), with "(disconnected)" for a teammate whose
// player is away and whose character waits for a reconnect (ACrossguardPlayerState::IsDisconnected).
// Hidden without teammates. Display only; clicks go through to the game.
UCLASS()
class CROSSGUARD_API UCrossguardTeamListWidget : public UCrossguardWidgetBase
{
	GENERATED_BODY()

public:
	UCrossguardTeamListWidget(const FObjectInitializer& ObjectInitializer);

	// The lines shown by the last refresh, one per teammate ("Name" or "Name (disconnected)").
	const TArray<FString>& GetLines() const { return Lines; }

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Panel;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> List;

	TArray<FString> Lines;
};
