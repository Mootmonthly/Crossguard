#pragma once

#include "CoreMinimal.h"

class UWorld;

// The single-PC development mode (docs/hub-and-menu.md, "Development mode").
//
// Owner's architecture: raids run on central dedicated match servers that many teams join, the hub on
// dedicated hub servers, and a backend service handles accounts, saves, parties and matchmaking. A
// player's PC never hosts a raid or a hub in the real game. The listen-server flow (the menu's Play
// hosting the hub, the whole server travelling hub -> raid -> hub) exists only for development on one
// PC. Every such code path is tagged "// DEV-ONLY (listen server)" and gated behind IsDevListenMode.
namespace Crossguard
{
	// DEV-ONLY (listen server): true when World may use the development listen-server flow: never in a
	// Shipping build; otherwise with -DevListenHub on the command line, or in a Play-In-Editor world
	// (also the automation tests). A test override for World wins (SetDevListenModeOverrideForTests).
	CROSSGUARD_API bool IsDevListenMode(const UWorld* World);

	// Tests: forces IsDevListenMode's answer for World only (it ends with that world, so a failed test
	// leaks nothing into the next PIE session). Unset (no value) clears it.
	CROSSGUARD_API void SetDevListenModeOverrideForTests(const UWorld* World, TOptional<bool> bDevListenMode);
}
