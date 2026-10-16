#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"

#include "CrossguardMenuGameMode.generated.h"

class UCrossguardMainMenuWidget;

// The main menu map's game mode (L_MainMenu, Tools/editor_setup.py -CreateMenu; docs/hub-and-menu.md):
// no pawn, its HUD shows UCrossguardMainMenuWidget with the mouse cursor on. Play hosts the hub
// (DEV-ONLY (listen server): HubMapName as a listen server with ?Hub; refused outside development
// mode until the backend sends players to a hub server), Join connects to an address, Settings, Quit. A player
// whose raid is over comes back here (ACrossguardGameMode::ReturnToMenu) with a reason key in the URL.
UCLASS()
class CROSSGUARD_API ACrossguardMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACrossguardMenuGameMode();

	// Reads the ?MenuReason= option (why the player is back here).
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	// No pawn: the player only sees the menu.
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	// The hub map Play opens. The -HubMap=<path> command line option overrides it.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Menu")
	FString HubMapName = TEXT("/Game/Maps/L_Hub");

	FString GetHubMapName() const;

	// The message for the reason this menu was opened with ("Your raid is over."); empty if none.
	const FString& GetReturnMessage() const { return ReturnMessage; }

	// URL option carrying a reason key (letters and digits) to the menu.
	static const TCHAR* ReasonOption;
	// Reason key: the player's raid is over (dead for the raid, no teammate left).
	static const TCHAR* RaidOverReason;
	// Reason key: the player's team is done but the match server knows no hub server to send them to.
	static const TCHAR* NoHubReason;

	// --- Pure helpers (tested) ---

	// Play: "<HubMap>?listen?Hub" (a listen server playing the map as the hub). Empty if HubMap is empty.
	static FString BuildHostUrl(const FString& HubMap);
	// Join: the address as typed, trimmed ("192.168.1.20", "host.example:7777"). Empty if it is empty
	// or holds spaces or URL separators ('?', '#', '/').
	static FString BuildJoinUrl(const FString& Address);
	// Development builds: Url plus "?DevPlayerId=<name>" from this machine's -CrossguardPlayerId=<name>
	// (letters, digits, '-', '_'; 32 at most), so a dedicated server (no local player) keeps the
	// player id and save. Url unchanged in Shipping, without the option, or when Url is empty.
	static FString WithLocalDevPlayerId(const FString& Url);
	// Back to the menu: "<MenuMap>?MenuReason=<Key>" (no option when Key is empty). Characters other
	// than letters and digits are dropped from the key. Empty if MenuMap is empty.
	static FString BuildMenuUrl(const FString& MenuMap, const FString& ReasonKey);
	// The message shown for a reason key; empty for an unknown one.
	static FString ReasonMessage(const FString& ReasonKey);

private:
	FString ReturnMessage;
};

// The menu map's HUD: puts the main menu on the local player's screen, UI-only input, cursor on.
UCLASS()
class CROSSGUARD_API ACrossguardMenuHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACrossguardMenuHUD();

	virtual void BeginPlay() override;

	// Replace with a Widget Blueprint subclass of CrossguardMainMenuWidget (docs/ui.md).
	UPROPERTY(EditDefaultsOnly, Category = "Crossguard|UI")
	TSubclassOf<UCrossguardMainMenuWidget> MainMenuClass;

	UFUNCTION(BlueprintPure, Category = "Crossguard|UI")
	UCrossguardMainMenuWidget* GetMainMenu() const { return MainMenu; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UCrossguardMainMenuWidget> MainMenu;
};
