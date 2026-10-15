#include "Game/CrossguardExtractionZone.h"

#include "Character/CrossguardCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "TimerManager.h"

ACrossguardExtractionZone::ACrossguardExtractionZone()
{
	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	Area->SetBoxExtent(FVector(200.0f, 200.0f, 150.0f));
	Area->SetCollisionProfileName(TEXT("Trigger"));
	SetRootComponent(Area);

	Area->OnComponentBeginOverlap.AddDynamic(this, &ACrossguardExtractionZone::OnAreaBeginOverlap);
	Area->OnComponentEndOverlap.AddDynamic(this, &ACrossguardExtractionZone::OnAreaEndOverlap);
}

void ACrossguardExtractionZone::OnAreaBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(OtherActor);
	const ACrossguardPlayerState* PlayerState = Character != nullptr ? Character->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	if (!HasAuthority() || PlayerState == nullptr || !PlayerState->IsInRaid() || Countdowns.Contains(Character))
	{
		return;
	}

	FTimerHandle& Handle = Countdowns.Add(Character);
	const TWeakObjectPtr<ACrossguardCharacter> Weak(Character);
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &ACrossguardExtractionZone::Extract, Weak), FMath::Max(ExtractSeconds, 0.01f), false);
}

void ACrossguardExtractionZone::OnAreaEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(OtherActor);
	FTimerHandle Handle;
	if (HasAuthority() && Character != nullptr && Countdowns.RemoveAndCopyValue(Character, Handle))
	{
		GetWorldTimerManager().ClearTimer(Handle);
	}
}

void ACrossguardExtractionZone::Extract(TWeakObjectPtr<ACrossguardCharacter> Character)
{
	Countdowns.Remove(Character);
	ACrossguardCharacter* Extracting = Character.Get();
	ACrossguardGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ACrossguardGameMode>() : nullptr;
	if (Extracting != nullptr && GameMode != nullptr)
	{
		GameMode->HandleExtraction(Extracting);
	}
}
