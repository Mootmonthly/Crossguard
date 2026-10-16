#include "Interaction/CrossguardRaidPortal.h"

#include "Character/CrossguardCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Crossguard.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/CrossguardPlayerController.h"
#include "Game/CrossguardPlayerState.h"
#include "UObject/ConstructorHelpers.h"

namespace CrossguardRaidPortalLocal
{
	const FLinearColor Refusal(1.0f, 0.55f, 0.3f);
}

ACrossguardRaidPortal::ACrossguardRaidPortal()
{
	// Replicated so clients see it and the HUD can show its prompt.
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	PortalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalMesh"));
	PortalMesh->SetupAttachment(GetRootComponent());
	// Placeholder: a 3 m disc, 10 cm thick, standing on the ground (walk onto it and press F).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		PortalMesh->SetStaticMesh(Cylinder.Object);
		PortalMesh->SetRelativeScale3D(FVector(3.0f, 3.0f, 0.1f));
		PortalMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 5.0f));
	}
}

void ACrossguardRaidPortal::Interact(ACrossguardCharacter* User)
{
	const ECrossguardPartyResult Result = Launch(User);
	if (Result == ECrossguardPartyResult::None)
	{
		return;
	}
	ACrossguardPlayerState* PlayerState = User != nullptr ? User->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (UCrossguardPartyComponent* Party = PlayerState != nullptr ? PlayerState->GetParty() : nullptr)
	{
		// Logs it and tells the party window (OnPartyRefused), like the window's own Start raid.
		Party->Refuse(Result);
	}
	if (ACrossguardPlayerController* Controller = User != nullptr ? Cast<ACrossguardPlayerController>(User->GetController()) : nullptr)
	{
		Controller->ClientShowMessage(RefusalMessage(Result), CrossguardRaidPortalLocal::Refusal);
	}
}

ECrossguardPartyResult ACrossguardRaidPortal::Launch(ACrossguardCharacter* User)
{
	LastMode = ECrossguardRaidTravel::NoDestination;
	LastUrl.Reset();
	ACrossguardPlayerState* PlayerState = User != nullptr ? User->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	UCrossguardPartySubsystem* Parties = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
	if (!HasAuthority() || PlayerState == nullptr || Parties == nullptr)
	{
		LastResult = ECrossguardPartyResult::NoPlayer;
		return LastResult;
	}
	if (bPlanOnly)
	{
		LastResult = Parties->PlanRaidTravel(PlayerState, LastMode, LastUrl);
		return LastResult;
	}
	// The same checks as the party window's Start raid: leader or solo, hub only.
	LastResult = Parties->StartRaid(PlayerState);
	UE_LOG(LogCrossguard, Log, TEXT("%s used the raid portal: %s"), *GetNameSafe(User),
		*StaticEnum<ECrossguardPartyResult>()->GetNameStringByValue(static_cast<int64>(LastResult)));
	return LastResult;
}

FString ACrossguardRaidPortal::RefusalMessage(ECrossguardPartyResult Result)
{
	switch (Result)
	{
	case ECrossguardPartyResult::NotLeader:
		return TEXT("Only your party's leader can start the raid.");
	case ECrossguardPartyResult::OnlyInHub:
		return TEXT("The raid portal only works in the hub.");
	case ECrossguardPartyResult::NoRaidDestination:
		return TEXT("No raid server available right now.");
	case ECrossguardPartyResult::HostInParty:
		return TEXT("The host cannot leave the hub while other players stay on it.");
	case ECrossguardPartyResult::AlreadySearching:
		return TEXT("Your party is already looking for a raid.");
	case ECrossguardPartyResult::SearchCancelled:
		return TEXT("Raid search called off: your party changed.");
	case ECrossguardPartyResult::BackendUnavailable:
		return TEXT("The online service did not answer. Try again.");
	default:
		return FString::Printf(TEXT("Cannot start the raid (%s)."),
			*StaticEnum<ECrossguardPartyResult>()->GetNameStringByValue(static_cast<int64>(Result)));
	}
}
