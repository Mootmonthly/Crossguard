#include "Game/CrossguardLocalPlayer.h"

#include "Engine/GameInstance.h"
#include "Game/CrossguardNakamaSubsystem.h"

FString UCrossguardLocalPlayer::GetGameLoginOptions() const
{
	const FString Base = Super::GetGameLoginOptions();
	const UGameInstance* Instance = GetGameInstance();
	const UCrossguardNakamaSubsystem* Nakama = Instance != nullptr ? Instance->GetSubsystem<UCrossguardNakamaSubsystem>() : nullptr;
	const FString Token = Nakama != nullptr ? Nakama->GetLoginOption() : FString();
	if (Token.IsEmpty())
	{
		return Base;
	}
	// Options are joined with '?' in the login URL (the engine adds the first one).
	return Base.IsEmpty() ? Token : Base + TEXT("?") + Token;
}
