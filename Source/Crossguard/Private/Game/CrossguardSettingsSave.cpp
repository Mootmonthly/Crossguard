#include "Game/CrossguardSettingsSave.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* UCrossguardSettingsSave::SlotName = TEXT("CrossguardSettings");

UCrossguardSettingsSave* UCrossguardSettingsSave::LoadOrDefault()
{
	UCrossguardSettingsSave* Settings = nullptr;
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		Settings = Cast<UCrossguardSettingsSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	}
	if (Settings == nullptr)
	{
		Settings = Cast<UCrossguardSettingsSave>(UGameplayStatics::CreateSaveGameObject(UCrossguardSettingsSave::StaticClass()));
	}
	if (Settings != nullptr)
	{
		Settings->ClampValues();
	}
	return Settings;
}

bool UCrossguardSettingsSave::SaveSettings()
{
	ClampValues();
	return UGameplayStatics::SaveGameToSlot(this, SlotName, 0);
}

void UCrossguardSettingsSave::ClampValues()
{
	MouseSensitivity = FMath::Clamp(MouseSensitivity, MinSensitivity, MaxSensitivity);
	FieldOfView = FMath::Clamp(FieldOfView, MinFieldOfView, MaxFieldOfView);
	MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
}

void UCrossguardSettingsSave::ApplyVolume() const
{
	if (GEngine == nullptr)
	{
		return;
	}
	// No audio device under -nosound / -nullrhi tests: nothing to set.
	const FAudioDeviceHandle Device = GEngine->GetMainAudioDevice();
	if (FAudioDevice* Audio = Device.GetAudioDevice())
	{
		Audio->SetTransientPrimaryVolume(MasterVolume);
	}
}
