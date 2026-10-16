#include "Game/CrossguardDevMode.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/WeakObjectPtrTemplates.h"

// Named, not anonymous (unity builds).
namespace CrossguardDevModeLocal
{
	TWeakObjectPtr<const UWorld> OverrideWorld;
	bool bOverrideValue = false;
}

bool Crossguard::IsDevListenMode(const UWorld* World)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (World != nullptr && CrossguardDevModeLocal::OverrideWorld.Get() == World)
	{
		return CrossguardDevModeLocal::bOverrideValue;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("DevListenHub")))
	{
		return true;
	}
	return World != nullptr && World->WorldType == EWorldType::PIE;
#endif
}

void Crossguard::SetDevListenModeOverrideForTests(const UWorld* World, TOptional<bool> bDevListenMode)
{
	if (bDevListenMode.IsSet() && World != nullptr)
	{
		CrossguardDevModeLocal::OverrideWorld = World;
		CrossguardDevModeLocal::bOverrideValue = bDevListenMode.GetValue();
		return;
	}
	CrossguardDevModeLocal::OverrideWorld.Reset();
}
