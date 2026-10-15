#include "Items/CrossguardLootContainer.h"

#include "Character/CrossguardCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Interaction/CrossguardLockComponent.h"
#include "Net/UnrealNetwork.h"

ACrossguardLootContainer::ACrossguardLootContainer()
{
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	BodyMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));
	BodyMeshComponent->SetupAttachment(GetRootComponent());
	BodyMeshComponent->SetVisibility(false);
	BodyMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Lock = CreateDefaultSubobject<UCrossguardLockComponent>(TEXT("Lock"));
}

void ACrossguardLootContainer::Interact(ACrossguardCharacter* User)
{
	if (User != nullptr && User->GetInventoryComponent() != nullptr)
	{
		User->GetInventoryComponent()->OpenLootContainer(this);
	}
}

void ACrossguardLootContainer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Items: not replicated. Whoever has the container open gets them through
	// UCrossguardInventoryComponent::GetOpenContainerItems (anti-cheat: nobody else sees them).
	DOREPLIFETIME(ACrossguardLootContainer, GridSize);
	DOREPLIFETIME(ACrossguardLootContainer, BodyMesh);
	DOREPLIFETIME(ACrossguardLootContainer, BodyMeshTransform);
	DOREPLIFETIME(ACrossguardLootContainer, bShowBodyMesh);
}

Crossguard::FInventoryGrid ACrossguardLootContainer::BuildGrid(FIntPoint Size) const
{
	Crossguard::FInventoryGrid Grid(Crossguard::FGridSize{Size.X, Size.Y});
	for (const FCrossguardInventoryItem& Item : Items)
	{
		Grid.Place(Item.Id, {Item.Size.X, Item.Size.Y}, {Item.Position.X, Item.Position.Y, Item.bRotated});
	}
	return Grid;
}

bool ACrossguardLootContainer::FindPlacement(FCrossguardInventoryItem& Item, FIntPoint Size) const
{
	Crossguard::FGridPlacement Spot;
	if (Item.Size.X <= 0 || Item.Size.Y <= 0 || !BuildGrid(Size).FindFreeSpot({Item.Size.X, Item.Size.Y}, true, Spot))
	{
		return false;
	}
	Item.Container = ECrossguardInventoryContainer::Main;
	Item.Position = FIntPoint(Spot.X, Spot.Y);
	Item.bRotated = Spot.bRotated;
	return true;
}

void ACrossguardLootContainer::InitializeContents(const TArray<FCrossguardInventoryItem>& NewItems)
{
	if (!HasAuthority())
	{
		return;
	}

	Items.Reset();
	GridSize = FIntPoint(FMath::Max(1, Width), FMath::Max(1, MinHeight));
	for (FCrossguardInventoryItem Item : NewItems)
	{
		if (Item.Size.X <= 0 || Item.Size.Y <= 0 || FMath::Min(Item.Size.X, Item.Size.Y) > GridSize.X)
		{
			continue;
		}
		// Grow one row at a time until it fits; an item at most Width wide (rotated if needed)
		// always fits once the grid is tall enough.
		while (!FindPlacement(Item, GridSize))
		{
			++GridSize.Y;
		}
		Item.Id = NextItemId++;
		Items.Add(Item);
	}
	OnContentsChanged.Broadcast();
}

bool ACrossguardLootContainer::FindItem(int32 ItemId, FCrossguardInventoryItem& OutItem) const
{
	const FCrossguardInventoryItem* Found = Items.FindByPredicate([ItemId](const FCrossguardInventoryItem& Item) { return Item.Id == ItemId; });
	if (ItemId == 0 || Found == nullptr)
	{
		return false;
	}
	OutItem = *Found;
	return true;
}

bool ACrossguardLootContainer::SetItemCount(int32 ItemId, int32 Count)
{
	const int32 Index = Items.IndexOfByPredicate([ItemId](const FCrossguardInventoryItem& Item) { return Item.Id == ItemId; });
	if (!HasAuthority() || ItemId == 0 || Index == INDEX_NONE)
	{
		return false;
	}
	if (Count <= 0)
	{
		Items.RemoveAt(Index);
	}
	else
	{
		Items[Index].Count = Count;
	}
	OnContentsChanged.Broadcast();
	return true;
}

bool ACrossguardLootContainer::AddItem(const FCrossguardInventoryItem& NewItem)
{
	FCrossguardInventoryItem Item = NewItem;
	if (!HasAuthority() || !FindPlacement(Item, GridSize))
	{
		return false;
	}
	Item.Id = NextItemId++;
	Items.Add(Item);
	OnContentsChanged.Broadcast();
	return true;
}

void ACrossguardLootContainer::OnRep_Contents()
{
	OnContentsChanged.Broadcast();
}

TArray<FCrossguardInventoryItem> ACrossguardLootContainer::TakeAllItems()
{
	TArray<FCrossguardInventoryItem> Taken;
	if (HasAuthority())
	{
		Taken = MoveTemp(Items);
		Items.Reset();
		OnContentsChanged.Broadcast();
	}
	return Taken;
}

void ACrossguardLootContainer::SetBodyMesh(USkeletalMesh* Mesh, const FTransform& RelativeTransform)
{
	if (HasAuthority())
	{
		BodyMesh = Mesh;
		BodyMeshTransform = RelativeTransform;
		ApplyBodyMesh();
	}
}

void ACrossguardLootContainer::ShowBodyMesh()
{
	if (HasAuthority() && !bShowBodyMesh)
	{
		bShowBodyMesh = true;
		ApplyBodyMesh();
	}
}

void ACrossguardLootContainer::OnRep_BodyMesh()
{
	ApplyBodyMesh();
}

void ACrossguardLootContainer::ApplyBodyMesh()
{
	if (BodyMeshComponent->GetSkeletalMeshAsset() != BodyMesh)
	{
		BodyMeshComponent->SetSkeletalMeshAsset(BodyMesh);
	}
	if (!bShowBodyMesh || BodyMesh == nullptr || BodyMeshComponent->IsVisible())
	{
		return;
	}
	// Collapses from where the character stood. Not synced between machines; it is only a marker.
	BodyMeshComponent->SetRelativeTransform(BodyMeshTransform);
	BodyMeshComponent->SetVisibility(true);
	BodyMeshComponent->SetCollisionProfileName(TEXT("Ragdoll"));
	BodyMeshComponent->SetSimulatePhysics(true);
}
