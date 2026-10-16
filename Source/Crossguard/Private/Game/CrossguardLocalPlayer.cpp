#include "Game/CrossguardLocalPlayer.h"

#include "Engine/GameInstance.h"
#include "Game/CrossguardNakamaSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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

FString UCrossguardLocalPlayer::GetNickname() const
{
#if !UE_BUILD_SHIPPING
	FString Name;
	if (FParse::Value(FCommandLine::Get(), TEXT("CrossguardPlayerId="), Name) && !Name.IsEmpty())
	{
		return Name;
	}
#endif
	return Super::GetNickname();
}
