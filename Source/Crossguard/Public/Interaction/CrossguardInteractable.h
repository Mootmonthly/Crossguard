#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "CrossguardInteractable.generated.h"

class ACrossguardCharacter;

UINTERFACE(MinimalAPI)
class UCrossguardInteractable : public UInterface
{
	GENERATED_BODY()
};

// Something a character uses with the interact key (E): bodies, chests, doors. The character
// picks the nearest one it faces within GetInteractRange. If the actor has a locked
// UCrossguardLockComponent, the character picks the lock first.
class CROSSGUARD_API ICrossguardInteractable
{
	GENERATED_BODY()

public:
	// Server. User is alive, in range and facing it; the actor is not locked.
	virtual void Interact(ACrossguardCharacter* User) = 0;

	// How close the user must be, in cm.
	virtual float GetInteractRange() const { return 250.0f; }
};
