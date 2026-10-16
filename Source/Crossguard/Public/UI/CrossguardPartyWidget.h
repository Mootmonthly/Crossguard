#pragma once

#include "CoreMinimal.h"
#include "Game/CrossguardPartyComponent.h"
#include "UI/CrossguardWidgetBase.h"

#include "CrossguardPartyWidget.generated.h"

class APlayerState;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

// The party window (placeholder, docs/ui.md, docs/parties.md; key P): your party (members in join
// order, the leader marked, "(disconnected)" for a member held for a reconnect), invite a player by
// name, leave, kick and make leader (leader only), Start raid (leader or solo, hub only), and the
// invite inbox (inviter, members, time left of 180 s; Accept, Decline, Decline all). Greyed buttons
// are hints only: the server decides (UCrossguardPartyComponent requests); refusals show in the
// window.
UCLASS()
class CROSSGUARD_API UCrossguardPartyWidget : public UCrossguardWindowWidget
{
	GENERATED_BODY()

public:
	UCrossguardPartyWidget(const FObjectInitializer& ObjectInitializer);

	// The status line for a refused party request ("Your party is already looking for a raid.").
	static FString RefusalText(ECrossguardPartyResult Result);

	// Rows drawn by the last redraw (tests).
	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetMemberRowCount() const { return MemberRows; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	int32 GetInviteRowCount() const { return InviteRows; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	FString GetHeaderLine() const { return HeaderLine; }

	// The member lines as shown ("Name (you) [leader] (disconnected)"), in join order (tests).
	const TArray<FString>& GetMemberLines() const { return MemberLines; }

	// What the Invite button does with the name field: invites the player with that name (any case)
	// on this server. False (and a status line) if there is none.
	bool InviteByName(const FString& PlayerName);

	// The online player state whose player id (UCrossguardGuildSubsystem::PlayerIdOf) is PlayerId, or
	// null (offline, or not replicated here). Used for the "(disconnected)" marker.
	APlayerState* FindPlayerState(const FString& PlayerId) const;

	virtual void OnWindowOpened() override;

protected:
	virtual void BuildTree() override;
	virtual void NativeRefresh() override;

	UFUNCTION()
	void HandlePartyRefused(ECrossguardPartyResult Result);

private:
	UCrossguardPartyComponent* GetParty() const;
	void BindParty();
	FString BuildSignature() const;
	void Redraw();
	// Updates the inbox rows' "time left" without rebuilding them.
	void UpdateCountdowns();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MemberList;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ActionList;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> InviteList;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NameField;

	// Inbox rows' countdown texts, by party id.
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTextBlock>> CountdownTexts;

	TWeakObjectPtr<UCrossguardPartyComponent> BoundParty;
	FString HeaderLine;
	FString LastSignature;
	TArray<FString> MemberLines;
	int32 MemberRows = 0;
	int32 InviteRows = 0;
};
