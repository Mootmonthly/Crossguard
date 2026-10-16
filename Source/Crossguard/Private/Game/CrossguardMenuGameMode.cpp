#include "Game/CrossguardMenuGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/CrossguardMainMenuWidget.h"

const TCHAR* ACrossguardMenuGameMode::ReasonOption = TEXT("MenuReason");
const TCHAR* ACrossguardMenuGameMode::RaidOverReason = TEXT("RaidOver");
const TCHAR* ACrossguardMenuGameMode::NoHubReason = TEXT("NoHub");

// --- ACrossguardMenuGameMode ----------------------------------------------------------------------

ACrossguardMenuGameMode::ACrossguardMenuGameMode()
{
	DefaultPawnClass = nullptr;
	// Applies the saved settings (volume) like in the game.
	PlayerControllerClass = ACrossguardPlayerController::StaticClass();
	HUDClass = ACrossguardMenuHUD::StaticClass();
}

void ACrossguardMenuGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	ReturnMessage = ReasonMessage(UGameplayStatics::ParseOption(Options, ReasonOption));
}

void ACrossguardMenuGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// No Super: it would spawn a pawn (RestartPlayer). The HUD shows the menu.
}

FString ACrossguardMenuGameMode::GetHubMapName() const
{
	FString FromCommandLine;
	return FParse::Value(FCommandLine::Get(), TEXT("HubMap="), FromCommandLine) ? FromCommandLine : HubMapName;
}

FString ACrossguardMenuGameMode::BuildHostUrl(const FString& HubMap)
{
	const FString Map = HubMap.TrimStartAndEnd();
	if (Map.IsEmpty())
	{
		return FString();
	}
	return FString::Printf(TEXT("%s?listen?%s"), *Map, ACrossguardGameMode::HubOption);
}

FString ACrossguardMenuGameMode::BuildJoinUrl(const FString& Address)
{
	const FString Trimmed = Address.TrimStartAndEnd();
	for (const TCHAR Char : Trimmed)
	{
		if (FChar::IsWhitespace(Char) || Char == TEXT('?') || Char == TEXT('#') || Char == TEXT('/') || Char == TEXT('\\'))
		{
			return FString();
		}
	}
	return Trimmed;
}

FString ACrossguardMenuGameMode::BuildMenuUrl(const FString& MenuMap, const FString& ReasonKey)
{
	const FString Map = MenuMap.TrimStartAndEnd();
	if (Map.IsEmpty())
	{
		return FString();
	}
	FString Key;
	for (const TCHAR Char : ReasonKey)
	{
		if (FChar::IsAlnum(Char))
		{
			Key.AppendChar(Char);
		}
	}
	return Key.IsEmpty() ? Map : FString::Printf(TEXT("%s?%s=%s"), *Map, ReasonOption, *Key);
}

FString ACrossguardMenuGameMode::ReasonMessage(const FString& ReasonKey)
{
	if (ReasonKey == RaidOverReason)
	{
		return TEXT("Your raid is over.");
	}
	if (ReasonKey == NoHubReason)
	{
		return TEXT("Your raid is over. No hub server is available right now.");
	}
	return FString();
}

// --- ACrossguardMenuHUD ---------------------------------------------------------------------------

ACrossguardMenuHUD::ACrossguardMenuHUD()
{
	MainMenuClass = UCrossguardMainMenuWidget::StaticClass();
}

void ACrossguardMenuHUD::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	if (PlayerOwner == nullptr || !PlayerOwner->IsLocalController() || World == nullptr || World->GetGameViewport() == nullptr)
	{
		return;
	}
	UClass* Chosen = MainMenuClass != nullptr ? MainMenuClass.Get() : UCrossguardMainMenuWidget::StaticClass();
	MainMenu = CreateWidget<UCrossguardMainMenuWidget>(PlayerOwner, Chosen);
	if (MainMenu == nullptr)
	{
		return;
	}
	MainMenu->EnsureTree();
	MainMenu->AddToViewport();
	MainMenu->Refresh();

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(MainMenu->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerOwner->SetInputMode(Mode);
	PlayerOwner->SetShowMouseCursor(true);
}
