#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"

#include "CrossguardHubMarker.generated.h"

// Placed in a map to make it a hub (docs/hub-and-menu.md): ACrossguardGameMode::InitGame turns
// bIsRaid off on a map that holds one, as the "?Hub" URL option does. A map cannot carry URL
// options, so this is how L_Hub (Tools/editor_setup.py -CreateHub) is a hub when opened directly or
// by a dedicated server. Invisible; it does nothing else. Keep it in the persistent level (not a
// streamed sub-level or a World Partition cell): the game mode looks for it before play begins.
UCLASS(NotBlueprintable)
class CROSSGUARD_API ACrossguardHubMarker : public AInfo
{
	GENERATED_BODY()

public:
	ACrossguardHubMarker();

	// True if World holds a hub marker.
	static bool IsHubWorld(const UWorld* World);
};
