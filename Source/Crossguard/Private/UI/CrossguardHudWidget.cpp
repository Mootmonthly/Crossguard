#include "UI/CrossguardHudWidget.h"

#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "CrossguardRules/Config.h"
#include "CrossguardRules/Economy.h"
#include "EngineUtils.h"
#include "Game/CrossguardExtractionZone.h"
#include "Game/CrossguardExtractPortal.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CrossguardDoor.h"
#include "Interaction/CrossguardGatherNode.h"
#include "Interaction/CrossguardInteractable.h"
#include "Interaction/CrossguardLockComponent.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardLootContainer.h"
#include "Items/CrossguardWorldItem.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "UI/CrossguardUIHelpers.h"

namespace CrossguardHudWidgetLocal
{
	// Data/Items/Items.json: carried gold.
	FName GoldCoinId() { return FName(TEXT("gold_coin")); }

	// How far an extract portal's countdown shows, in cm.
	constexpr float PortalShowDistance = 1500.0f;

	struct FStatusName
	{
		FGameplayTag Tag;
		const TCHAR* Name;
	};
}

UCrossguardHudWidget::UCrossguardHudWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RefreshInterval = 0.1f;
}

void UCrossguardHudWidget::BuildTree()
{
	// Display only: clicks go through to the game.
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanel* Root = Make<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	const auto Pin = [Root](UWidget* Widget, FVector2D Anchor, FVector2D Alignment, FVector2D Offset)
	{
		UCanvasPanelSlot* PinSlot = Root->AddChildToCanvas(Widget);
		PinSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		PinSlot->SetAlignment(Alignment);
		PinSlot->SetPosition(Offset);
		PinSlot->SetAutoSize(true);
	};
	const FLinearColor Backdrop(0.0f, 0.0f, 0.0f, 0.45f);

	// Crosshair.
	UTextBlock* Crosshair = MakeText(TEXT("+"), 18);
	Pin(Crosshair, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector);

	// Bottom left: vitals and progress (the engine's debug text uses the top left).
	UBorder* VitalsPanel = MakePanel(Backdrop, 6.0f);
	UVerticalBox* Vitals = MakeColumn();
	VitalsPanel->SetContent(Vitals);
	HealthText = MakeText(TEXT("HP"), 14, FLinearColor(0.6f, 1.0f, 0.6f));
	AddTo(Vitals, HealthText);
	USizeBox* HealthBarBox = Make<USizeBox>();
	HealthBarBox->SetWidthOverride(240.0f);
	HealthBarBox->SetHeightOverride(12.0f);
	HealthBar = Make<UProgressBar>();
	HealthBar->SetFillColorAndOpacity(FLinearColor(0.75f, 0.1f, 0.1f));
	HealthBarBox->SetContent(HealthBar);
	AddTo(Vitals, HealthBarBox);
	ShieldText = MakeText(FString(), 12, FLinearColor(0.5f, 0.8f, 1.0f));
	AddTo(Vitals, ShieldText);
	StatusText = MakeText(FString(), 12, FLinearColor(1.0f, 0.85f, 0.3f));
	AddTo(Vitals, StatusText);
	ProgressText = MakeText(FString(), 11, FLinearColor(0.85f, 0.85f, 0.85f));
	AddTo(Vitals, ProgressText);
	Pin(VitalsPanel, FVector2D(0.0f, 1.0f), FVector2D(0.0f, 1.0f), FVector2D(16.0f, -16.0f));

	// Bottom centre: weapon set, hotbar 3-7, skills.
	UBorder* BarPanel = MakePanel(Backdrop, 6.0f);
	UVerticalBox* Bar = MakeColumn();
	BarPanel->SetContent(Bar);
	WeaponText = MakeText(FString(), 12);
	AddTo(Bar, WeaponText);
	UHorizontalBox* Hotbar = MakeRow();
	AddTo(Bar, Hotbar);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		UBorder* SlotBox = MakePanel(FLinearColor(0.1f, 0.1f, 0.12f, 0.9f), 4.0f);
		USizeBox* SlotSize = Make<USizeBox>();
		SlotSize->SetWidthOverride(96.0f);
		SlotSize->SetHeightOverride(40.0f);
		UTextBlock* SlotText = MakeText(FString::Printf(TEXT("%d: -"), Index + 3), 10);
		SlotText->SetAutoWrapText(true);
		SlotSize->SetContent(SlotText);
		SlotBox->SetContent(SlotSize);
		AddTo(Hotbar, SlotBox, 4.0f);
		HotbarTexts.Add(SlotText);
	}
	SkillText = MakeText(FString(), 11, FLinearColor(0.75f, 0.85f, 1.0f));
	AddTo(Bar, SkillText);
	Pin(BarPanel, FVector2D(0.5f, 1.0f), FVector2D(0.5f, 1.0f), FVector2D(0.0f, -16.0f));

	// Under the crosshair: interact prompt, channel bar, messages.
	UVerticalBox* Center = MakeColumn();
	PromptText = MakeText(FString(), 14);
	AddTo(Center, PromptText);
	USizeBox* ChannelBox = Make<USizeBox>();
	ChannelBox->SetWidthOverride(260.0f);
	ChannelBox->SetHeightOverride(10.0f);
	ChannelBar = Make<UProgressBar>();
	ChannelBar->SetFillColorAndOpacity(FLinearColor(0.9f, 0.8f, 0.3f));
	ChannelBox->SetContent(ChannelBar);
	AddTo(Center, ChannelBox);
	ChannelText = MakeText(FString(), 11);
	AddTo(Center, ChannelText);
	MessageText = MakeText(FString(), 13, FLinearColor(1.0f, 0.6f, 0.3f));
	AddTo(Center, MessageText);
	Pin(Center, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 40.0f));

	// Top centre: downed, dead, spectating.
	CenterText = MakeText(FString(), 20, FLinearColor(1.0f, 0.35f, 0.35f));
	Pin(CenterText, FVector2D(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 60.0f));
}

void UCrossguardHudWidget::ShowMessage(const FString& Message, FLinearColor Color, float Seconds)
{
	EnsureTree();
	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FSlateColor(Color));
	}
	const UWorld* World = GetWorld();
	MessageUntil = (World != nullptr ? World->GetTimeSeconds() : 0.0) + Seconds;
}

void UCrossguardHudWidget::NativeRefresh()
{
	ACrossguardCharacter* Character = GetCrossguardCharacter();
	RefreshVitals(Character);
	RefreshHotbar(Character);
	RefreshSkills(Character);
	RefreshChannel(Character);
	RefreshDeathAndSpectating(Character);

	const UWorld* World = GetWorld();
	if (MessageText != nullptr && World != nullptr && World->GetTimeSeconds() > MessageUntil)
	{
		MessageText->SetText(FText::GetEmpty());
	}
}

void UCrossguardHudWidget::RefreshVitals(ACrossguardCharacter* Character)
{
	const UAbilitySystemComponent* ASC = Character != nullptr ? Character->GetAbilitySystemComponent() : nullptr;
	if (ASC == nullptr)
	{
		HealthLine.Reset();
		StatusLine.Reset();
		SetText(HealthText, FString());
		SetText(ShieldText, FString());
		SetText(StatusText, FString());
		if (HealthBar != nullptr)
		{
			HealthBar->SetPercent(0.0f);
		}
	}
	else
	{
		const float Health = ASC->GetNumericAttribute(UCrossguardAttributeSet::GetHealthAttribute());
		const float MaxHealth = ASC->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute());
		const float Shield = ASC->GetNumericAttribute(UCrossguardAttributeSet::GetAttackShieldAttribute());
		HealthLine = FString::Printf(TEXT("HP %.0f / %.0f"), Health, MaxHealth);
		SetText(HealthText, HealthLine);
		if (HealthBar != nullptr)
		{
			HealthBar->SetPercent(MaxHealth > 0.0f ? FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f) : 0.0f);
		}
		SetText(ShieldText, Shield > 0.0f ? FString::Printf(TEXT("Shield %.0f"), Shield) : FString());

		using CrossguardHudWidgetLocal::FStatusName;
		const FStatusName States[] = {
			{ CrossguardTags::State_Stunned, TEXT("Stunned") },
			{ CrossguardTags::State_Rooted, TEXT("Rooted") },
			{ CrossguardTags::State_Silenced, TEXT("Silenced") },
			{ CrossguardTags::State_Muted, TEXT("Muted") },
			{ CrossguardTags::State_Slowed, TEXT("Slowed") },
			{ CrossguardTags::State_Blinded, TEXT("Blinded") },
			{ CrossguardTags::State_KnockedUp, TEXT("Knocked up") },
			{ CrossguardTags::State_HealBlocked, TEXT("Heal blocked") },
			{ CrossguardTags::State_Stealthed, TEXT("Stealthed") },
			{ CrossguardTags::State_CCImmune, TEXT("CC immune") },
			{ CrossguardTags::State_Flying, TEXT("Flying") },
			{ CrossguardTags::State_Dragonified, TEXT("Dragon form") },
			{ CrossguardTags::State_Multiscale, TEXT("Multiscale") },
			{ CrossguardTags::State_Guarding, TEXT("Guarding") },
		};
		TArray<FString> Active;
		for (const FStatusName& State : States)
		{
			if (ASC->HasMatchingGameplayTag(State.Tag))
			{
				Active.Add(State.Name);
			}
		}
		StatusLine = FString::Join(Active, TEXT(", "));
		SetText(StatusText, StatusLine);
	}

	const ACrossguardPlayerState* PlayerState = GetCrossguardPlayerState();
	const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	FString Progress;
	if (Character != nullptr)
	{
		Progress += FString::Printf(TEXT("Level %d"), Character->Level);
	}
	if (PlayerState != nullptr)
	{
		const int32 Carried = Inventory != nullptr ? Inventory->CountItem(CrossguardHudWidgetLocal::GoldCoinId()) : 0;
		Progress += PlayerState->IsInRaid()
			? FString::Printf(TEXT("   Raid XP %d   Gold carried %d (banked %d)"), PlayerState->GetRaidXp(), Carried, PlayerState->GetGold())
			: FString::Printf(TEXT("   XP %d   Gold %d"), PlayerState->GetTotalXp(), PlayerState->GetGold());
	}
	SetText(ProgressText, Progress);
}

void UCrossguardHudWidget::RefreshHotbar(ACrossguardCharacter* Character)
{
	const UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;

	// Weapon set and what is in hand.
	FString Weapons;
	if (Equipment != nullptr && Inventory != nullptr)
	{
		const int32 Active = Equipment->GetActiveWeaponSet();
		for (int32 Set = 0; Set < 2; ++Set)
		{
			const FString Main = CrossguardUI::WeaponName(Equipment->GetWeaponInSet(Set, ECrossguardEquipmentSlot::MainHand), Equipment);
			const FString Off = CrossguardUI::WeaponName(Equipment->GetWeaponInSet(Set, ECrossguardEquipmentSlot::OffHand), Equipment);
			Weapons += FString::Printf(TEXT("%s%d: %s / %s%s   "), Set == Active ? TEXT("[") : TEXT(""), Set + 1,
				Main.IsEmpty() ? TEXT("-") : *Main, Off.IsEmpty() ? TEXT("-") : *Off, Set == Active ? TEXT("]") : TEXT(""));
		}
		FCrossguardInventoryItem InHand;
		if (Inventory->GetReadiedItemId() != 0 && Inventory->FindItem(Inventory->GetReadiedItemId(), InHand))
		{
			Weapons += FString::Printf(TEXT("In hand: %s"), *CrossguardUI::ItemName(InHand, Inventory, Equipment));
		}
		else
		{
			Weapons += Equipment->IsDrawingWeapons() ? TEXT("Drawing...") : Equipment->AreWeaponsReady() ? TEXT("Weapons ready") : TEXT("Hands empty (1 / 2)");
		}
	}
	SetText(WeaponText, Weapons);

	// Hotbar keys 3-7.
	HotbarLine.Reset();
	FCrossguardInventoryItem Readied;
	const bool bHasReadied = Inventory != nullptr && Inventory->GetReadiedItemId() != 0 && Inventory->FindItem(Inventory->GetReadiedItemId(), Readied);
	for (int32 Index = 0; Index < HotbarTexts.Num(); ++Index)
	{
		FString Line = FString::Printf(TEXT("%d: -"), Index + 3);
		bool bInHand = false;
		if (Inventory != nullptr && Inventory->GetHotbar().IsValidIndex(Index) && !Inventory->GetHotbar()[Index].IsEmpty())
		{
			const FCrossguardHotbarSlot& HotbarSlot = Inventory->GetHotbar()[Index];
			int32 Count = 0;
			for (const FCrossguardInventoryItem& Item : Inventory->GetItems())
			{
				if (Item.Category == HotbarSlot.Category && Item.ItemId == HotbarSlot.ItemId && Item.Rarity == HotbarSlot.Rarity)
				{
					Count += Item.Count;
				}
			}
			FCrossguardInventoryItem Shown;
			Shown.Category = HotbarSlot.Category;
			Shown.ItemId = HotbarSlot.ItemId;
			Shown.Rarity = HotbarSlot.Rarity;
			Line = FString::Printf(TEXT("%d: %s x%d"), Index + 3, *CrossguardUI::ItemName(Shown, Inventory, Equipment), Count);
			bInHand = bHasReadied && Readied.ItemId == HotbarSlot.ItemId && Readied.Rarity == HotbarSlot.Rarity;
		}
		if (UTextBlock* Text = HotbarTexts[Index])
		{
			Text->SetText(FText::FromString(Line));
			Text->SetColorAndOpacity(FSlateColor(bInHand ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor::White));
		}
		HotbarLine += (Index > 0 ? TEXT(" | ") : TEXT("")) + Line;
	}
}

void UCrossguardHudWidget::RefreshSkills(ACrossguardCharacter* Character)
{
	const UCrossguardLoadoutComponent* Loadout = Character != nullptr ? Character->GetLoadoutComponent() : nullptr;
	const UAbilitySystemComponent* ASC = Character != nullptr ? Character->GetAbilitySystemComponent() : nullptr;
	if (Loadout == nullptr)
	{
		SetText(SkillText, FString());
		return;
	}
	TArray<FString> Parts;
	for (const FName& Skill : Loadout->GetActivatableSkills())
	{
		const float Remaining = CrossguardUI::SkillCooldownRemaining(ASC, Skill);
		const FString Name = CrossguardUI::SkillName(Skill, Loadout);
		const FString Shown = Skill == Loadout->GetSelectedSkill() ? FString::Printf(TEXT("[E: %s]"), *Name) : Name;
		Parts.Add(Remaining > 0.0f ? FString::Printf(TEXT("%s %.1fs"), *Shown, Remaining) : Shown);
	}
	SetText(SkillText, Parts.IsEmpty() ? FString(TEXT("No skills (hold Q to pick, E to cast)")) : TEXT("Skills (hold Q): ") + FString::Join(Parts, TEXT("  ")));
}

AActor* UCrossguardHudWidget::FindInteractTarget(const ACrossguardCharacter* Character)
{
	if (Character == nullptr || Character->GetWorld() == nullptr)
	{
		return nullptr;
	}
	// Same choice as ACrossguardCharacter::FindInteractTarget (server): nearest in range, within
	// about 60 degrees of the view.
	FVector EyeLocation;
	FRotator EyeRotation;
	Character->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector Forward = EyeRotation.Vector();
	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(Character->GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		const ICrossguardInteractable* Interactable = Cast<ICrossguardInteractable>(Candidate);
		if (Interactable == nullptr || Candidate == Character)
		{
			continue;
		}
		const FVector ToCandidate = Candidate->GetActorLocation() - EyeLocation;
		const float Distance = FVector::Dist(Character->GetActorLocation(), Candidate->GetActorLocation());
		if (Distance <= Interactable->GetInteractRange() && Distance < BestDistance
			&& FVector::DotProduct(ToCandidate.GetSafeNormal(), Forward) >= 0.5f)
		{
			Best = Candidate;
			BestDistance = Distance;
		}
	}
	return Best;
}

FString UCrossguardHudWidget::DescribeInteraction(const ACrossguardCharacter* Character, const AActor* Target)
{
	if (Character == nullptr || Target == nullptr)
	{
		return FString();
	}
	const UCrossguardLockComponent* Lock = Target->FindComponentByClass<UCrossguardLockComponent>();
	if (Lock != nullptr && Lock->IsLocked())
	{
		return Character->IsLockpicking() ? TEXT("F: Stop picking the lock") : TEXT("F: Pick the lock");
	}
	if (const ACrossguardLootContainer* Container = Cast<ACrossguardLootContainer>(Target))
	{
		// Chests roll a loot table; bodies have none.
		return Container->LootTableId.IsNone() ? TEXT("F: Loot body") : TEXT("F: Open chest");
	}
	if (const ACrossguardGatherNode* Node = Cast<ACrossguardGatherNode>(Target))
	{
		if (Node->IsDepleted())
		{
			return TEXT("Depleted");
		}
		if (Node->GetGatherer() == Character)
		{
			return TEXT("F: Stop gathering");
		}
		return Node->IsBeingGathered() ? TEXT("Someone is gathering here") : TEXT("F: Gather");
	}
	if (const ACrossguardDoor* Door = Cast<ACrossguardDoor>(Target))
	{
		return Door->IsOpen() ? TEXT("F: Close door") : TEXT("F: Open door");
	}
	if (const ACrossguardWorldItem* WorldItem = Cast<ACrossguardWorldItem>(Target))
	{
		return FString::Printf(TEXT("F: Pick up %s"),
			*CrossguardUI::ItemLabel(WorldItem->GetItem(), Character->GetInventoryComponent(), Character->GetEquipmentComponent()));
	}
	// Others name themselves (stash chest, vendor NPC).
	if (const ICrossguardInteractable* Interactable = Cast<ICrossguardInteractable>(Target))
	{
		const FString Prompt = Interactable->GetInteractPrompt();
		if (!Prompt.IsEmpty())
		{
			return Prompt;
		}
	}
	return TEXT("F: Use");
}

void UCrossguardHudWidget::RefreshChannel(ACrossguardCharacter* Character)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	if (Now >= NextPromptUpdate)
	{
		NextPromptUpdate = Now + 0.2;
		InteractPrompt = DescribeInteraction(Character, FindInteractTarget(Character));
		SetText(PromptText, InteractPrompt);
	}

	FString Channel;
	float Progress = -1.0f;
	if (Character != nullptr)
	{
		// Gathering: the node replicates its start time.
		for (TActorIterator<ACrossguardGatherNode> It(World); It; ++It)
		{
			if (It->GetGatherer() == Character)
			{
				Channel = TEXT("Gathering");
				Progress = It->GetGatherProgress();
				break;
			}
		}

		// Lockpicking: only "in progress" replicates; the time is estimated here from Dexterity.
		if (Character->IsLockpicking())
		{
			if (LockpickSince < 0.0)
			{
				LockpickSince = Now;
			}
			const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
			const float Dexterity = ASC != nullptr ? ASC->GetNumericAttribute(UCrossguardAttributeSet::GetDexterityAttribute()) : 0.0f;
			const double Seconds = Crossguard::LockpickSeconds(Crossguard::FEconomyConfig(), Dexterity);
			Channel = TEXT("Picking the lock");
			Progress = Seconds > 0.0 ? static_cast<float>(FMath::Clamp((Now - LockpickSince) / Seconds, 0.0, 1.0)) : 1.0f;
		}
		else
		{
			LockpickSince = -1.0;
		}

		// Extraction zone: the countdown is server-only; count from when we stepped in.
		const ACrossguardExtractionZone* Zone = nullptr;
		for (TActorIterator<ACrossguardExtractionZone> It(World); It; ++It)
		{
			if (Character->IsOverlappingActor(*It))
			{
				Zone = *It;
				break;
			}
		}
		if (Zone != nullptr && Channel.IsEmpty())
		{
			if (ExtractZoneSince < 0.0)
			{
				ExtractZoneSince = Now;
			}
			Channel = TEXT("Extracting");
			Progress = Zone->ExtractSeconds > 0.0f ? static_cast<float>(FMath::Clamp((Now - ExtractZoneSince) / Zone->ExtractSeconds, 0.0, 1.0)) : 1.0f;
		}
		else if (Zone == nullptr)
		{
			ExtractZoneSince = -1.0;
		}

		// A nearby extract portal: its state and countdown.
		if (Channel.IsEmpty())
		{
			for (TActorIterator<ACrossguardExtractPortal> It(World); It; ++It)
			{
				if (FVector::Dist(It->GetActorLocation(), Character->GetActorLocation()) > CrossguardHudWidgetLocal::PortalShowDistance)
				{
					continue;
				}
				if (It->GetState() == ECrossguardPortalState::Channeling)
				{
					Channel = FString::Printf(TEXT("Portal opens in %.0f s (stay within 10 m)"), It->GetSecondsLeft());
					Progress = It->ChannelSeconds > 0.0f ? FMath::Clamp(1.0f - It->GetSecondsLeft() / It->ChannelSeconds, 0.0f, 1.0f) : 1.0f;
				}
				else if (It->GetState() == ECrossguardPortalState::Open)
				{
					Channel = FString::Printf(TEXT("Portal open: %.0f s left"), It->GetSecondsLeft());
				}
				break;
			}
		}
	}
	SetText(ChannelText, Channel);
	if (ChannelBar != nullptr)
	{
		ChannelBar->SetVisibility(Progress >= 0.0f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		ChannelBar->SetPercent(FMath::Max(Progress, 0.0f));
	}
}

void UCrossguardHudWidget::RefreshDeathAndSpectating(ACrossguardCharacter* Character)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	FString Center;
	const UAbilitySystemComponent* ASC = Character != nullptr ? Character->GetAbilitySystemComponent() : nullptr;
	if (Character == nullptr)
	{
		DownedSince = -1.0;
		const APlayerController* Player = GetOwningPlayer();
		const APawn* Watched = Player != nullptr ? Cast<APawn>(Player->GetViewTarget()) : nullptr;
		Center = Watched != nullptr && Watched->GetPlayerState() != nullptr
			? FString::Printf(TEXT("Spectating %s"), *CrossguardUI::PlayerNameOf(Watched))
			: FString(TEXT("Dead for the raid"));
	}
	else if (Character->IsPermanentlyDead())
	{
		Center = TEXT("Dead for the raid");
	}
	else if (ASC != nullptr && ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead))
	{
		if (DownedSince < 0.0)
		{
			DownedSince = Now;
		}
		const double Left = FMath::Max(0.0, Crossguard::FDeathConfig().ReviveWindowSeconds - (Now - DownedSince));
		Center = FString::Printf(TEXT("Downed: a teammate can revive you for %.0f s"), Left);
	}
	else
	{
		DownedSince = -1.0;
	}
	SetText(CenterText, Center);
}
