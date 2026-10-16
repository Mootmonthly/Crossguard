#include "UI/CrossguardMainMenuWidget.h"

#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/CrossguardDevMode.h"
#include "Game/CrossguardMenuGameMode.h"
#include "Game/CrossguardNakamaSubsystem.h"
#include "Game/CrossguardPlayerController.h"
#include "Game/CrossguardSettingsSave.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "UI/CrossguardUIButton.h"

namespace CrossguardMainMenuLocal
{
	const FLinearColor Title(1.0f, 0.85f, 0.5f);
	const FLinearColor Info(0.75f, 0.75f, 0.8f);
	const FLinearColor Refusal(1.0f, 0.55f, 0.3f);
	const FLinearColor Success(0.5f, 1.0f, 0.5f);

	// Settings steps (StepSetting).
	constexpr float SensitivityStep = 0.1f;
	constexpr float FieldOfViewStep = 5.0f;
	constexpr float VolumeStep = 0.1f;

	// The hub map Play opens when there is no menu game mode (a test world).
	const TCHAR* DefaultHubMap = TEXT("/Game/Maps/L_Hub");
}

UCrossguardMainMenuWidget::UCrossguardMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Redrawn when something changes, not on a timer.
	RefreshInterval = 0.0f;
}

void UCrossguardMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Refresh();
}

UCrossguardSettingsSave* UCrossguardMainMenuWidget::EnsureSettings()
{
	if (Settings == nullptr)
	{
		Settings = UCrossguardSettingsSave::LoadOrDefault();
	}
	return Settings;
}

void UCrossguardMainMenuWidget::BuildTree()
{
	using namespace CrossguardMainMenuLocal;
	EnsureSettings();

	UCanvasPanel* Root = Make<UCanvasPanel>();
	WidgetTree->RootWidget = Root;

	UBorder* Panel = MakePanel(FLinearColor(0.03f, 0.03f, 0.04f, 0.92f), 16.0f);
	UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetPosition(FVector2D::ZeroVector);
	PanelSlot->SetAutoSize(true);

	UVerticalBox* Column = MakeColumn();
	Panel->SetContent(Column);
	AddTo(Column, MakeText(TEXT("CROSSGUARD"), 32, Title));
	AddTo(Column, MakeText(TEXT("Placeholder menu"), 10, Info), 8.0f);

	// Account: not a main button (those are the play flow); the login comes first.
	UHorizontalBox* AccountRow = MakeRow();
	AddTo(Column, AccountRow, 6.0f);
	AccountText = MakeText(GetAccountLine(), 12, Info);
	USizeBox* AccountBox = Make<USizeBox>();
	AccountBox->SetWidthOverride(240.0f);
	AccountBox->SetContent(AccountText);
	AddTo(AccountRow, AccountBox);
	AddTo(AccountRow, MakeButton(TEXT("Log in"), [this]() { LogIn(); }, 12));

	const auto AddMainButton = [this, Column](const FString& Label, TFunction<void()> OnClicked)
	{
		UCrossguardUIButton* Button = MakeButton(Label, MoveTemp(OnClicked), 16);
		USizeBox* ButtonBox = Make<USizeBox>();
		ButtonBox->SetWidthOverride(320.0f);
		ButtonBox->SetContent(Button);
		AddTo(Column, ButtonBox, 6.0f);
		MainButtons.Add(Button);
	};

	AddMainButton(TEXT("Play"), [this]() { Play(); });

	// Join: the host's address, then the button.
	UHorizontalBox* JoinRow = MakeRow();
	AddTo(Column, JoinRow, 6.0f);
	AddTo(JoinRow, MakeText(TEXT("Address:"), 12));
	AddressField = MakeTextField(TEXT("192.168.1.20 or host:7777"));
	USizeBox* FieldSize = Make<USizeBox>();
	FieldSize->SetWidthOverride(240.0f);
	FieldSize->SetContent(AddressField);
	AddTo(JoinRow, FieldSize);
	AddMainButton(TEXT("Join"), [this]() { Join(); });

	AddMainButton(TEXT("Settings"), [this]() { ToggleSettings(); });

	// Settings, hidden until opened.
	SettingsPanel = MakeColumn();
	AddTo(Column, SettingsPanel, 6.0f);
	SettingsText = MakeText(FString(), 12, Info);
	AddTo(SettingsPanel, SettingsText);
	const TCHAR* SettingNames[] = { TEXT("Mouse sensitivity"), TEXT("Field of view"), TEXT("Master volume") };
	for (int32 Which = 0; Which < 3; ++Which)
	{
		UHorizontalBox* Row = MakeRow();
		AddTo(SettingsPanel, Row);
		UTextBlock* SettingName = MakeText(SettingNames[Which], 12);
		USizeBox* NameBox = Make<USizeBox>();
		NameBox->SetWidthOverride(170.0f);
		NameBox->SetContent(SettingName);
		AddTo(Row, NameBox);
		AddTo(Row, MakeButton(TEXT(" - "), [this, Which]() { StepSetting(Which, -1); }, 12));
		AddTo(Row, MakeButton(TEXT(" + "), [this, Which]() { StepSetting(Which, 1); }, 12));
	}
	UHorizontalBox* SaveRow = MakeRow();
	AddTo(SettingsPanel, SaveRow, 4.0f);
	AddTo(SaveRow, MakeButton(TEXT("Save settings"), [this]() { SaveSettings(); }, 12));
	AddTo(SaveRow, MakeButton(TEXT("Close"), [this]() { ToggleSettings(); }, 12));
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);

	AddMainButton(TEXT("Quit"), [this]() { Quit(); });

	StatusText = MakeText(FString(), 12, Refusal);
	StatusText->SetAutoWrapText(true);
	AddTo(Column, StatusText, 8.0f);

	// Why the player is back here (e.g. dead for the raid).
	const UWorld* World = GetWorld();
	const ACrossguardMenuGameMode* MenuMode = World != nullptr ? World->GetAuthGameMode<ACrossguardMenuGameMode>() : nullptr;
	if (MenuMode != nullptr && !MenuMode->GetReturnMessage().IsEmpty())
	{
		ShowStatus(MenuMode->GetReturnMessage(), Info);
	}
}

void UCrossguardMainMenuWidget::NativeRefresh()
{
	SetText(SettingsText, GetSettingsLine());
	SetText(AccountText, GetAccountLine());
	if (SettingsPanel != nullptr)
	{
		SettingsPanel->SetVisibility(bSettingsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

FString UCrossguardMainMenuWidget::GetPlayUrl() const
{
	const UWorld* World = GetWorld();
	const ACrossguardMenuGameMode* MenuMode = World != nullptr ? World->GetAuthGameMode<ACrossguardMenuGameMode>() : nullptr;
	return ACrossguardMenuGameMode::BuildHostUrl(MenuMode != nullptr ? MenuMode->GetHubMapName() : FString(CrossguardMainMenuLocal::DefaultHubMap));
}

FString UCrossguardMainMenuWidget::GetJoinUrl() const
{
	return AddressField != nullptr ? ACrossguardMenuGameMode::BuildJoinUrl(AddressField->GetText().ToString()) : FString();
}

void UCrossguardMainMenuWidget::SetJoinAddress(const FString& Address)
{
	EnsureTree();
	if (AddressField != nullptr)
	{
		AddressField->SetText(FText::FromString(Address));
	}
}

FString UCrossguardMainMenuWidget::GetSettingsLine() const
{
	if (Settings == nullptr)
	{
		return FString();
	}
	return FString::Printf(TEXT("Mouse sensitivity %.2f | Field of view %.0f | Master volume %d%%"), Settings->MouseSensitivity,
		Settings->FieldOfView, FMath::RoundToInt(Settings->MasterVolume * 100.0f));
}

TArray<FString> UCrossguardMainMenuWidget::GetMainButtonLabels() const
{
	TArray<FString> Labels;
	for (const UCrossguardUIButton* Button : MainButtons)
	{
		if (Button != nullptr)
		{
			Labels.Add(Button->GetLabel());
		}
	}
	return Labels;
}

UCrossguardUIButton* UCrossguardMainMenuWidget::FindMainButton(const FString& Label) const
{
	for (UCrossguardUIButton* Button : MainButtons)
	{
		if (Button != nullptr && Button->GetLabel() == Label)
		{
			return Button;
		}
	}
	return nullptr;
}

void UCrossguardMainMenuWidget::ShowStatus(const FString& Message, const FLinearColor& Color)
{
	StatusLine = Message;
	if (StatusText != nullptr)
	{
		StatusText->SetText(FText::FromString(Message));
		StatusText->SetColorAndOpacity(FSlateColor(Color));
	}
}

void UCrossguardMainMenuWidget::TravelTo(const FString& Url)
{
	UWorld* World = GetWorld();
	if (GEngine == nullptr || World == nullptr)
	{
		return;
	}
	// As the "open" console command: this machine leaves the menu map for Url.
	GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
}

void UCrossguardMainMenuWidget::Play()
{
	using namespace CrossguardMainMenuLocal;
	if (!Crossguard::IsDevListenMode(GetWorld()))
	{
		// The real game never hosts on a player's PC: Play will connect to a hub server once the
		// backend exists.
		ShowStatus(TEXT("Online play is not available yet (no hub server)."), Refusal);
		return;
	}
	// DEV-ONLY (listen server): this PC hosts the hub.
	const FString Url = GetPlayUrl();
	FString Map;
	Url.Split(TEXT("?"), &Map, nullptr);
	if (Url.IsEmpty() || !FPackageName::DoesPackageExist(Map))
	{
		ShowStatus(FString::Printf(TEXT("No hub map %s: run Tools\\editor_setup.bat -CreateHub"), *Map), Refusal);
		return;
	}
	ShowStatus(TEXT("Opening the hub..."), Success);
	TravelTo(Url);
}

void UCrossguardMainMenuWidget::Join()
{
	using namespace CrossguardMainMenuLocal;
	const FString Url = GetJoinUrl();
	if (Url.IsEmpty())
	{
		ShowStatus(TEXT("Type the host's address (e.g. 192.168.1.20 or host:7777)."), Refusal);
		return;
	}
	ShowStatus(FString::Printf(TEXT("Connecting to %s..."), *Url), Success);
	// Development builds: the server keeps this machine's player id (-CrossguardPlayerId=).
	TravelTo(ACrossguardMenuGameMode::WithLocalDevPlayerId(Url));
}

void UCrossguardMainMenuWidget::ToggleSettings()
{
	bSettingsOpen = !bSettingsOpen;
	Refresh();
}

void UCrossguardMainMenuWidget::StepSetting(int32 Which, int32 Direction)
{
	using namespace CrossguardMainMenuLocal;
	UCrossguardSettingsSave* Edited = EnsureSettings();
	if (Edited == nullptr)
	{
		return;
	}
	const float Sign = Direction < 0 ? -1.0f : 1.0f;
	switch (Which)
	{
	case 0:
		Edited->MouseSensitivity += Sign * SensitivityStep;
		break;
	case 1:
		Edited->FieldOfView += Sign * FieldOfViewStep;
		break;
	case 2:
		Edited->MasterVolume += Sign * VolumeStep;
		break;
	default:
		return;
	}
	Edited->ClampValues();
	Refresh();
}

void UCrossguardMainMenuWidget::SaveSettings()
{
	using namespace CrossguardMainMenuLocal;
	UCrossguardSettingsSave* Edited = EnsureSettings();
	if (Edited == nullptr)
	{
		return;
	}
	const bool bSaved = Edited->SaveSettings();
	if (ACrossguardPlayerController* Player = Cast<ACrossguardPlayerController>(GetOwningPlayer()))
	{
		Player->ApplySettings(*Edited);
	}
	else
	{
		Edited->ApplyVolume();
	}
	ShowStatus(bSaved ? FString(TEXT("Settings saved.")) : FString(TEXT("Could not write the settings.")), bSaved ? Success : Refusal);
	Refresh();
}

void UCrossguardMainMenuWidget::Quit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

FString UCrossguardMainMenuWidget::GetAccountLine() const
{
	const UCrossguardNakamaSubsystem* Nakama = UCrossguardNakamaSubsystem::Get(this);
	return Nakama != nullptr ? Nakama->DescribeLogin() : FString(TEXT("Not logged in"));
}

void UCrossguardMainMenuWidget::LogIn()
{
	using namespace CrossguardMainMenuLocal;
	UCrossguardNakamaSubsystem* Nakama = UCrossguardNakamaSubsystem::Get(this);
	if (Nakama == nullptr)
	{
		return;
	}
	// TODO(Steam): with the Steam subsystem running, log in with a Steam ticket instead
	// (UCrossguardNakamaSubsystem::LoginWithDevice comment).
	ShowStatus(TEXT("Logging in..."), Info);
	TWeakObjectPtr<UCrossguardMainMenuWidget> WeakThis(this);
	Nakama->LoginWithDevice([WeakThis](bool bLoggedIn, const FString& Error)
	{
		if (UCrossguardMainMenuWidget* Self = WeakThis.Get())
		{
			Self->ShowStatus(bLoggedIn ? FString(TEXT("Logged in.")) : FString::Printf(TEXT("Could not log in: %s"), *Error), bLoggedIn ? Success : Refusal);
			Self->Refresh();
		}
	});
	Refresh();
}
