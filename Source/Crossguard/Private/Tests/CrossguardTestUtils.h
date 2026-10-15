#pragma once

// Shared helpers for the Crossguard editor automation tests (docs/editor-tests.md).
// Run them with Tools/run_editor_tests.bat, or Session Frontend → Automation, filter "Crossguard".

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// Every Crossguard test runs in the editor (also headless: UnrealEditor-Cmd -nullrhi) and is listed
// under the Engine filter. In UE 5.5+ EAutomationTestFlags is an enum class with flag operators; in
// older versions these are plain enum values; the expression works with both.
#define CROSSGUARD_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CrossguardTests
{
	// An empty transient data table with RowType rows, kept alive (not garbage collected) while the
	// returned pointer lives.
	template <typename RowType>
	TStrongObjectPtr<UDataTable> MakeTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = RowType::StaticStruct();
		return TStrongObjectPtr<UDataTable>(Table);
	}

	// A transient game world for one test, destroyed with this object. No level, no game mode (so
	// actors spawned in it do not get BeginPlay, and GetAuthGameMode() is null); spawned pawns with
	// AutoPossessAI still get their AI controller (APawn::PostInitializeComponents checks
	// World->IsGameWorld()). Same set-up as the engine's GameplayAbilities effect tests
	// (GameplayEffectsTest.cpp): CreateWorld + a Game world context + InitializeActorsForPlay + BeginPlay.
	class FTestWorld
	{
	public:
		FTestWorld()
		{
			if (GEngine == nullptr)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World == nullptr)
			{
				return;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			const FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}

		~FTestWorld()
		{
			if (World != nullptr && GEngine != nullptr)
			{
				GEngine->DestroyWorldContext(World);
				// Also removes the world from the root set (CreateWorld added it), so it can be collected.
				World->DestroyWorld(false);
			}
			World = nullptr;
		}

		FTestWorld(const FTestWorld&) = delete;
		FTestWorld& operator=(const FTestWorld&) = delete;

		UWorld* Get() const { return World; }

	private:
		UWorld* World = nullptr;
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
