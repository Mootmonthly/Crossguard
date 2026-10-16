#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"

#include "CrossguardLocalPlayer.generated.h"

// The game's local player (Config/DefaultEngine.ini, LocalPlayerClassName). Adds the Nakama session
// token to every server login (?NakamaToken=<token>: menu -> hub, hub -> raid, raid -> hub), so the
// dedicated server can verify who connects (ACrossguardGameMode::PreLoginAsync, docs/anti-cheat.md
// "Player identity"). Nothing is added when the player is not logged in to Nakama.
UCLASS()
class CROSSGUARD_API UCrossguardLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	virtual FString GetGameLoginOptions() const override;
};
