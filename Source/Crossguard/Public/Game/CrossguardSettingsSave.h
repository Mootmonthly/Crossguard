#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "CrossguardSettingsSave.generated.h"

class APlayerController;

// The player's own settings (placeholder; the main menu's Settings, docs/hub-and-menu.md): mouse
// sensitivity, field of view and master volume, in a local save slot on this machine (not the
// account's character save). ACrossguardPlayerController applies them to its local player.
UCLASS()
class CROSSGUARD_API UCrossguardSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	// Multiplies mouse look (1 = the game's default speed).
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float MouseSensitivity = 1.0f;

	// Horizontal field of view of the first-person camera, in degrees.
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float FieldOfView = 90.0f;

	// 0 (muted) to 1.
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float MasterVolume = 1.0f;

	static constexpr float MinSensitivity = 0.1f;
	static constexpr float MaxSensitivity = 5.0f;
	static constexpr float MinFieldOfView = 60.0f;
	static constexpr float MaxFieldOfView = 120.0f;

	static const TCHAR* SlotName;

	// The saved settings, or the defaults if none are saved (never null).
	static UCrossguardSettingsSave* LoadOrDefault();
	// Clamps and writes the slot. False if writing failed.
	bool SaveSettings();
	// Keeps every value in range.
	void ClampValues();
	// Sets the process-wide master volume (the other two are per controller:
	// ACrossguardPlayerController::ApplySettings).
	void ApplyVolume() const;
};
