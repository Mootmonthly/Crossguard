// Native skill registry (CrossguardNativeSkills): ids, classes and the catalog fallback. No world,
// no assets.

#include "Tests/CrossguardTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "Skills/CrossguardNativeSkills.h"
#include "Skills/CrossguardSkillCatalog.h"
#include "UObject/UObjectHash.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNativeSkillRegistryTest, "Crossguard.Skills.NativeRegistry", CROSSGUARD_TEST_FLAGS)

bool FCrossguardNativeSkillRegistryTest::RunTest(const FString& Parameters)
{
	const TMap<FName, TSubclassOf<UCrossguardGameplayAbility>>& All = CrossguardNativeSkills::All();
	TestTrue(TEXT("CrossguardNativeSkills::All() lists skills"), All.Num() > 0);

	// A catalog with no table and no AbilityClasses: FindAbilityClass falls back to the native registry.
	UCrossguardSkillCatalog* EmptyCatalog = NewObject<UCrossguardSkillCatalog>(GetTransientPackage(), NAME_None, RF_Transient);

	for (const TPair<FName, TSubclassOf<UCrossguardGameplayAbility>>& Pair : All)
	{
		const FString Id = Pair.Key.ToString();
		if (!TestFalse(*FString::Printf(TEXT("Registered id '%s' is not None"), *Id), Pair.Key.IsNone()))
		{
			continue;
		}
		UClass* Class = Pair.Value.Get();
		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a class"), *Id), Class))
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("'%s' -> %s is not abstract"), *Id, *Class->GetName()), Class->HasAnyClassFlags(CLASS_Abstract));
		const UCrossguardGameplayAbility* Ability = Class->GetDefaultObject<UCrossguardGameplayAbility>();
		if (!TestNotNull(*FString::Printf(TEXT("'%s' -> %s has a default object"), *Id, *Class->GetName()), Ability))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s's SkillId '%s' matches its registry id '%s'"), *Class->GetName(), *Ability->SkillId.ToString(), *Id),
			Ability->SkillId == Pair.Key);
		TestTrue(*FString::Printf(TEXT("CrossguardNativeSkills::Find('%s') returns %s"), *Id, *Class->GetName()),
			CrossguardNativeSkills::Find(Pair.Key).Get() == Class);
		TestTrue(*FString::Printf(TEXT("An empty catalog's FindAbilityClass('%s') falls back to %s"), *Id, *Class->GetName()),
			EmptyCatalog->FindAbilityClass(Pair.Key).Get() == Class);
	}

	TestTrue(TEXT("Find(None) returns no class"), CrossguardNativeSkills::Find(NAME_None).Get() == nullptr);
	TestTrue(TEXT("Find of an unknown id returns no class"), CrossguardNativeSkills::Find(TEXT("crossguard_test_no_such_skill")).Get() == nullptr);

	// The registry is keyed by SkillId, so two classes with the same id would silently hide one, and a
	// new C++ skill nobody registered would not work without editor setup. Every native, concrete
	// ability class with a SkillId must be the registered class for its id.
	TArray<UClass*> Derived;
	GetDerivedClasses(UCrossguardGameplayAbility::StaticClass(), Derived, true);
	int32 NativeWithId = 0;
	for (UClass* Class : Derived)
	{
		if (Class == nullptr || !Class->HasAnyClassFlags(CLASS_Native) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		const UCrossguardGameplayAbility* Ability = Class->GetDefaultObject<UCrossguardGameplayAbility>();
		if (Ability == nullptr || Ability->SkillId.IsNone())
		{
			continue;
		}
		++NativeWithId;
		const TSubclassOf<UCrossguardGameplayAbility>* Registered = All.Find(Ability->SkillId);
		if (Registered == nullptr)
		{
			AddError(FString::Printf(TEXT("%s has SkillId '%s' but is not registered in CrossguardNativeSkills::All() (CrossguardNativeSkills.cpp)"),
				*Class->GetName(), *Ability->SkillId.ToString()));
		}
		else if (Registered->Get() != Class)
		{
			AddError(FString::Printf(TEXT("%s and %s both have SkillId '%s'; the registry keeps only %s"), *Class->GetName(),
				*GetNameSafe(Registered->Get()), *Ability->SkillId.ToString(), *GetNameSafe(Registered->Get())));
		}
	}
	AddInfo(FString::Printf(TEXT("%d registered native skills, %d native ability classes with a SkillId"), All.Num(), NativeWithId));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
