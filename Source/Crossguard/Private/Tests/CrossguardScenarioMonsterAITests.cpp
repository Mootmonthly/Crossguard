// Scenario tests: monster AI (aggro, attacks, leash, damage reactions), monster death (body, loot,
// kill XP), mob data, Ranged and Mage mobs, the monster team, spawner respawns and AI skill use
// (docs/design-decisions.md: Experience, "Minion and monster skills", "Monster team", "Mob weapons";
// ACrossguardMonster / ACrossguardMonsterController). Framework: CrossguardScenario.h,
// docs/editor-tests.md.
//
// The test map has no NavMesh: monsters steer straight at their goal (the controllers' direct move).
// Behaviour tests use a monster without a Mob Id (ACrossguardMonster's built-in numbers), so they do
// not depend on the Mob Forge data; data tests read the game mode's Mob Table and skip when it is unset.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AIController.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "Character/CrossguardMobRow.h"
#include "Character/CrossguardMonster.h"
#include "Combat/CrossguardProjectile.h"
#include "Combat/CrossguardSpellProjectile.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CrossguardRules/Config.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardMobSpawner.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayAbilitySpec.h"
#include "Items/CrossguardLootContainer.h"
#include "Skills/CrossguardDataPassiveAbility.h"
#include "Skills/CrossguardNativeSkills.h"
#include "Skills/CrossguardSkillCatalog.h"
#include "Skills/CrossguardSkillTypes.h"

namespace CrossguardScenarioMonsterAI
{
	using FContext = CrossguardScenario::FScenarioContext;

	// Every scene keeps far more than 15 m (aggro range) from the PIE player at (-30 m, -30 m).
	// A body is spawned at the character's transform when it dies.
	constexpr float BodySearchRadius = 200.0f;
	// ACrossguardMobSpawner's CheckDead period (seconds): a death is noticed up to this late.
	constexpr float SpawnerCheckSeconds = 2.0f;

	// The game mode's Mob Table, or null after Skip (unset) / Fail (no game mode).
	const UDataTable* RequireMobTable(FContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr, TEXT("a Crossguard game mode")))
		{
			return nullptr;
		}
		if (Mode->MobTable == nullptr)
		{
			C.Skip(TEXT("the game mode's Mob Table is unset (import Data/Mobs/MobTable.json as DT_Mobs and set it on BP_CrossguardGameMode, Crossguard | Mobs)"));
			return nullptr;
		}
		return Mode->MobTable;
	}

	// Preferred's row if it fits, else the first row (by name) that fits. Skips the scenario if none
	// does. OutId: the row name.
	const FCrossguardMobRow* FindMob(FContext& C, FName Preferred, TFunctionRef<bool(const FCrossguardMobRow&)> Fits, FName& OutId,
		const TCHAR* What)
	{
		const UDataTable* Table = RequireMobTable(C);
		if (Table == nullptr)
		{
			return nullptr;
		}
		if (const FCrossguardMobRow* Row = Table->FindRow<FCrossguardMobRow>(Preferred, TEXT("Scenario"), false); Row != nullptr && Fits(*Row))
		{
			OutId = Preferred;
			return Row;
		}
		TArray<FName> Names = Table->GetRowNames();
		Names.Sort(FNameLexicalLess());
		for (const FName& Name : Names)
		{
			const FCrossguardMobRow* Row = Table->FindRow<FCrossguardMobRow>(Name, TEXT("Scenario"), false);
			if (Row != nullptr && Fits(*Row))
			{
				OutId = Name;
				return Row;
			}
		}
		C.Skip(FString::Printf(TEXT("%s has no %s mob (Data/Mobs/Mobs.json, Mob Forge)"), *Table->GetName(), What));
		return nullptr;
	}

	const FCrossguardMobRow* MobRowOf(const FContext& C, const ACrossguardMonster* Monster)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		return Mode != nullptr && Mode->MobTable != nullptr && Monster != nullptr
			? Mode->MobTable->FindRow<FCrossguardMobRow>(Monster->MobId, TEXT("Scenario"), false)
			: nullptr;
	}

	// Who an AI-controlled pawn is fighting: both monster and minion controllers SetFocus on their
	// target while they have one and ClearFocus otherwise.
	AActor* FocusOf(const APawn* Pawn)
	{
		const AAIController* Controller = Pawn != nullptr ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		return Controller != nullptr ? Controller->GetFocusActor() : nullptr;
	}

	// Damage from Source to Target through the native damage effect (as C.Kill does), so the target's
	// OnDamaged sees Source as the attacker. Not through guards.
	bool DealDamage(AActor* Source, AActor* Target, float Attack, float Magic = 0.0f)
	{
		UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Source);
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (SourceASC == nullptr || TargetASC == nullptr)
		{
			return false;
		}
		const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, SourceASC->MakeEffectContext());
		if (!Spec.IsValid())
		{
			return false;
		}
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, Attack);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, Magic);
		SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		return true;
	}

	// Back to full Health (keeps a target alive through long waits). Not on the dead.
	void TopUpHealth(AActor* Actor)
	{
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		if (ASC != nullptr && !ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead))
		{
			ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(),
				ASC->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute()));
		}
	}

	// Log lines "<Victim> took X damage from <Source> (...)" (UCrossguardAttributeSet).
	int32 CountDamageLines(const FContext& C, const AActor* Victim, const AActor* Source)
	{
		if (Victim == nullptr || Source == nullptr)
		{
			return 0;
		}
		const FString VictimPart = FString::Printf(TEXT(": %s took "), *Victim->GetName());
		const FString SourcePart = FString::Printf(TEXT(" damage from %s ("), *Source->GetName());
		int32 Count = 0;
		for (const FString& Line : C.LogLines())
		{
			if (Line.Contains(VictimPart) && Line.Contains(SourcePart))
			{
				++Count;
			}
		}
		return Count;
	}

	// Skill ids granted on Character's ability system: native / catalog classes by their SkillId,
	// data passives by their UCrossguardSkillGrant.
	TSet<FName> GrantedSkillIds(const ACrossguardCharacter* Character)
	{
		TSet<FName> Ids;
		UCrossguardAbilitySystemComponent* ASC = Character != nullptr ? Character->GetCrossguardAbilitySystemComponent() : nullptr;
		if (ASC == nullptr)
		{
			return Ids;
		}
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (const UCrossguardSkillGrant* Grant = Cast<UCrossguardSkillGrant>(Spec.SourceObject.Get()))
			{
				Ids.Add(Grant->SkillId);
			}
			else if (const UCrossguardGameplayAbility* Ability = Cast<UCrossguardGameplayAbility>(Spec.Ability); Ability != nullptr && !Ability->SkillId.IsNone())
			{
				Ids.Add(Ability->SkillId);
			}
		}
		return Ids;
	}

	// Whether ACrossguardAICharacter::GrantSkills can grant SkillId: a class (catalog or native), or
	// a data passive from the catalog.
	bool IsGrantable(const UCrossguardSkillCatalog* Catalog, FName SkillId)
	{
		if (Catalog == nullptr)
		{
			return CrossguardNativeSkills::Find(SkillId) != nullptr;
		}
		if (Catalog->FindAbilityClass(SkillId) != nullptr)
		{
			return true;
		}
		const FCrossguardSkillRow* Row = Catalog->FindSkill(SkillId);
		return Row != nullptr && Row->Category == ECrossguardSkillCategory::Passive;
	}

	// The skills a mob should end up with: its list, bundles replaced by what they include (needs the
	// catalog), keeping only what can be granted.
	TArray<FName> ExpectedSkills(const UCrossguardSkillCatalog* Catalog, const FCrossguardMobRow& Row, TArray<FName>& OutBundles)
	{
		TArray<FName> Expected;
		for (const FName& SkillId : Row.Skills)
		{
			const FCrossguardSkillRow* Skill = Catalog != nullptr ? Catalog->FindSkill(SkillId) : nullptr;
			if (Skill != nullptr && Skill->Includes.Num() > 0)
			{
				OutBundles.AddUnique(SkillId);
				for (const FName& Included : Skill->Includes)
				{
					if (IsGrantable(Catalog, Included))
					{
						Expected.AddUnique(Included);
					}
				}
				continue;
			}
			if (IsGrantable(Catalog, SkillId))
			{
				Expected.AddUnique(SkillId);
			}
		}
		return Expected;
	}

	// Capsule bottom above the floor top (Z = 0), in cm.
	float FeetAboveFloor(const ACharacter* Character)
	{
		return static_cast<float>(Character->GetActorLocation().Z) - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	float DistanceFromHome(const ACrossguardMonster* Monster)
	{
		return static_cast<float>(FVector::Dist2D(Monster->GetActorLocation(), Monster->GetHomeLocation()));
	}

	float Distance2D(const AActor* A, const AActor* B)
	{
		return static_cast<float>(FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()));
	}

	// A box blocking everything (line of sight included), Size in cm, centred at Center.
	AStaticMeshActor* SpawnWall(FContext& C, FName Name, const FVector& Center, const FVector& Size)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!C.Require(Cube != nullptr, TEXT("/Engine/BasicShapes/Cube exists (for a wall)")))
		{
			return nullptr;
		}
		AStaticMeshActor* Wall = C.SpawnActor<AStaticMeshActor>(Name, Center, 0.0f, [Cube](AStaticMeshActor& Spawned)
		{
			UStaticMeshComponent* Mesh = Spawned.GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		});
		if (Wall != nullptr)
		{
			// The engine cube is 100 cm, centred.
			Wall->SetActorScale3D(Size / 100.0f);
		}
		return Wall;
	}

	// A minion summoned by Summoner (on its team), run by its minion controller.
	ACrossguardMinion* SpawnMinionOf(FContext& C, FName Name, ACrossguardCharacter* Summoner, const FVector& FloorPoint, float Yaw)
	{
		const FVector Location = FloorPoint + FVector(0.0f, 0.0f, GetDefault<ACrossguardMinion>()->GetDefaultHalfHeight() + 5.0f);
		ACrossguardMinion* Minion = C.SpawnActor<ACrossguardMinion>(Name, Location, Yaw, [Summoner](ACrossguardMinion& Spawned)
		{
			Spawned.SetSummoner(Summoner);
		});
		if (Minion != nullptr && Minion->GetController() == nullptr)
		{
			Minion->SpawnDefaultController();
		}
		return Minion;
	}

	// Monsters (alive or dead) that Spawner spawned (it owns them).
	TArray<ACrossguardMonster*> SpawnedBy(FContext& C, const AActor* Spawner)
	{
		TArray<ACrossguardMonster*> Result;
		for (ACrossguardMonster* Monster : C.FindAll<ACrossguardMonster>())
		{
			if (Monster->GetOwner() == Spawner)
			{
				Result.Add(Monster);
			}
		}
		return Result;
	}
}

// --- Aggro, line of sight, attack damage and interval --------------------------------------------

// A monster ignores a player beyond its aggro range and one behind a wall; it aggroes once it can see
// the player in range, walks up and hits for its Attack Damage once per Attack Interval.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMonsterAggroAndAttack, "Crossguard.Scenario.Monsters.AggroSightAndAttack", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMonsterAggroAndAttack::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 1000.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A monster (built-in numbers) and a player 20 m away"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), NAME_None, FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(2000.0f, SceneY, 0.0f), 180.0f);
		if (!C.Require(Monster != nullptr && Player != nullptr, TEXT("monster and player spawned")))
		{
			return;
		}
		C.Require(Monster->AggroRange < 1900.0f && Monster->AggroRange > 1100.0f,
			FString::Printf(TEXT("the monster's Aggro Range is between 11 m and 19 m (is %.0f cm)"), Monster->AggroRange));
	});

	Scenario.Wait(2.0f);

	Scenario.Step(TEXT("Beyond aggro range: ignored. The player moves to 10 m, behind a wall"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Monster == nullptr || Player == nullptr)
		{
			C.Fail(TEXT("monster or player gone"));
			return;
		}
		C.Test.TestNull(TEXT("No target beyond aggro range (no focus)"), FocusOf(Monster));
		C.Test.TestTrue(TEXT("The monster stays home"), DistanceFromHome(Monster) < 50.0f);
		C.Test.TestEqual(TEXT("The player is unhurt"), C.Health(Player), C.MaxHealth(Player), 0.01f);

		// 20 cm thick, 6 m wide, 4 m tall, half-way between them.
		SpawnWall(C, TEXT("Wall"), FVector(500.0f, SceneY, 200.0f), FVector(20.0f, 600.0f, 400.0f));
		C.Place(Player, FVector(1000.0f, SceneY, 0.0f), 180.0f);
	});

	Scenario.Wait(2.0f);

	Scenario.Step(TEXT("In range without line of sight: ignored. The wall goes"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		AActor* Wall = C.FindActor(TEXT("Wall"));
		if (Monster == nullptr || Player == nullptr || !C.Require(Wall != nullptr, TEXT("the wall exists")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The player stands within aggro range"), Distance2D(Monster, Player) < Monster->AggroRange);
		C.Test.TestNull(TEXT("No target without line of sight (no focus)"), FocusOf(Monster));
		C.Test.TestTrue(TEXT("The monster still stays home"), DistanceFromHome(Monster) < 50.0f);
		C.Test.TestEqual(TEXT("The player is still unhurt"), C.Health(Player), C.MaxHealth(Player), 0.01f);
		Wall->Destroy();
	});

	Scenario.WaitUntil(TEXT("the monster targets the player it now sees"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		const AActor* Player = C.FindActor(TEXT("Player"));
		return Monster != nullptr && Player != nullptr && FocusOf(Monster) == Player;
	});

	Scenario.Step(TEXT("Watch the player's Health"), [](FScenarioContext& C)
	{
		C.SetNumber(TEXT("Last"), C.Health(C.FindActor(TEXT("Player"))));
		C.SetNumber(TEXT("Hits"), 0.0);
	});

	// Every frame: each drop in Health is one hit.
	Scenario.WaitUntil(TEXT("the monster hits the player twice"), 10.0f, [](FScenarioContext& C)
	{
		const AActor* Player = C.FindActor(TEXT("Player"));
		if (!C.Require(Player != nullptr && !C.IsDead(Player), TEXT("the player is alive")))
		{
			return false;
		}
		const float Health = C.Health(Player);
		const float Last = static_cast<float>(C.Number(TEXT("Last")));
		int32 Hits = static_cast<int32>(C.Number(TEXT("Hits")));
		if (Health < Last - 0.01f && Hits < 2)
		{
			++Hits;
			C.SetNumber(FName(*FString::Printf(TEXT("Hit%dTime"), Hits)), C.Now());
			C.SetNumber(FName(*FString::Printf(TEXT("Hit%dDamage"), Hits)), Last - Health);
			C.SetNumber(TEXT("Hits"), Hits);
		}
		C.SetNumber(TEXT("Last"), Health);
		return Hits >= 2;
	});

	Scenario.Step(TEXT("Each hit is its Attack Damage, one per Attack Interval"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Monster == nullptr || Player == nullptr)
		{
			return;
		}
		const float Expected = C.ExpectedDamage(Monster, Player, Monster->AttackDamage);
		C.Test.AddInfo(FString::Printf(TEXT("Hits: %.2f and %.2f (expected %.2f each), %.2f s apart (Attack Interval %.2f s)"),
			C.Number(TEXT("Hit1Damage")), C.Number(TEXT("Hit2Damage")), Expected, C.Number(TEXT("Hit2Time")) - C.Number(TEXT("Hit1Time")),
			Monster->AttackInterval));
		C.Test.TestEqual(TEXT("First hit: the monster's Attack Damage through the damage formula"), static_cast<float>(C.Number(TEXT("Hit1Damage"))),
			Expected, 0.05f);
		C.Test.TestEqual(TEXT("Second hit: the same"), static_cast<float>(C.Number(TEXT("Hit2Damage"))), Expected, 0.05f);
		C.Test.TestTrue(TEXT("It hit from within its Attack Range"), Distance2D(Monster, Player) <= Monster->AttackRange + 1.0f);
		const float Interval = static_cast<float>(C.Number(TEXT("Hit2Time")) - C.Number(TEXT("Hit1Time")));
		if (FMath::IsNearlyZero(C.Stat(Monster, UCrossguardAttributeSet::GetActionSpeedAttribute())))
		{
			C.Test.TestEqual(TEXT("Seconds between hits = Attack Interval (no Action Speed)"), Interval, Monster->AttackInterval, 0.1f);
		}
		else
		{
			C.Test.TestTrue(TEXT("Seconds between hits <= Attack Interval (Action Speed shortens it)"), Interval <= Monster->AttackInterval + 0.1f);
		}
	});

	Scenario.Run();
	return true;
}

// --- Leash ----------------------------------------------------------------------------------------

// Pulled beyond its leash with nobody in reach, the monster walks home and is fully healed there; a
// player standing beyond the leash (though within aggro range) does not pull it out again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMonsterLeashReturnsHome, "Crossguard.Scenario.Monsters.LeashReturnsHome", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMonsterLeashReturnsHome::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 2000.0f;
	// A short leash so the chase is short.
	static constexpr float Leash = 600.0f;
	FScenario Scenario(*this);
	Scenario.TimeDilation(2.0f);

	Scenario.Step(TEXT("A monster with a 6 m leash and a player 4 m away"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), NAME_None, FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(400.0f, SceneY, 0.0f), 180.0f);
		if (!C.Require(Monster != nullptr && Player != nullptr, TEXT("monster and player spawned")))
		{
			return;
		}
		Monster->LeashRange = Leash;
	});

	Scenario.WaitUntil(TEXT("the monster targets the player"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		return Monster != nullptr && FocusOf(Monster) == C.FindActor(TEXT("Player"));
	});

	Scenario.Step(TEXT("The player hurts it, then steps 12 m from its home: beyond the leash, still in aggro range"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Monster == nullptr || Player == nullptr)
		{
			C.Fail(TEXT("monster or player gone"));
			return;
		}
		DealDamage(Player, Monster, 30.0f);
		if (!C.Require(C.Health(Monster) < C.MaxHealth(Monster) && !C.IsDead(Monster), TEXT("the monster is hurt, not dead")))
		{
			return;
		}
		C.Place(Player, FVector(1200.0f, SceneY, 0.0f), 180.0f);
		C.Test.TestTrue(TEXT("The player now stands beyond the leash from the monster's home"),
			FVector::Dist(Player->GetActorLocation(), Monster->GetHomeLocation()) > Monster->LeashRange);
	});

	Scenario.WaitUntil(TEXT("the chase pulls the monster beyond its leash"), 5.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		TopUpHealth(C.FindActor(TEXT("Player")));
		return Monster != nullptr && DistanceFromHome(Monster) > Monster->LeashRange;
	});

	Scenario.WaitUntil(TEXT("the monster walks back home and is healed to full"), 15.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		TopUpHealth(C.FindActor(TEXT("Player")));
		if (Monster == nullptr)
		{
			return false;
		}
		if (FocusOf(Monster) == nullptr && DistanceFromHome(Monster) > 200.0f)
		{
			C.SetNumber(TEXT("SawReturning"), 1.0);
		}
		return DistanceFromHome(Monster) <= 150.0f && FMath::IsNearlyEqual(C.Health(Monster), C.MaxHealth(Monster), 0.01f);
	});

	Scenario.Step(TEXT("Home and healed; it walked back without a target"), [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		if (Monster == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("It walked home with no target (focus cleared on the way)"), C.Number(TEXT("SawReturning")) > 0.0);
		C.Test.TestNull(TEXT("No target at home"), FocusOf(Monster));
	});

	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("The player beyond the leash is not picked up again"), [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Monster == nullptr || Player == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("The player is within the monster's aggro range"), Distance2D(Monster, Player) < Monster->AggroRange);
		C.Test.TestNull(TEXT("...but beyond its leash, so not a target"), FocusOf(Monster));
		C.Test.TestTrue(TEXT("The monster stays home"), DistanceFromHome(Monster) <= 150.0f);
		C.Test.TestEqual(TEXT("And stays at full Health"), C.Health(Monster), C.MaxHealth(Monster), 0.01f);
	});

	Scenario.Run();
	return true;
}

// Pulled beyond its leash while another player stands in aggro range within the leash, the monster
// turns on that player instead of walking home, and is not healed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMonsterLeashRetargets, "Crossguard.Scenario.Monsters.LeashRetargets", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMonsterLeashRetargets::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 3000.0f;
	static constexpr float Leash = 600.0f;
	FScenario Scenario(*this);
	Scenario.TimeDilation(2.0f);

	Scenario.Step(TEXT("A monster with a 6 m leash and player A 4 m away"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), NAME_None, FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(400.0f, SceneY, 0.0f), 180.0f);
		if (!C.Require(Monster != nullptr && A != nullptr, TEXT("monster and A spawned")))
		{
			return;
		}
		Monster->LeashRange = Leash;
	});

	Scenario.WaitUntil(TEXT("the monster targets A"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		return Monster != nullptr && FocusOf(Monster) == C.FindActor(TEXT("A"));
	});

	Scenario.Step(TEXT("B arrives 4 m on the other side of home; A hurts the monster and steps beyond the leash"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		ACrossguardCharacter* A = C.Get<ACrossguardCharacter>(TEXT("A"));
		if (Monster == nullptr || A == nullptr)
		{
			C.Fail(TEXT("monster or A gone"));
			return;
		}
		ACrossguardCharacter* B = C.SpawnPlayer(TEXT("B"), FVector(-400.0f, SceneY, 0.0f), 0.0f);
		if (!C.Require(B != nullptr, TEXT("B spawned")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The monster keeps its target A while B arrives"), FocusOf(Monster) == A);
		DealDamage(A, Monster, 30.0f);
		C.Place(A, FVector(1200.0f, SceneY, 0.0f), 180.0f);
	});

	Scenario.WaitUntil(TEXT("pulled beyond the leash, the monster turns on B"), 8.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		TopUpHealth(C.FindActor(TEXT("A")));
		TopUpHealth(C.FindActor(TEXT("B")));
		if (Monster != nullptr && DistanceFromHome(Monster) > Monster->LeashRange)
		{
			C.SetNumber(TEXT("WasPulled"), 1.0);
		}
		return Monster != nullptr && FocusOf(Monster) == C.FindActor(TEXT("B"));
	});

	Scenario.Step(TEXT("Retargeted, not reset"), [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		if (Monster == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("The chase had pulled it beyond its leash"), C.Number(TEXT("WasPulled")) > 0.0);
		C.Test.TestTrue(TEXT("Not healed: retargeting is no reset"), C.Health(Monster) < C.MaxHealth(Monster) - 0.01f);
	});

	Scenario.Run();
	return true;
}

// --- Damage from any range ------------------------------------------------------------------------

// A player beyond aggro range is ignored until they hurt the monster; then it turns on them and closes in.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMonsterReactsToDamage, "Crossguard.Scenario.Monsters.ReactsToDamageFromAnyRange", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMonsterReactsToDamage::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 4000.0f;
	// Beyond aggro (15 m), within the leash from home (30 m).
	static constexpr float PlayerDistance = 2500.0f;
	FScenario Scenario(*this);
	Scenario.TimeDilation(2.0f);

	Scenario.Step(TEXT("A monster and a player 25 m away"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), NAME_None, FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(PlayerDistance, SceneY, 0.0f), 180.0f);
		if (!C.Require(Monster != nullptr && Player != nullptr, TEXT("monster and player spawned")))
		{
			return;
		}
		C.Require(Monster->AggroRange < PlayerDistance - 100.0f && Monster->LeashRange > PlayerDistance + 100.0f,
			TEXT("25 m is beyond the monster's aggro range and within its leash"));
	});

	Scenario.Wait(1.5f);

	Scenario.Step(TEXT("Ignored; then the player hurts it from 25 m"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Monster == nullptr || Player == nullptr)
		{
			C.Fail(TEXT("monster or player gone"));
			return;
		}
		C.Test.TestNull(TEXT("No target before the damage"), FocusOf(Monster));
		C.Test.TestTrue(TEXT("Home before the damage"), DistanceFromHome(Monster) < 50.0f);
		C.Require(DealDamage(Player, Monster, 5.0f), TEXT("the damage was applied"));
	});

	Scenario.WaitUntil(TEXT("the monster turns on its attacker"), 2.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		return Monster != nullptr && FocusOf(Monster) == C.FindActor(TEXT("Player"));
	});

	Scenario.WaitUntil(TEXT("the monster closes in and hits the attacker"), 12.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		const AActor* Player = C.FindActor(TEXT("Player"));
		return Monster != nullptr && Player != nullptr && CountDamageLines(C, Player, Monster) > 0;
	});

	Scenario.Run();
	return true;
}

// --- Monster team ---------------------------------------------------------------------------------

// Two monsters and a minion one of them summoned never fight each other; when a player comes, all
// three go for the player (the summon fights players, not monsters).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMonsterTeam, "Crossguard.Scenario.Monsters.MonsterTeam", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMonsterTeam::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 5000.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("Monsters M1 and M2 and M1's minion stand together"), [](FScenarioContext& C)
	{
		ACrossguardMonster* M1 = C.SpawnMonster(TEXT("M1"), NAME_None, FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardMonster* M2 = C.SpawnMonster(TEXT("M2"), NAME_None, FVector(250.0f, SceneY, 0.0f), 180.0f);
		if (!C.Require(M1 != nullptr && M2 != nullptr, TEXT("both monsters spawned")))
		{
			return;
		}
		ACrossguardMinion* Minion = SpawnMinionOf(C, TEXT("Minion"), M1, FVector(-250.0f, SceneY, 0.0f), 0.0f);
		if (!C.Require(Minion != nullptr && Minion->GetController() != nullptr, TEXT("M1's minion spawned with its controller")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The minion's summoner is M1"), Minion->GetSummoner() == M1);
	});

	Scenario.Wait(3.0f);

	Scenario.Step(TEXT("Nobody fought; a player comes 8 m away"), [](FScenarioContext& C)
	{
		for (const TCHAR* Name : {TEXT("M1"), TEXT("M2"), TEXT("Minion")})
		{
			const ACrossguardCharacter* Character = C.Get<ACrossguardCharacter>(Name);
			if (Character == nullptr)
			{
				C.Fail(TEXT("a monster or the minion is gone"));
				return;
			}
			C.Test.TestNull(*FString::Printf(TEXT("%s has no target among monsters"), Name), FocusOf(Character));
			C.Test.TestEqual(*FString::Printf(TEXT("%s is unhurt"), Name), C.Health(Character), C.MaxHealth(Character), 0.01f);
		}
		C.ClearLog();
		C.SpawnPlayer(TEXT("Player"), FVector(800.0f, SceneY, 0.0f), 180.0f);
	});

	Scenario.WaitUntil(TEXT("both monsters and the minion target the player"), 4.0f, [](FScenarioContext& C)
	{
		AActor* Player = C.FindActor(TEXT("Player"));
		TopUpHealth(Player);
		return Player != nullptr && FocusOf(Cast<APawn>(C.FindActor(TEXT("M1")))) == Player && FocusOf(Cast<APawn>(C.FindActor(TEXT("M2")))) == Player
			&& FocusOf(Cast<APawn>(C.FindActor(TEXT("Minion")))) == Player;
	});

	Scenario.WaitUntil(TEXT("the monsters' minion hits the player"), 8.0f, [](FScenarioContext& C)
	{
		AActor* Player = C.FindActor(TEXT("Player"));
		const AActor* Minion = C.FindActor(TEXT("Minion"));
		if (!C.Require(Player != nullptr && Minion != nullptr, TEXT("player and minion still there")))
		{
			return false;
		}
		TopUpHealth(Player);
		return CountDamageLines(C, Player, Minion) > 0;
	});

	Scenario.Step(TEXT("No monster or minion hurt another"), [](FScenarioContext& C)
	{
		const TArray<FName> Names = {TEXT("M1"), TEXT("M2"), TEXT("Minion")};
		for (const FName& Victim : Names)
		{
			const AActor* VictimActor = C.FindActor(Victim);
			if (VictimActor == nullptr)
			{
				continue;
			}
			C.Test.TestEqual(*FString::Printf(TEXT("%s is unhurt"), *Victim.ToString()), C.Health(VictimActor), C.MaxHealth(VictimActor), 0.01f);
			for (const FName& Source : Names)
			{
				C.Test.TestEqual(*FString::Printf(TEXT("No damage from %s to %s"), *Source.ToString(), *Victim.ToString()),
					CountDamageLines(C, VictimActor, C.FindActor(Source)), 0);
			}
		}
	});

	Scenario.Run();
	return true;
}

// --- Death: body, loot, kill XP ---------------------------------------------------------------------

// Checklist / Experience: a player kills monsters of one mob -> each leaves a body (with a roll of
// its loot table: over 6 bodies at least one holds something), the player gets Basic Monster XP for
// each, and a dead monster cannot be revived.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMonsterDeath, "Crossguard.Scenario.Monsters.DeathBodyLootXp", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMonsterDeath::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 6000.0f;
	// skeleton_mage: 1-4 rolls, 1 in 8 is nothing (Data/Mobs/Mobs.json): 6 empty bodies are
	// about 4 in a million.
	static constexpr int32 Count = 6;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player kills 6 monsters of one mob at once"), [](FScenarioContext& C)
	{
		FName MobId;
		const FCrossguardMobRow* MobData = FindMob(C, TEXT("skeleton_mage"), [](const FCrossguardMobRow& Mob)
		{
			return Mob.Kind == ECrossguardMobKind::Monster && !Mob.LootTableId.IsNone();
		}, MobId, TEXT("non-boss mob with a loot table"));
		if (MobData == nullptr)
		{
			return;
		}
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(-1500.0f, SceneY, 0.0f), 0.0f);
		const ACrossguardPlayerState* Progression = C.PlayerState(Player);
		if (!C.Require(Player != nullptr && Progression != nullptr && Progression->IsInRaid(), TEXT("a player in a raid")))
		{
			return;
		}
		C.SetNumber(TEXT("RaidXpBefore"), Progression->GetRaidXp());
		C.Test.AddInfo(FString::Printf(TEXT("Mob: %s, loot table %s"), *MobId.ToString(), *MobData->LootTableId.ToString()));
		// Spawned and killed in the same frame: no time to fight back.
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FName Name(*FString::Printf(TEXT("Monster%d"), Index));
			ACrossguardMonster* Monster = C.SpawnMonster(Name, MobId, FVector(Index * 500.0f, SceneY, 0.0f), 180.0f);
			if (!C.Require(Monster != nullptr, FString::Printf(TEXT("%s spawned"), *Name.ToString())))
			{
				return;
			}
			C.Test.TestTrue(*FString::Printf(TEXT("%s rolls its mob's loot table"), *Name.ToString()), Monster->LootTableId == MobData->LootTableId);
			C.Kill(Monster, Player);
		}
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("6 bodies, at least one with loot; Basic Monster XP for each; no revive"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		const ACrossguardPlayerState* Progression = C.PlayerState(Player);
		if (!C.Require(Progression != nullptr, TEXT("the player still has its player state")))
		{
			return;
		}
		int32 Items = 0;
		int32 NonEmpty = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FString Name = FString::Printf(TEXT("Monster%d"), Index);
			ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(FName(*Name));
			if (Monster == nullptr)
			{
				continue;
			}
			C.Test.TestTrue(*FString::Printf(TEXT("%s is dead"), *Name), C.IsDead(Monster));
			C.Test.TestFalse(*FString::Printf(TEXT("%s cannot be revived"), *Name), Monster->Revive(0.5f));
			const ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(Monster->GetActorLocation(), BodySearchRadius);
			if (C.Test.TestNotNull(*FString::Printf(TEXT("%s left a body"), *Name), Body))
			{
				Items += Body->GetItems().Num();
				NonEmpty += Body->GetItems().Num() > 0 ? 1 : 0;
			}
		}
		C.Test.AddInfo(FString::Printf(TEXT("%d of %d bodies hold loot, %d stacks in all"), NonEmpty, Count, Items));
		C.Test.TestTrue(TEXT("The loot table was rolled into the bodies (at least one holds something; set the game mode's Loot tables if not)"),
			NonEmpty > 0);
		C.Test.TestEqual(TEXT("Raid XP: Basic Monster XP per kill"), Progression->GetRaidXp() - static_cast<int32>(C.Number(TEXT("RaidXpBefore"))),
			Count * Crossguard::FXpConfig().BasicMonster);
	});

	Scenario.Run();
	return true;
}

// A boss mob (Kind Boss) gives Boss Monster XP to its killer and leaves a body with its loot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioBossKill, "Crossguard.Scenario.Monsters.BossKillXpAndLoot", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioBossKill::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 3000.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A player kills a boss the moment it appears"), [](FScenarioContext& C)
	{
		FName MobId;
		const FCrossguardMobRow* Boss = FindMob(C, TEXT("mithril_dragon"), [](const FCrossguardMobRow& Mob)
		{
			return Mob.Kind == ECrossguardMobKind::Boss;
		}, MobId, TEXT("boss"));
		if (Boss == nullptr)
		{
			return;
		}
		// Bosses see far (the dragon 50 m): far from the PIE player, and killed in the same frame.
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(-1000.0f, SceneY, 0.0f), 0.0f);
		const ACrossguardPlayerState* Progression = C.PlayerState(Player);
		if (!C.Require(Player != nullptr && Progression != nullptr && Progression->IsInRaid(), TEXT("a player in a raid")))
		{
			return;
		}
		C.SetNumber(TEXT("RaidXpBefore"), Progression->GetRaidXp());
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Boss"), MobId, FVector(2000.0f, SceneY, 0.0f), 180.0f);
		if (!C.Require(Monster != nullptr, TEXT("the boss spawned")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("A boss gives Boss Monster XP"), Monster->KillXpSource == ECrossguardXpSource::BossMonster);
		C.Kill(Monster, Player);
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Boss Monster XP and a body"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Boss"));
		const ACrossguardPlayerState* Progression = C.PlayerState(Player);
		if (Monster == nullptr || !C.Require(Progression != nullptr, TEXT("the player still has its player state")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("The boss is dead"), C.IsDead(Monster));
		C.Test.TestEqual(TEXT("Raid XP: Boss Monster XP"), Progression->GetRaidXp() - static_cast<int32>(C.Number(TEXT("RaidXpBefore"))),
			Crossguard::FXpConfig().BossMonster);
		// Scaled bosses are wide: the body is at the boss's own transform.
		const ACrossguardLootContainer* Body = C.FindNearest<ACrossguardLootContainer>(Monster->GetActorLocation(), BodySearchRadius);
		if (C.Test.TestNotNull(TEXT("The boss left a body"), Body))
		{
			C.Test.AddInfo(FString::Printf(TEXT("Boss body: %d stacks (loot table %s)"), Body->GetItems().Num(), *Monster->LootTableId.ToString()));
			// mob_mithril_dragon: 5-12 rolls, never nothing (Data/Mobs/Mobs.json).
			if (Monster->MobId == FName(TEXT("mithril_dragon")))
			{
				C.Test.TestTrue(TEXT("The dragon's body holds loot (5-12 rolls, never nothing; set the game mode's Loot tables if not)"),
					Body->GetItems().Num() > 0);
			}
		}
	});

	Scenario.Run();
	return true;
}

// --- Mob data ---------------------------------------------------------------------------------------

// Every mob of the Mob Table (up to 6): its stats, weapon and Impact Power, loot, kill XP, size (feet
// on the floor) and skills (bundles expanded) are applied when it begins play.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMobDataApplied, "Crossguard.Scenario.Monsters.MobDataApplied", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMobDataApplied::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 3500.0f;
	static constexpr int32 MaxMobs = 6;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("One monster of each mob, 12 m apart, nobody around"), [](FScenarioContext& C)
	{
		const UDataTable* Table = RequireMobTable(C);
		if (Table == nullptr)
		{
			return;
		}
		TArray<FName> Names = Table->GetRowNames();
		Names.Sort(FNameLexicalLess());
		if (!C.Require(Names.Num() > 0, TEXT("the Mob Table has rows")))
		{
			return;
		}
		const int32 Count = FMath::Min(Names.Num(), MaxMobs);
		C.SetNumber(TEXT("Count"), Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FName Name(*FString::Printf(TEXT("Mob%d"), Index));
			ACrossguardMonster* Monster = C.SpawnMonster(Name, Names[Index], FVector(-3000.0f + Index * 1200.0f, SceneY, 0.0f), 0.0f);
			C.Require(Monster != nullptr, FString::Printf(TEXT("%s spawned"), *Names[Index].ToString()));
		}
	});

	// Settle on the floor.
	Scenario.Wait(1.0f);

	Scenario.Step(TEXT("Each monster has its mob's data"), [](FScenarioContext& C)
	{
		const ACrossguardGameMode* Mode = C.GameMode();
		if (!C.Require(Mode != nullptr, TEXT("the game mode")))
		{
			return;
		}
		const UCrossguardSkillCatalog* Catalog = Mode->MobSkillCatalog;
		if (Catalog == nullptr)
		{
			C.Test.AddWarning(TEXT("The game mode's Mob Skill Catalog is unset: only native skills are granted and bundles are not expanded "
				"(set it to DA_SkillCatalog, Crossguard | Mobs)"));
		}
		const int32 Count = static_cast<int32>(C.Number(TEXT("Count")));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(FName(*FString::Printf(TEXT("Mob%d"), Index)));
			const FCrossguardMobRow* Row = MobRowOf(C, Monster);
			if (Monster == nullptr || Row == nullptr)
			{
				C.Test.AddError(FString::Printf(TEXT("Mob%d or its row is missing"), Index));
				continue;
			}
			const FString Id = Monster->MobId.ToString();
			const auto Check = [&C, &Id](const TCHAR* What, bool bOk)
			{
				C.Test.TestTrue(*FString::Printf(TEXT("%s: %s"), *Id, What), bOk);
			};
			const auto CheckNumber = [&C, &Id](const TCHAR* What, float Actual, float Expected)
			{
				C.Test.TestEqual(*FString::Printf(TEXT("%s: %s"), *Id, What), Actual, Expected, 0.01f);
			};

			// Stats.
			Check(TEXT("Combat Role"), Monster->CombatRole == Row->CombatRole);
			Check(TEXT("wields a weapon (or not)"), Monster->bWieldsWeapon == Row->bWieldsWeapon);
			if (Row->bWieldsWeapon)
			{
				Check(TEXT("Weapon Type"), Monster->WeaponType == Row->WeaponType);
				Check(TEXT("two-handed (or not)"), Monster->bTwoHandedWeapon == Row->bTwoHandedWeapon);
				Check(TEXT("holds its weapon"), Monster->GetWieldedWeapon().IsSet() && Monster->GetWieldedWeapon().GetValue() == Row->WeaponType);
			}
			C.Test.TestEqual(*FString::Printf(TEXT("%s: Impact Power"), *Id), Monster->ImpactPower, Row->ImpactPower);
			CheckNumber(TEXT("Attack Damage"), Monster->AttackDamage, Row->AttackDamage);
			CheckNumber(TEXT("Attack Interval"), Monster->AttackInterval, Row->AttackInterval);
			CheckNumber(TEXT("Attack Range"), Monster->AttackRange, Row->AttackRange);
			CheckNumber(TEXT("Fireball Interval"), Monster->FireballInterval, Row->FireballInterval);
			CheckNumber(TEXT("Keep Distance"), Monster->KeepDistance, Row->KeepDistance);
			CheckNumber(TEXT("Aggro Range"), Monster->AggroRange, Row->AggroRange);
			CheckNumber(TEXT("Leash Range"), Monster->LeashRange, Row->LeashRange);
			Check(TEXT("Loot Table Id"), Monster->LootTableId == Row->LootTableId);
			Check(TEXT("kill XP by Kind"), Monster->KillXpSource
				== (Row->Kind == ECrossguardMobKind::Boss ? ECrossguardXpSource::BossMonster : ECrossguardXpSource::BasicMonster));
			if (FMath::IsNearlyZero(C.Stat(Monster, UCrossguardAttributeSet::GetMaxHealthBonusAttribute())))
			{
				CheckNumber(TEXT("Max Health"), C.MaxHealth(Monster), Row->Health);
			}
			else
			{
				C.Test.AddInfo(FString::Printf(TEXT("%s: Max Health %.1f (Health %.1f with a Max HP Bonus from its skills)"), *Id, C.MaxHealth(Monster),
					Row->Health));
			}
			CheckNumber(TEXT("full Health"), C.Health(Monster), C.MaxHealth(Monster));
			CheckNumber(TEXT("walk speed = Move Speed x the Move Speed stat"), Monster->GetCharacterMovement()->MaxWalkSpeed,
				Row->MoveSpeed * FMath::Max(C.Stat(Monster, UCrossguardAttributeSet::GetMoveSpeedAttribute()), 0.0f));

			// Size: scaled, standing on the floor (not in it, not floating), home where it stands.
			CheckNumber(TEXT("scale"), static_cast<float>(Monster->GetActorScale3D().X), FMath::Max(Row->Scale, 0.05f));
			const float Feet = FeetAboveFloor(Monster);
			C.Test.AddInfo(FString::Printf(TEXT("%s: scale %.2f, capsule bottom %.1f cm above the floor"), *Id, Monster->GetActorScale3D().X, Feet));
			Check(TEXT("stands on the floor (capsule bottom within -3..15 cm of it)"), Feet > -3.0f && Feet < 15.0f);
			Check(TEXT("home is where it stands"), DistanceFromHome(Monster) < 50.0f);

			// Skills: its list with bundles expanded.
			TArray<FName> Bundles;
			const TArray<FName> Expected = ExpectedSkills(Catalog, *Row, Bundles);
			const TSet<FName> Granted = GrantedSkillIds(Monster);
			for (const FName& Skill : Expected)
			{
				Check(*FString::Printf(TEXT("has skill %s%s"), *Skill.ToString(), Row->Skills.Contains(Skill) ? TEXT("") : TEXT(" (from a bundle)")),
					Granted.Contains(Skill));
			}
			for (const FName& Skill : Row->Skills)
			{
				if (!Expected.Contains(Skill) && !Bundles.Contains(Skill))
				{
					C.Test.AddInfo(FString::Printf(TEXT("%s: skill %s not checked (no ability class%s)"), *Id, *Skill.ToString(),
						Catalog == nullptr ? TEXT(" without the Mob Skill Catalog") : TEXT("")));
				}
			}
			if (!Bundles.IsEmpty())
			{
				C.Test.AddInfo(FString::Printf(TEXT("%s: bundles expanded: %s"), *Id, *FString::JoinBy(Bundles, TEXT(", "), [](const FName& Name) { return Name.ToString(); })));
			}
		}
	});

	Scenario.Run();
	return true;
}

// --- Ranged and Mage mobs -------------------------------------------------------------------------

// A Ranged mob walks in to its Keep Distance (not closer) and shoots arrows at the player.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioRangedMob, "Crossguard.Scenario.Monsters.RangedKeepsDistanceAndShoots", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioRangedMob::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 7000.0f;
	static constexpr float StartDistance = 1300.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A Ranged mob and a player 13 m away"), [](FScenarioContext& C)
	{
		FName MobId;
		const FCrossguardMobRow* Ranged = FindMob(C, TEXT("skeleton_archer"), [](const FCrossguardMobRow& Mob)
		{
			return Mob.CombatRole == ECrossguardCombatRole::Ranged && Mob.Kind == ECrossguardMobKind::Monster
				&& Mob.AggroRange > StartDistance + 100.0f && Mob.KeepDistance < StartDistance - 200.0f && Mob.AttackRange > StartDistance;
		}, MobId, TEXT("Ranged mob (aggro and reach beyond 13 m, Keep Distance under 11 m)"));
		if (Ranged == nullptr)
		{
			return;
		}
		ACrossguardMonster* Archer = C.SpawnMonster(TEXT("Archer"), MobId, FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(StartDistance, SceneY, 0.0f), 180.0f);
		if (!C.Require(Archer != nullptr && Player != nullptr, TEXT("archer and player spawned")))
		{
			return;
		}
		C.SetNumber(TEXT("MinDistance"), StartDistance);
	});

	Scenario.WaitUntil(TEXT("an arrow from the archer hits the player"), 6.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Archer = C.Get<ACrossguardMonster>(TEXT("Archer"));
		AActor* Player = C.FindActor(TEXT("Player"));
		if (Archer == nullptr || !C.Require(Player != nullptr, TEXT("the player is there")))
		{
			return false;
		}
		TopUpHealth(Player);
		for (const ACrossguardProjectile* Projectile : C.FindAll<ACrossguardProjectile>())
		{
			if (Projectile->GetOwner() == Archer && !Projectile->IsA<ACrossguardSpellProjectile>())
			{
				C.SetNumber(TEXT("SawArrow"), 1.0);
			}
		}
		C.SetNumber(TEXT("MinDistance"), FMath::Min(C.Number(TEXT("MinDistance")), static_cast<double>(Distance2D(Archer, Player))));
		return C.Number(TEXT("SawArrow")) > 0.0 && CountDamageLines(C, Player, Archer) > 0;
	});

	// Time to walk in and stop.
	Scenario.WaitUntil(TEXT("the archer settles"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Archer = C.Get<ACrossguardMonster>(TEXT("Archer"));
		AActor* Player = C.FindActor(TEXT("Player"));
		if (Archer != nullptr && Player != nullptr)
		{
			TopUpHealth(Player);
			C.SetNumber(TEXT("MinDistance"), FMath::Min(C.Number(TEXT("MinDistance")), static_cast<double>(Distance2D(Archer, Player))));
		}
		// Watches for 2 game seconds.
		if (C.Number(TEXT("SettleEnd")) <= 0.0)
		{
			C.SetNumber(TEXT("SettleEnd"), C.Now() + 2.0);
		}
		return C.Now() >= C.Number(TEXT("SettleEnd"));
	});

	Scenario.Step(TEXT("It stopped at its Keep Distance"), [](FScenarioContext& C)
	{
		const ACrossguardMonster* Archer = C.Get<ACrossguardMonster>(TEXT("Archer"));
		const AActor* Player = C.FindActor(TEXT("Player"));
		if (Archer == nullptr || Player == nullptr)
		{
			return;
		}
		const float Distance = Distance2D(Archer, Player);
		const float Closest = static_cast<float>(C.Number(TEXT("MinDistance")));
		C.Test.AddInfo(FString::Printf(TEXT("Archer to player: %.0f cm now, %.0f cm at the closest (Keep Distance %.0f, start %.0f)"), Distance, Closest,
			Archer->KeepDistance, StartDistance));
		C.Test.TestTrue(TEXT("It walked in toward its Keep Distance"), Distance < StartDistance - 200.0f);
		C.Test.TestTrue(TEXT("It never came closer than its Keep Distance (-1 m)"), Closest >= Archer->KeepDistance - 100.0f);
		C.Test.TestTrue(TEXT("It stands about its Keep Distance away (+1.5 m)"), Distance <= Archer->KeepDistance + 150.0f);
		C.Test.TestTrue(TEXT("Its arrows hit the player more than once"), CountDamageLines(C, Player, Archer) >= 2);
	});

	Scenario.Run();
	return true;
}

// A Mage mob keeps its distance and casts fireballs at the player.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMageMob, "Crossguard.Scenario.Monsters.MageKeepsDistanceAndCasts", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMageMob::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 8000.0f;
	static constexpr float StartDistance = 1300.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("A Mage mob and a player 13 m away"), [](FScenarioContext& C)
	{
		FName MobId;
		const FCrossguardMobRow* Mage = FindMob(C, TEXT("skeleton_mage"), [](const FCrossguardMobRow& Mob)
		{
			return Mob.CombatRole == ECrossguardCombatRole::Mage && Mob.Kind == ECrossguardMobKind::Monster
				&& Mob.AggroRange > StartDistance + 100.0f && Mob.KeepDistance < StartDistance - 200.0f;
		}, MobId, TEXT("Mage mob (aggro beyond 13 m, Keep Distance under 11 m)"));
		if (Mage == nullptr)
		{
			return;
		}
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Mage"), MobId, FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(StartDistance, SceneY, 0.0f), 180.0f);
		if (!C.Require(Monster != nullptr && Player != nullptr, TEXT("mage and player spawned")))
		{
			return;
		}
		C.SetNumber(TEXT("MinDistance"), StartDistance);
	});

	Scenario.WaitUntil(TEXT("a fireball from the mage hits the player"), 6.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Mage = C.Get<ACrossguardMonster>(TEXT("Mage"));
		AActor* Player = C.FindActor(TEXT("Player"));
		if (Mage == nullptr || !C.Require(Player != nullptr, TEXT("the player is there")))
		{
			return false;
		}
		TopUpHealth(Player);
		for (const ACrossguardSpellProjectile* Spell : C.FindAll<ACrossguardSpellProjectile>())
		{
			if (Spell->GetOwner() == Mage)
			{
				C.SetNumber(TEXT("SawFireball"), 1.0);
			}
		}
		C.SetNumber(TEXT("MinDistance"), FMath::Min(C.Number(TEXT("MinDistance")), static_cast<double>(Distance2D(Mage, Player))));
		return C.Number(TEXT("SawFireball")) > 0.0 && CountDamageLines(C, Player, Mage) > 0;
	});

	Scenario.WaitUntil(TEXT("the mage settles"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Mage = C.Get<ACrossguardMonster>(TEXT("Mage"));
		AActor* Player = C.FindActor(TEXT("Player"));
		if (Mage != nullptr && Player != nullptr)
		{
			TopUpHealth(Player);
			C.SetNumber(TEXT("MinDistance"), FMath::Min(C.Number(TEXT("MinDistance")), static_cast<double>(Distance2D(Mage, Player))));
		}
		// Watches for 2 game seconds.
		if (C.Number(TEXT("SettleEnd")) <= 0.0)
		{
			C.SetNumber(TEXT("SettleEnd"), C.Now() + 2.0);
		}
		return C.Now() >= C.Number(TEXT("SettleEnd"));
	});

	Scenario.Step(TEXT("It keeps its distance"), [](FScenarioContext& C)
	{
		const ACrossguardMonster* Mage = C.Get<ACrossguardMonster>(TEXT("Mage"));
		const AActor* Player = C.FindActor(TEXT("Player"));
		if (Mage == nullptr || Player == nullptr)
		{
			return;
		}
		const float Distance = Distance2D(Mage, Player);
		const float Closest = static_cast<float>(C.Number(TEXT("MinDistance")));
		C.Test.AddInfo(FString::Printf(TEXT("Mage to player: %.0f cm now, %.0f cm at the closest (Keep Distance %.0f)"), Distance, Closest,
			Mage->KeepDistance));
		C.Test.TestTrue(TEXT("It never came closer than its Keep Distance (-1 m)"), Closest >= Mage->KeepDistance - 100.0f);
		C.Test.TestTrue(TEXT("It stands about its Keep Distance away (+1.5 m)"), Distance <= Mage->KeepDistance + 150.0f);
	});

	Scenario.Run();
	return true;
}

// --- AI skill use ---------------------------------------------------------------------------------

// skeleton_mage (fireball, heal, ...): casts its Fireball skill at the player in range, and Heal on
// itself once hurt below 60% Health.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioMobSkillUse, "Crossguard.Scenario.Monsters.SkeletonMageUsesSkills", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioMobSkillUse::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float SceneY = 9000.0f;
	FScenario Scenario(*this);

	Scenario.Step(TEXT("skeleton_mage (fireball, heal) and a player 10 m away"), [](FScenarioContext& C)
	{
		FName MobId;
		const FCrossguardMobRow* Mage = FindMob(C, TEXT("skeleton_mage"), [](const FCrossguardMobRow& Mob)
		{
			return Mob.Skills.Contains(FName(TEXT("fireball"))) && Mob.Skills.Contains(FName(TEXT("heal"))) && Mob.Kind == ECrossguardMobKind::Monster
				&& Mob.AggroRange > 1100.0f;
		}, MobId, TEXT("monster with Fireball and Heal"));
		if (Mage == nullptr)
		{
			return;
		}
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Mage"), MobId, FVector(0.0f, SceneY, 0.0f), 0.0f);
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(1000.0f, SceneY, 0.0f), 180.0f);
		if (!C.Require(Monster != nullptr && Player != nullptr, TEXT("mage and player spawned")))
		{
			return;
		}
		const TSet<FName> Granted = GrantedSkillIds(Monster);
		C.Require(Granted.Contains(FName(TEXT("fireball"))) && Granted.Contains(FName(TEXT("heal"))), TEXT("the mage was granted Fireball and Heal"));
		C.Test.TestFalse(TEXT("Heal is not used at full Health"), C.IsOnCooldown(Monster, TEXT("heal")));
	});

	Scenario.WaitUntil(TEXT("the mage casts its Fireball skill (on cooldown)"), 4.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Mage = C.Get<ACrossguardMonster>(TEXT("Mage"));
		TopUpHealth(C.FindActor(TEXT("Player")));
		return Mage != nullptr && C.IsOnCooldown(Mage, TEXT("fireball"));
	});

	Scenario.Step(TEXT("Aimed at the player; then the player hurts it below 60% Health"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Mage = C.Get<ACrossguardMonster>(TEXT("Mage"));
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Mage == nullptr || Player == nullptr)
		{
			C.Fail(TEXT("mage or player gone"));
			return;
		}
		C.Test.TestTrue(TEXT("Its skills aim at the player"), Mage->GetSkillAimTarget() == Player);
		C.Test.TestFalse(TEXT("No Heal while healthy"), C.IsOnCooldown(Mage, TEXT("heal")));
		// Magic Damage in small bites (Eldritch Shield absorbs Attack Damage only).
		const float Step = 0.1f * C.MaxHealth(Mage);
		for (int32 Bite = 0; Bite < 40 && C.Health(Mage) > 0.5f * C.MaxHealth(Mage); ++Bite)
		{
			DealDamage(Player, Mage, 0.0f, Step);
		}
		const float Fraction = C.Health(Mage) / FMath::Max(C.MaxHealth(Mage), 1.0f);
		C.Test.AddInfo(FString::Printf(TEXT("Mage at %.0f%% Health"), Fraction * 100.0f));
		if (!C.Require(!C.IsDead(Mage) && Fraction < 0.6f, TEXT("the mage is below 60% Health, alive")))
		{
			return;
		}
		C.SetNumber(TEXT("HurtHealth"), C.Health(Mage));
		C.ClearLog();
	});

	Scenario.WaitUntil(TEXT("the mage heals itself"), 3.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Mage = C.Get<ACrossguardMonster>(TEXT("Mage"));
		TopUpHealth(C.FindActor(TEXT("Player")));
		return Mage != nullptr && C.IsOnCooldown(Mage, TEXT("heal"));
	});

	Scenario.Step(TEXT("Healed by its own Heal"), [](FScenarioContext& C)
	{
		const ACrossguardMonster* Mage = C.Get<ACrossguardMonster>(TEXT("Mage"));
		if (Mage == nullptr)
		{
			return;
		}
		C.Test.TestTrue(TEXT("Its Health went up"), C.Health(Mage) > static_cast<float>(C.Number(TEXT("HurtHealth"))) + 0.01f);
		C.Test.TestTrue(TEXT("Log: it healed itself"), C.LogContains(FString::Printf(TEXT(": %s healed "), *Mage->GetName())));
		C.Test.TestTrue(TEXT("Heal was aimed at itself (its last skill's aim)"), Mage->GetSkillAimTarget() == Mage);
	});

	Scenario.Run();
	return true;
}

// --- Spawner respawn ------------------------------------------------------------------------------

// A spawner with Respawn Seconds replaces a dead monster that long after its death (noticed within
// the spawner's 2 s check); the body stays.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioSpawnerRespawn, "Crossguard.Scenario.Monsters.SpawnerRespawn", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioSpawnerRespawn::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioMonsterAI;

	static constexpr float RespawnSeconds = 4.0f;
	static const FVector SpawnerLocation(4000.0f, 0.0f, 50.0f);
	FScenario Scenario(*this);
	Scenario.TimeDilation(2.0f);

	Scenario.Step(TEXT("A spawner of one monster with 4 s respawns"), [](FScenarioContext& C)
	{
		FName MobId;
		if (FindMob(C, TEXT("skeleton_soldier"), [](const FCrossguardMobRow& Mob) { return Mob.Kind == ECrossguardMobKind::Monster; }, MobId,
				TEXT("non-boss")) == nullptr)
		{
			return;
		}
		C.SetRaid(true);
		ACrossguardMobSpawner* Spawner = C.SpawnActor<ACrossguardMobSpawner>(TEXT("Spawner"), SpawnerLocation, 0.0f, [MobId](ACrossguardMobSpawner& Spawned)
		{
			FCrossguardMobSpawnOption Option;
			Option.MobId = MobId;
			Option.Weight = 1;
			Spawned.Mobs = {Option};
			Spawned.CountMin = 1;
			Spawned.CountMax = 1;
			Spawned.Radius = 300.0f;
			Spawned.RespawnSeconds = RespawnSeconds;
		});
		C.Require(Spawner != nullptr, TEXT("the spawner spawned"));
	});

	Scenario.Wait(0.5f);

	Scenario.Step(TEXT("Its monster dies"), [](FScenarioContext& C)
	{
		const AActor* Spawner = C.FindActor(TEXT("Spawner"));
		if (!C.Require(Spawner != nullptr, TEXT("the spawner exists")))
		{
			return;
		}
		const TArray<ACrossguardMonster*> Monsters = SpawnedBy(C, Spawner);
		if (!C.Require(Monsters.Num() == 1, FString::Printf(TEXT("the spawner spawned 1 monster (spawned %d)"), Monsters.Num())))
		{
			return;
		}
		C.Remember(TEXT("First"), Monsters[0]);
		C.Kill(Monsters[0]);
		C.Require(C.IsDead(Monsters[0]), TEXT("the monster died"));
		C.SetNumber(TEXT("DeathTime"), C.Now());
	});

	Scenario.WaitUntil(TEXT("a replacement appears"), RespawnSeconds + SpawnerCheckSeconds + 3.0f, [](FScenarioContext& C)
	{
		const AActor* Spawner = C.FindActor(TEXT("Spawner"));
		if (Spawner == nullptr)
		{
			return false;
		}
		for (ACrossguardMonster* Monster : SpawnedBy(C, Spawner))
		{
			if (Monster->IsAlive())
			{
				C.SetNumber(TEXT("RespawnTime"), C.Now());
				C.Remember(TEXT("Second"), Monster);
				return true;
			}
		}
		return false;
	});

	Scenario.Step(TEXT("On time; the first one's body stays"), [](FScenarioContext& C)
	{
		const float Elapsed = static_cast<float>(C.Number(TEXT("RespawnTime")) - C.Number(TEXT("DeathTime")));
		C.Test.AddInfo(FString::Printf(TEXT("Replacement %.2f s after the death (Respawn Seconds %.1f, checked every %.1f s)"), Elapsed, RespawnSeconds,
			SpawnerCheckSeconds));
		C.Test.TestTrue(TEXT("Not before Respawn Seconds"), Elapsed >= RespawnSeconds - 0.05f);
		C.Test.TestTrue(TEXT("Within Respawn Seconds + the spawner's check period"), Elapsed <= RespawnSeconds + SpawnerCheckSeconds + 0.5f);

		const ACrossguardMonster* First = Cast<ACrossguardMonster>(C.FindActor(TEXT("First")));
		const ACrossguardMonster* Second = C.Get<ACrossguardMonster>(TEXT("Second"));
		if (C.Test.TestNotNull(TEXT("The dead monster is still there"), First))
		{
			C.Test.TestTrue(TEXT("...and dead"), C.IsDead(First));
			C.Test.TestNotNull(TEXT("Its body stays"), C.FindNearest<ACrossguardLootContainer>(First->GetActorLocation(), BodySearchRadius));
		}
		if (Second != nullptr && First != nullptr)
		{
			C.Test.TestTrue(TEXT("The replacement is a new monster of the same mob"), Second != First && Second->MobId == First->MobId);
			C.Test.TestEqual(TEXT("Replacement at full Health"), C.Health(Second), C.MaxHealth(Second), 0.01f);
		}
		const AActor* Spawner = C.FindActor(TEXT("Spawner"));
		if (Spawner != nullptr)
		{
			C.Test.TestEqual(TEXT("The spawner has spawned 2 monsters in all (one dead, one alive)"), SpawnedBy(C, Spawner).Num(), 2);
		}
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
