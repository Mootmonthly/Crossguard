#include "Character/CrossguardMonster.h"

#include "Character/CrossguardMonsterController.h"

ACrossguardMonster::ACrossguardMonster()
{
	AIControllerClass = ACrossguardMonsterController::StaticClass();
	KillXpSource = ECrossguardXpSource::BasicMonster;
	LootTableId = TEXT("basic_monster");
	// Placeholder numbers.
	FixedHealth = 80.0f;
	AttackDamage = 12.0f;
	AttackInterval = 1.5f;
}

void ACrossguardMonster::BeginPlay()
{
	Super::BeginPlay();
	HomeLocation = GetActorLocation();
}
