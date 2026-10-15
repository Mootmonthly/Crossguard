// Spawns AI characters in a transient game world (no level, no game mode, nothing ticks): fixed
// Health, damage through the native damage effect, death at 0 Health, skill grants and teams.
//
// Written without an engine to compile against: the world set-up copies the engine's own
// GameplayAbilities tests (UWorld::CreateWorld + a Game world context + InitializeActorsForPlay +
// BeginPlay), which run in editor automation, including -nullrhi. If this test crashes or the
// possession checks fail on a new engine version, the set-up in CrossguardTestUtils.h is the first
// suspect; the other Crossguard tests do not need a world.

#include "Tests/CrossguardTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "Character/CrossguardMonster.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/Controller.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "Skills/CrossguardNativeSkills.h"
#include "Templates/Function.h"

namespace CrossguardWorldTestHelpers
{
	constexpr float MonsterHealth = 50.0f;

	// Spawns T, lets Configure set its properties before PostInitializeComponents (which spawns the
	// AI controller and possesses: PossessedBy applies FixedHealth and grants SkillIds), then finishes
	// spawning.
	template <typename T>
	T* SpawnConfigured(UWorld& World, const FVector& Location, TFunctionRef<void(T&)> Configure)
	{
		const FTransform Transform(FRotator::ZeroRotator, Location);
		T* Actor = World.SpawnActorDeferred<T>(T::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Actor == nullptr)
		{
			return nullptr;
		}
		Configure(*Actor);
		Actor->FinishSpawning(Transform);
		return Actor;
	}

	float Health(const ACrossguardCharacter& Character)
	{
		const UCrossguardAbilitySystemComponent* ASC = Character.GetCrossguardAbilitySystemComponent();
		return ASC != nullptr ? ASC->GetNumericAttribute(UCrossguardAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	float MaxHealth(const ACrossguardCharacter& Character)
	{
		const UCrossguardAbilitySystemComponent* ASC = Character.GetCrossguardAbilitySystemComponent();
		return ASC != nullptr ? ASC->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute()) : -1.0f;
	}

	bool IsMarkedDead(const ACrossguardCharacter& Character)
	{
		const UCrossguardAbilitySystemComponent* ASC = Character.GetCrossguardAbilitySystemComponent();
		return ASC != nullptr && ASC->HasMatchingGameplayTag(CrossguardTags::State_Dead);
	}

	// Source hits Target with the native damage effect for AttackDamage base Attack Damage, as a
	// weapon or skill would (SetByCaller.Damage.Attack).
	bool ApplyAttackDamage(ACrossguardCharacter& Source, ACrossguardCharacter& Target, float AttackDamage)
	{
		UAbilitySystemComponent* SourceASC = Source.GetCrossguardAbilitySystemComponent();
		UAbilitySystemComponent* TargetASC = Target.GetCrossguardAbilitySystemComponent();
		if (SourceASC == nullptr || TargetASC == nullptr)
		{
			return false;
		}
		const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, SourceASC->MakeEffectContext());
		if (!Spec.IsValid())
		{
			return false;
		}
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, AttackDamage);
		SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		return true;
	}

	bool HasAbilityOfClass(const ACrossguardCharacter& Character, const UClass* AbilityClass)
	{
		UCrossguardAbilitySystemComponent* ASC = Character.GetCrossguardAbilitySystemComponent();
		if (ASC == nullptr || AbilityClass == nullptr)
		{
			return false;
		}
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability != nullptr && Spec.Ability->GetClass() == AbilityClass)
			{
				return true;
			}
		}
		return false;
	}

	// A registered native skill id: Preferred if registered, else the first one that is not Avoid.
	FName PickNativeSkill(FName Preferred, FName Avoid)
	{
		if (CrossguardNativeSkills::Find(Preferred).Get() != nullptr)
		{
			return Preferred;
		}
		for (const TPair<FName, TSubclassOf<UCrossguardGameplayAbility>>& Pair : CrossguardNativeSkills::All())
		{
			if (Pair.Key != Avoid)
			{
				return Pair.Key;
			}
		}
		return NAME_None;
	}
}

// Engine and game code may log warnings in a level-less world (no navigation, no physics asset on
// the placeholder body). They show up as warnings of this test (errors, if the project elevates log
// warnings); the log lines say where they came from.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardWorldAICharacterTest, "Crossguard.World.AICharacters", CROSSGUARD_TEST_FLAGS)

bool FCrossguardWorldAICharacterTest::RunTest(const FString& Parameters)
{
	using namespace CrossguardWorldTestHelpers;

	const CrossguardTests::FTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	if (!TestNotNull(TEXT("Transient game world"), World))
	{
		return false;
	}

	const FName FirstSkill = PickNativeSkill(TEXT("fireball"), NAME_None);
	const FName SecondSkill = PickNativeSkill(TEXT("heal"), FirstSkill);
	UClass* FirstSkillClass = CrossguardNativeSkills::Find(FirstSkill).Get();
	UClass* SecondSkillClass = CrossguardNativeSkills::Find(SecondSkill).Get();
	if (!TestNotNull(TEXT("A native skill to grant"), FirstSkillClass) || !TestNotNull(TEXT("A second native skill to grant"), SecondSkillClass)
		|| !TestTrue(TEXT("Two different native skills"), FirstSkillClass != SecondSkillClass))
	{
		return false;
	}

	// --- A monster with explicit stats -------------------------------------------------------------
	ACrossguardMonster* Monster = SpawnConfigured<ACrossguardMonster>(*World, FVector(0.0f, 0.0f, 200.0f), [](ACrossguardMonster& Spawned)
	{
		Spawned.FixedHealth = MonsterHealth;
		Spawned.AttackDamage = 5.0f;
		// No game mode here to roll loot with anyway.
		Spawned.LootTableId = NAME_None;
	});
	if (!TestNotNull(TEXT("Spawned ACrossguardMonster"), Monster))
	{
		return false;
	}
	if (Monster->GetController() == nullptr)
	{
		// Auto-possession should have happened in PostInitializeComponents; do it by hand so the rest
		// of the test still runs, but report it.
		AddError(TEXT("The monster was not auto-possessed by its AI controller on spawn (AutoPossessAI); possessing by hand"));
		Monster->SpawnDefaultController();
	}
	if (!TestNotNull(TEXT("Monster's ability system component"), Monster->GetCrossguardAbilitySystemComponent())
		|| !TestNotNull(TEXT("Monster has a controller"), Monster->GetController()))
	{
		return false;
	}
	TestEqual(TEXT("Monster Max Health is its FixedHealth"), MaxHealth(*Monster), MonsterHealth, 0.01f);
	TestEqual(TEXT("Monster starts at full Health"), Health(*Monster), MonsterHealth, 0.01f);
	TestTrue(TEXT("Monster is alive"), Monster->IsAlive());

	// --- A minion granted a native skill -----------------------------------------------------------
	ACrossguardMinion* Minion = SpawnConfigured<ACrossguardMinion>(*World, FVector(400.0f, 0.0f, 200.0f), [FirstSkill](ACrossguardMinion& Spawned)
	{
		Spawned.FixedHealth = 30.0f;
		// No catalog: native skills only (CrossguardNativeSkills).
		Spawned.SkillCatalog = nullptr;
		Spawned.SkillIds = {FirstSkill};
	});
	if (!TestNotNull(TEXT("Spawned ACrossguardMinion"), Minion))
	{
		return false;
	}
	if (Minion->GetController() == nullptr)
	{
		AddError(TEXT("The minion was not auto-possessed by its AI controller on spawn; possessing by hand"));
		Minion->SpawnDefaultController();
	}
	if (!TestNotNull(TEXT("Minion's ability system component"), Minion->GetCrossguardAbilitySystemComponent()))
	{
		return false;
	}
	TestEqual(TEXT("Minion Max Health is its FixedHealth"), MaxHealth(*Minion), 30.0f, 0.01f);
	TestTrue(*FString::Printf(TEXT("Possession granted SkillIds: '%s' (%s)"), *FirstSkill.ToString(), *FirstSkillClass->GetName()),
		HasAbilityOfClass(*Minion, FirstSkillClass));

	// GrantSkills replaces the granted skills with SkillIds.
	Minion->SkillIds = {SecondSkill};
	Minion->GrantSkills();
	TestTrue(*FString::Printf(TEXT("GrantSkills granted '%s'"), *SecondSkill.ToString()), HasAbilityOfClass(*Minion, SecondSkillClass));
	TestFalse(*FString::Printf(TEXT("GrantSkills removed '%s'"), *FirstSkill.ToString()), HasAbilityOfClass(*Minion, FirstSkillClass));

	// --- Damage and death --------------------------------------------------------------------------
	TestTrue(TEXT("Damage spec applied"), ApplyAttackDamage(*Minion, *Monster, 10.0f));
	const float AfterHit = Health(*Monster);
	AddInfo(FString::Printf(TEXT("10 base Attack Damage took the monster from %.2f to %.2f Health"), MonsterHealth, AfterHit));
	TestTrue(TEXT("A hit lowers the monster's Health"), AfterHit < MonsterHealth);
	TestTrue(TEXT("A small hit does not kill the monster"), AfterHit > 0.0f);
	TestFalse(TEXT("Not dead after a small hit"), IsMarkedDead(*Monster));

	TestTrue(TEXT("Lethal damage spec applied"), ApplyAttackDamage(*Minion, *Monster, 100000.0f));
	TestEqual(TEXT("Lethal damage leaves the monster at 0 Health"), Health(*Monster), 0.0f, 0.001f);
	TestTrue(TEXT("The monster has State.Dead at 0 Health"), IsMarkedDead(*Monster));
	TestFalse(TEXT("IsAlive() is false at 0 Health"), Monster->IsAlive());

	ApplyAttackDamage(*Minion, *Monster, 10.0f);
	TestEqual(TEXT("Hitting a dead monster keeps it at 0 Health"), Health(*Monster), 0.0f, 0.001f);

	// --- Teams with spawned actors -----------------------------------------------------------------
	ACrossguardMonster* OtherMonster = SpawnConfigured<ACrossguardMonster>(*World, FVector(0.0f, 600.0f, 200.0f), [](ACrossguardMonster& Spawned)
	{
		Spawned.LootTableId = NAME_None;
	});
	ACrossguardMinion* MonsterMinion = SpawnConfigured<ACrossguardMinion>(*World, FVector(0.0f, 900.0f, 200.0f), [OtherMonster](ACrossguardMinion& Spawned)
	{
		Spawned.SetSummoner(OtherMonster);
	});
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACrossguardCharacter* Playerless = World->SpawnActor<ACrossguardCharacter>(ACrossguardCharacter::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(-600.0f, 0.0f, 200.0f)), Params);
	if (TestNotNull(TEXT("Second monster"), OtherMonster) && TestNotNull(TEXT("Monster's minion"), MonsterMinion)
		&& TestNotNull(TEXT("Player-less character"), Playerless))
	{
		TestTrue(TEXT("Two monsters are allies"), UCrossguardTeamLibrary::AreAllies(Monster, OtherMonster));
		TestEqual(TEXT("A monster's minion is on MonsterTeamId"), UCrossguardTeamLibrary::GetTeamId(MonsterMinion),
			UCrossguardTeamLibrary::MonsterTeamId);
		TestTrue(TEXT("A monster's minion is every monster's ally"), UCrossguardTeamLibrary::AreAllies(MonsterMinion, Monster));
		TestFalse(TEXT("A monster is not an ally of a player-less character"), UCrossguardTeamLibrary::AreAllies(Monster, Playerless));
		TestFalse(TEXT("A player-less character is not an ally of a monster"), UCrossguardTeamLibrary::AreAllies(Playerless, OtherMonster));
		TestFalse(TEXT("A summoner-less minion is not a monster's ally"), UCrossguardTeamLibrary::AreAllies(Minion, Monster));
		TestEqual(TEXT("A player-less character has no team"), UCrossguardTeamLibrary::GetTeamId(Playerless), -1);
	}

	// --- A minion dies too -------------------------------------------------------------------------
	if (OtherMonster != nullptr)
	{
		ApplyAttackDamage(*OtherMonster, *Minion, 100000.0f);
		TestEqual(TEXT("Lethal damage leaves the minion at 0 Health"), Health(*Minion), 0.0f, 0.001f);
		TestTrue(TEXT("The minion has State.Dead at 0 Health"), IsMarkedDead(*Minion));
	}

	// FTestWorld destroys the world (and every actor in it) here.
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
