// Scenario test framework: see CrossguardScenario.h and docs/editor-tests.md.
//
// Written without an engine to compile against. The editor calls it relies on are listed in
// docs/editor-tests.md ("Scenario framework: engine calls to check first"); if a scenario hangs or
// PIE does not start, look at FScenarioRunner below first.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AIController.h"
#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "AbilitySystem/CrossguardAttributeSet.h"
#include "AbilitySystem/CrossguardEffects.h"
#include "AbilitySystem/CrossguardGameplayAbility.h"
#include "AbilitySystem/CrossguardGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Character/CrossguardAICharacter.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMonster.h"
#include "Components/StaticMeshComponent.h"
#include "CrossguardRules/Combat.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/OnlineReplStructs.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayAbilitySpec.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Skills/CrossguardLoadoutComponent.h"
#include "Skills/CrossguardNativeSkills.h"
#include "Skills/CrossguardSkillAbility.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace CrossguardScenarioLocal
{
	// The test map (cm). The floor's top is at Z = 0.
	constexpr float FloorSize = 20000.0f;
	const FVector LocalPlayerStart(-3000.0f, -3000.0f, 100.0f);
	// Real seconds.
	constexpr double PIEStartTimeout = 60.0;
	constexpr double PIEEndTimeout = 30.0;
	// Gap between a capsule's bottom and the floor when a character is spawned (cm).
	constexpr float SpawnClearance = 2.0f;
	// GrantSkills raises a character's level up to this when a loadout needs more Skill Capacity
	// (the default MaxLevel in CrossguardRules/Config.h).
	constexpr int32 MaxTestLevel = 15;

	const TCHAR* const PreferredGameMode = TEXT("BP_CrossguardGameMode");
	const TCHAR* const PreferredCharacter = TEXT("BP_CrossguardCharacter");

	UAbilitySystemComponent* FindASC(const AActor* Actor)
	{
		return Actor != nullptr ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor) : nullptr;
	}

	bool IsSkeletonName(const FString& Name)
	{
		return Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_"))
			|| Name.StartsWith(TEXT("HOTRELOADED_"));
	}

	// A granted, non-passive skill's spec by skill id; null if none.
	const FGameplayAbilitySpec* FindSkillSpec(UAbilitySystemComponent* ASC, FName SkillId)
	{
		if (ASC == nullptr)
		{
			return nullptr;
		}
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const UCrossguardGameplayAbility* Ability = Cast<UCrossguardGameplayAbility>(Spec.Ability);
			if (Ability != nullptr && Ability->SkillId == SkillId && Ability->SkillCategory != ECrossguardSkillCategory::Passive)
			{
				return &Spec;
			}
		}
		return nullptr;
	}

	// Blueprint classes derived from Base (not Base, not native, not a subclass of Excluded), by class
	// path. From the asset registry (also unloaded Blueprints) and from classes already in memory.
	TArray<FString> FindBlueprintClassPaths(UClass* Base, UClass* Excluded)
	{
		TSet<FString> Paths;

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		if (Registry.IsLoadingAssets())
		{
			// Headless runs can start the tests before the initial scan has finished.
			Registry.SearchAllAssets(true);
		}
		TSet<FTopLevelAssetPath> Derived;
		Registry.GetDerivedClassNames({Base->GetClassPathName()}, TSet<FTopLevelAssetPath>(), Derived);
		for (const FTopLevelAssetPath& Path : Derived)
		{
			const FString Package = Path.GetPackageName().ToString();
			if (Path == Base->GetClassPathName() || Package.StartsWith(TEXT("/Script/")) || IsSkeletonName(Path.GetAssetName().ToString()))
			{
				continue;
			}
			Paths.Add(Path.ToString());
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class == Base || !Class->IsChildOf(Base) || (Excluded != nullptr && Class->IsChildOf(Excluded))
				|| Class->HasAnyClassFlags(CLASS_Native | CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				|| Class->GetPackage() == GetTransientPackage() || IsSkeletonName(Class->GetName()))
			{
				continue;
			}
			Paths.Add(Class->GetPathName());
		}

		TArray<FString> Sorted = Paths.Array();
		Sorted.Sort();
		return Sorted;
	}

	// The project's Blueprint of Base: the one whose asset is named Preferred, else the only one, else
	// the first by path (with a warning). Null, with an error naming the missing asset, if none loads.
	UClass* FindBlueprintClass(FAutomationTestBase& Test, UClass* Base, UClass* Excluded, const TCHAR* Preferred, const TCHAR* SetupHint)
	{
		TArray<FString> Candidates = FindBlueprintClassPaths(Base, Excluded);
		const FString PreferredClassName = FString(Preferred) + TEXT("_C");
		Candidates.StableSort([&PreferredClassName](const FString& A, const FString& B)
		{
			const bool bA = A.EndsWith(TEXT(".") + PreferredClassName);
			const bool bB = B.EndsWith(TEXT(".") + PreferredClassName);
			return bA && !bB;
		});

		for (const FString& Path : Candidates)
		{
			UClass* Class = StaticLoadClass(Base, nullptr, *Path);
			if (Class == nullptr || Class->HasAnyClassFlags(CLASS_Abstract) || (Excluded != nullptr && Class->IsChildOf(Excluded)))
			{
				continue;
			}
			if (Candidates.Num() > 1 && !Path.EndsWith(TEXT(".") + PreferredClassName))
			{
				Test.AddWarning(FString::Printf(TEXT("No Blueprint named %s; using %s out of %s"), Preferred, *Path,
					*FString::Join(Candidates, TEXT(", "))));
			}
			return Class;
		}
		Test.AddError(FString::Printf(TEXT("Scenario set-up: no Blueprint of %s found (expected one named %s under /Game). %s"),
			*Base->GetName(), Preferred, SetupHint));
		return nullptr;
	}
}

namespace CrossguardScenario
{
	// --- Log capture ------------------------------------------------------------------------------

	// Registered with GLog while a scenario's PIE session runs.
	class FScenarioLogCapture : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Message == nullptr)
			{
				return;
			}
			FScopeLock Lock(&Mutex);
			Lines.Add(FString::Printf(TEXT("%s: %s"), *Category.ToString(), Message));
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }

		TArray<FString> GetLines() const
		{
			FScopeLock Lock(&Mutex);
			return Lines;
		}

		void Clear()
		{
			FScopeLock Lock(&Mutex);
			Lines.Reset();
		}

	private:
		mutable FCriticalSection Mutex;
		TArray<FString> Lines;
	};

	// --- Context ----------------------------------------------------------------------------------

	FScenarioContext::FScenarioContext(FAutomationTestBase& InTest)
		: Test(InTest)
	{
	}

	FScenarioContext::~FScenarioContext() = default;

	ACrossguardCharacter* FScenarioContext::LocalPlayer() const
	{
		UWorld* PlayWorld = World();
		APlayerController* First = PlayWorld != nullptr ? PlayWorld->GetFirstPlayerController() : nullptr;
		return First != nullptr ? Cast<ACrossguardCharacter>(First->GetPawn()) : nullptr;
	}

	float FScenarioContext::Now() const
	{
		const UWorld* PlayWorld = World();
		return PlayWorld != nullptr ? PlayWorld->GetTimeSeconds() : 0.0f;
	}

	void FScenarioContext::Fail(const FString& Why)
	{
		Test.AddError(Why);
		bStopped = true;
	}

	bool FScenarioContext::Require(bool bCondition, const FString& What)
	{
		if (!bCondition)
		{
			Fail(FString::Printf(TEXT("Required: %s"), *What));
		}
		return bCondition;
	}

	void FScenarioContext::Skip(const FString& Why)
	{
		Test.AddWarning(FString::Printf(TEXT("Scenario skipped: %s"), *Why));
		bStopped = true;
	}

	void FScenarioContext::Remember(FName Name, AActor* Actor)
	{
		Actors.Add(Name, Actor);
	}

	AActor* FScenarioContext::FindActor(FName Name) const
	{
		const TWeakObjectPtr<AActor>* Found = Actors.Find(Name);
		return Found != nullptr ? Found->Get() : nullptr;
	}

	double FScenarioContext::Number(FName Name, double Default) const
	{
		const double* Found = Numbers.Find(Name);
		return Found != nullptr ? *Found : Default;
	}

	// --- Spawning ---------------------------------------------------------------------------------

	AActor* FScenarioContext::SpawnActorInternal(FName Name, UClass* Class, const FVector& Location, float Yaw, TFunctionRef<void(AActor&)> Configure)
	{
		UWorld* PlayWorld = World();
		if (PlayWorld == nullptr || Class == nullptr)
		{
			Fail(FString::Printf(TEXT("Cannot spawn '%s': no PIE world or no class"), *Name.ToString()));
			return nullptr;
		}
		const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Location);
		AActor* Actor = PlayWorld->SpawnActorDeferred<AActor>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Actor == nullptr)
		{
			Fail(FString::Printf(TEXT("Could not spawn '%s' (%s)"), *Name.ToString(), *Class->GetName()));
			return nullptr;
		}
		Configure(*Actor);
		Actor->FinishSpawning(Transform);
		if (!IsValid(Actor))
		{
			Fail(FString::Printf(TEXT("'%s' (%s) was destroyed while spawning"), *Name.ToString(), *Class->GetName()));
			return nullptr;
		}
		Remember(Name, Actor);
		return Actor;
	}

	ACrossguardCharacter* FScenarioContext::SpawnCharacter(FName Name, UClass* Class, const FVector& FloorPoint, float Yaw, bool bAIController,
		TFunctionRef<void(ACrossguardCharacter&)> Configure)
	{
		UWorld* PlayWorld = World();
		if (PlayWorld == nullptr || Class == nullptr || !Class->IsChildOf(ACrossguardCharacter::StaticClass()))
		{
			Fail(FString::Printf(TEXT("Cannot spawn '%s': no PIE world or no character class"), *Name.ToString()));
			return nullptr;
		}
		const float HalfHeight = Class->GetDefaultObject<ACrossguardCharacter>()->GetDefaultHalfHeight();
		const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), FloorPoint + FVector(0.0f, 0.0f, HalfHeight + CrossguardScenarioLocal::SpawnClearance));
		ACrossguardCharacter* Character = PlayWorld->SpawnActorDeferred<ACrossguardCharacter>(Class, Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Character == nullptr)
		{
			Fail(FString::Printf(TEXT("Could not spawn character '%s' (%s)"), *Name.ToString(), *Class->GetName()));
			return nullptr;
		}

		if (!bKeepDebugLoadout)
		{
			Character->DebugMainHand = FCrossguardWeaponItem();
			Character->DebugOffHand = FCrossguardWeaponItem();
			Character->DebugArmor.Reset();
			Character->DebugSkills.Reset();
			Character->DebugInventory.Reset();
			Character->bRollDebugAffixes = false;
			Character->DebugReviveDelay = 0.0f;
			Character->bDebugLootTakesAll = false;
			Character->bDebugHud = false;
		}
		// Possession is decided here, not by the Blueprint's Auto Possess settings.
		Character->AutoPossessPlayer = EAutoReceiveInput::Disabled;
		Character->AutoPossessAI = bAIController ? EAutoPossessAI::Spawned : EAutoPossessAI::Disabled;
		if (bAIController && Character->AIControllerClass == nullptr)
		{
			Character->AIControllerClass = AAIController::StaticClass();
		}
		Configure(*Character);
		Character->FinishSpawning(Transform);
		if (!IsValid(Character))
		{
			Fail(FString::Printf(TEXT("Character '%s' was destroyed while spawning"), *Name.ToString()));
			return nullptr;
		}
		if (bAIController && Character->GetController() == nullptr)
		{
			Character->SpawnDefaultController();
		}
		Remember(Name, Character);
		return Character;
	}

	ACrossguardCharacter* FScenarioContext::SpawnPlayer(FName Name, const FVector& FloorPoint, float Yaw, const FString& Party)
	{
		UWorld* PlayWorld = World();
		ACrossguardGameMode* Mode = GameMode();
		if (PlayWorld == nullptr || Mode == nullptr)
		{
			Fail(TEXT("SpawnPlayer: no PIE world or game mode"));
			return nullptr;
		}

		// 1. The controller. Its PlayerState is created in PostInitializeComponents (game mode's class).
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		UClass* ControllerClass = Mode->PlayerControllerClass != nullptr ? Mode->PlayerControllerClass.Get() : APlayerController::StaticClass();
		APlayerController* Controller = PlayWorld->SpawnActor<APlayerController>(ControllerClass, FloorPoint, FRotator(0.0f, Yaw, 0.0f), Params);
		if (Controller == nullptr || Controller->PlayerState == nullptr)
		{
			Fail(FString::Printf(TEXT("SpawnPlayer '%s': the %s or its PlayerState did not spawn"), *Name.ToString(), *ControllerClass->GetName()));
			return nullptr;
		}

		// 2. Login set-up: name, team (Party), start spot.
		FString Options = FString::Printf(TEXT("?Name=%s"), *Name.ToString());
		if (!Party.IsEmpty())
		{
			Options += FString::Printf(TEXT("?%s=%s"), ACrossguardGameMode::PartyOption, *Party);
		}
		const FString LoginError = Mode->InitNewPlayer(Controller, FUniqueNetIdRepl(), Options);
		if (!LoginError.IsEmpty())
		{
			Test.AddWarning(FString::Printf(TEXT("SpawnPlayer '%s': InitNewPlayer said: %s"), *Name.ToString(), *LoginError));
		}

		// 3. The pawn, spawned here (debug loadout cleared, at FloorPoint) and handed to the controller;
		// RestartPlayer keeps an existing pawn and possesses it.
		ACrossguardCharacter* Character = SpawnCharacter(Name, CharacterClass(), FloorPoint, Yaw, false, [](ACrossguardCharacter&) {});
		if (Character == nullptr)
		{
			Controller->Destroy();
			return nullptr;
		}
		Controller->SetPawn(Character);
		// What PostLogin ends with: RestartPlayer, and the raid begins for the player on a raid map.
		Mode->HandleStartingNewPlayer_Implementation(Controller);
		if (Character->GetController() != Controller)
		{
			Mode->RestartPlayer(Controller);
		}
		if (Character->GetController() != Controller)
		{
			Controller->Possess(Character);
		}
		if (!Require(Character->GetController() == Controller, FString::Printf(TEXT("SpawnPlayer '%s' is possessed by its PlayerController"), *Name.ToString())))
		{
			return nullptr;
		}
		Controller->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
		Test.TestTrue(*FString::Printf(TEXT("SpawnPlayer '%s' is player-controlled"), *Name.ToString()), Character->IsPlayerControlled());
		Remember(FName(*(Name.ToString() + TEXT(".Controller"))), Controller);
		return Character;
	}

	ACrossguardCharacter* FScenarioContext::SpawnDummy(FName Name, const FVector& FloorPoint, float Yaw)
	{
		ACrossguardCharacter* Character = SpawnCharacter(Name, CharacterClass(), FloorPoint, Yaw, true, [](ACrossguardCharacter&) {});
		if (Character != nullptr)
		{
			Require(Character->GetController() != nullptr, FString::Printf(TEXT("SpawnDummy '%s' has an AI controller"), *Name.ToString()));
			Test.TestTrue(*FString::Printf(TEXT("SpawnDummy '%s' has its ability system set up (Max Health > 0)"), *Name.ToString()),
				MaxHealth(Character) > 0.0f);
		}
		return Character;
	}

	ACrossguardMonster* FScenarioContext::SpawnMonster(FName Name, FName MobId, const FVector& FloorPoint, float Yaw)
	{
		ACrossguardCharacter* Character = SpawnCharacter(Name, ACrossguardMonster::StaticClass(), FloorPoint, Yaw, true,
			[MobId](ACrossguardCharacter& Spawned) { CastChecked<ACrossguardMonster>(&Spawned)->MobId = MobId; });
		return Cast<ACrossguardMonster>(Character);
	}

	void FScenarioContext::Place(ACrossguardCharacter* Character, const FVector& FloorPoint, float Yaw)
	{
		if (Character == nullptr)
		{
			Fail(TEXT("Place: no character"));
			return;
		}
		const FVector Location = FloorPoint + FVector(0.0f, 0.0f, Character->GetDefaultHalfHeight() + CrossguardScenarioLocal::SpawnClearance);
		Character->SetActorLocationAndRotation(Location, FRotator(0.0f, Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* Controller = Character->GetController())
		{
			Controller->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
		}
	}

	// --- Gear, items, skills ----------------------------------------------------------------------

	bool FScenarioContext::Equip(ACrossguardCharacter* Character, FName WeaponId, ECrossguardEquipmentSlot Slot, ECrossguardRarity Rarity)
	{
		UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
		if (Equipment == nullptr)
		{
			Fail(FString::Printf(TEXT("Equip %s: no character or equipment component"), *WeaponId.ToString()));
			return false;
		}
		if (Equipment->WeaponTable == nullptr)
		{
			Fail(FString::Printf(TEXT("Equip %s: %s's EquipmentComponent has no Weapon Table (set it to DT_Weapons in the character Blueprint)"),
				*WeaponId.ToString(), *Character->GetClass()->GetName()));
			return false;
		}
		FCrossguardWeaponItem Item;
		Item.WeaponId = WeaponId;
		Item.Rarity = Rarity;
		ECrossguardEquipError Error = Equipment->Equip(Slot, Item);
		if (Error == ECrossguardEquipError::TwoHandedWithOffHand && Slot == ECrossguardEquipmentSlot::MainHand)
		{
			Equipment->Equip(ECrossguardEquipmentSlot::OffHand, FCrossguardWeaponItem());
			Error = Equipment->Equip(Slot, Item);
		}
		if (Error != ECrossguardEquipError::None)
		{
			Fail(FString::Printf(TEXT("Equip %s in %s on %s: %s"), *WeaponId.ToString(),
				Slot == ECrossguardEquipmentSlot::MainHand ? TEXT("the main hand") : TEXT("the off hand"), *Character->GetName(),
				*StaticEnum<ECrossguardEquipError>()->GetNameStringByValue(static_cast<int64>(Error))));
			return false;
		}
		return true;
	}

	bool FScenarioContext::Unequip(ACrossguardCharacter* Character, ECrossguardEquipmentSlot Slot)
	{
		UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
		return Equipment != nullptr && Equipment->Equip(Slot, FCrossguardWeaponItem()) == ECrossguardEquipError::None;
	}

	int32 FScenarioContext::GiveItem(ACrossguardCharacter* Character, FName ItemId, int32 Count, ECrossguardRarity Rarity)
	{
		UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
		if (Inventory == nullptr || Inventory->ItemTable == nullptr)
		{
			Fail(FString::Printf(TEXT("GiveItem %s: no inventory, or its Item Table is unset (set it to DT_Items in the character Blueprint)"),
				*ItemId.ToString()));
			return Count;
		}
		const FCrossguardItemRow* Row = Inventory->ItemTable->FindRow<FCrossguardItemRow>(ItemId, TEXT("Scenario"), false);
		if (Row == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("GiveItem: %s is not a row of %s"), *ItemId.ToString(), *Inventory->ItemTable->GetName()));
			return Count;
		}
		return GiveItemOfCategory(Character, Row->Category, ItemId, Count, Rarity);
	}

	int32 FScenarioContext::GiveItemOfCategory(ACrossguardCharacter* Character, ECrossguardItemCategory Category, FName ItemId, int32 Count,
		ECrossguardRarity Rarity)
	{
		UCrossguardInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
		if (Inventory == nullptr)
		{
			Fail(FString::Printf(TEXT("GiveItem %s: no character or inventory"), *ItemId.ToString()));
			return Count;
		}
		FCrossguardInventoryItem Item;
		Item.Category = Category;
		Item.ItemId = ItemId;
		Item.Rarity = Rarity;
		Item.Count = Count;
		const int32 Left = Inventory->AddItem(Item);
		if (Left > 0)
		{
			Test.AddWarning(FString::Printf(TEXT("GiveItem: %d of %d %s did not fit or have no data"), Left, Count, *ItemId.ToString()));
		}
		return Left;
	}

	bool FScenarioContext::GrantSkills(ACrossguardCharacter* Character, const TArray<FName>& SkillIds)
	{
		UCrossguardAbilitySystemComponent* ASC = Character != nullptr ? Character->GetCrossguardAbilitySystemComponent() : nullptr;
		if (ASC == nullptr)
		{
			Fail(TEXT("GrantSkills: no character or ability system"));
			return false;
		}

		UCrossguardLoadoutComponent* Loadout = Character->GetLoadoutComponent();
		if (Loadout != nullptr && Loadout->SkillCatalog != nullptr)
		{
			if (ACrossguardPlayerState* Progression = PlayerState(Character))
			{
				for (const FName& Skill : SkillIds)
				{
					Progression->LearnSkillForTesting(Skill);
				}
			}
			FCrossguardLoadoutResult Result = Loadout->ValidateLoadout(SkillIds);
			// Not enough Skill Capacity at this level: raise the level (debug progression) until it fits,
			// up to the cap. Attributes are left as they are; ExpectedDamage reads the current stats.
			while (Result.Error == ECrossguardLoadoutError::OverCapacity && Character->Level < CrossguardScenarioLocal::MaxTestLevel)
			{
				Character->bUseDebugProgression = true;
				++Character->Level;
				Result = Loadout->ValidateLoadout(SkillIds);
			}
			if (!Result.IsValid())
			{
				Test.AddError(FString::Printf(TEXT("GrantSkills on %s: the loadout refuses it: %s (skill %s, other %s, cost %d of %d)"),
					*Character->GetName(), *StaticEnum<ECrossguardLoadoutError>()->GetNameStringByValue(static_cast<int64>(Result.Error)),
					*Result.SkillId.ToString(), *Result.OtherSkillId.ToString(), Result.TotalCost, Result.Capacity));
				return false;
			}
			Loadout->RequestLoadout(SkillIds);
			const bool bApplied = Loadout->GetEquippedSkills() == SkillIds;
			if (!bApplied)
			{
				Test.AddError(FString::Printf(TEXT("GrantSkills on %s: the loadout was not applied"), *Character->GetName()));
			}
			return bApplied;
		}

		Test.AddInfo(FString::Printf(TEXT("GrantSkills on %s: no Skill Catalog on its LoadoutComponent; granting native skill classes directly"),
			*Character->GetName()));
		bool bAll = true;
		for (const FName& Skill : SkillIds)
		{
			const TSubclassOf<UCrossguardGameplayAbility> AbilityClass = CrossguardNativeSkills::Find(Skill);
			if (AbilityClass == nullptr)
			{
				Test.AddError(FString::Printf(TEXT("GrantSkills: %s has no native class; set the character Blueprint's LoadoutComponent Skill Catalog (DA_SkillCatalog)"),
					*Skill.ToString()));
				bAll = false;
				continue;
			}
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, Character));
		}
		return bAll;
	}

	// --- Acting -----------------------------------------------------------------------------------

	void FScenarioContext::Face(ACrossguardCharacter* Character, float Yaw)
	{
		if (Character == nullptr)
		{
			Fail(TEXT("Face: no character"));
			return;
		}
		const FRotator Rotation(0.0f, Yaw, 0.0f);
		Character->SetActorRotation(Rotation);
		if (AController* Controller = Character->GetController())
		{
			Controller->SetControlRotation(Rotation);
		}
	}

	void FScenarioContext::Aim(ACrossguardCharacter* Character, const AActor* Target)
	{
		if (Target == nullptr)
		{
			Fail(TEXT("Aim: no target"));
			return;
		}
		AimAt(Character, Target->GetActorLocation());
	}

	void FScenarioContext::AimAt(ACrossguardCharacter* Character, const FVector& Point)
	{
		if (Character == nullptr)
		{
			Fail(TEXT("AimAt: no character"));
			return;
		}
		// Weapon traces, skills and guards look from here (APawn::GetActorEyesViewPoint).
		const FRotator View = (Point - Character->GetPawnViewLocation()).Rotation();
		Character->SetActorRotation(FRotator(0.0f, View.Yaw, 0.0f));
		if (AController* Controller = Character->GetController())
		{
			Controller->SetControlRotation(View);
		}
	}

	bool FScenarioContext::BasicAttack(ACrossguardCharacter* Character)
	{
		return Character != nullptr && Character->TryWeaponAttack();
	}

	bool FScenarioContext::RaiseGuard(ACrossguardCharacter* Character)
	{
		if (Character == nullptr)
		{
			return false;
		}
		Character->TrySecondaryAction();
		return HasTag(Character, CrossguardTags::State_Guarding);
	}

	void FScenarioContext::LowerGuard(ACrossguardCharacter* Character)
	{
		if (UAbilitySystemComponent* ASC = CrossguardScenarioLocal::FindASC(Character))
		{
			const FGameplayTagContainer GuardTag(CrossguardTags::Ability_Guard);
			ASC->CancelAbilities(&GuardTag);
		}
	}

	bool FScenarioContext::ActivateSkill(ACrossguardCharacter* Character, FName SkillId)
	{
		UAbilitySystemComponent* ASC = CrossguardScenarioLocal::FindASC(Character);
		const FGameplayAbilitySpec* Spec = CrossguardScenarioLocal::FindSkillSpec(ASC, SkillId);
		if (Spec == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("ActivateSkill: %s has no active skill '%s' (GrantSkills first)"), *GetNameSafe(Character),
				*SkillId.ToString()));
			return false;
		}
		// Copy the handle: activation can change the spec array.
		const FGameplayAbilitySpecHandle Handle = Spec->Handle;
		return ASC->TryActivateAbility(Handle);
	}

	bool FScenarioContext::IsOnCooldown(const ACrossguardCharacter* Character, FName SkillId) const
	{
		UAbilitySystemComponent* ASC = CrossguardScenarioLocal::FindASC(Character);
		const FGameplayAbilitySpec* Spec = CrossguardScenarioLocal::FindSkillSpec(ASC, SkillId);
		const UCrossguardGameplayAbility* Ability = Spec != nullptr ? Cast<UCrossguardGameplayAbility>(Spec->Ability) : nullptr;
		return Ability != nullptr && !Ability->CooldownTags.IsEmpty() && ASC->HasAnyMatchingGameplayTags(Ability->CooldownTags);
	}

	void FScenarioContext::Kill(AActor* Victim, AActor* Killer)
	{
		UAbilitySystemComponent* VictimASC = CrossguardScenarioLocal::FindASC(Victim);
		UAbilitySystemComponent* SourceASC = Killer != nullptr ? CrossguardScenarioLocal::FindASC(Killer) : VictimASC;
		if (VictimASC == nullptr || SourceASC == nullptr)
		{
			Fail(FString::Printf(TEXT("Kill: %s or %s has no ability system"), *GetNameSafe(Victim), *GetNameSafe(Killer)));
			return;
		}
		const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, SourceASC->MakeEffectContext());
		if (!Spec.IsValid())
		{
			Fail(TEXT("Kill: could not make the damage spec"));
			return;
		}
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, 1.0e7f);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, 1.0e7f);
		SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
	}

	// --- Reading state ----------------------------------------------------------------------------

	float FScenarioContext::Health(const AActor* Actor) const
	{
		const UAbilitySystemComponent* ASC = CrossguardScenarioLocal::FindASC(Actor);
		return ASC != nullptr ? ASC->GetNumericAttribute(UCrossguardAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	float FScenarioContext::MaxHealth(const AActor* Actor) const
	{
		const UAbilitySystemComponent* ASC = CrossguardScenarioLocal::FindASC(Actor);
		return ASC != nullptr ? ASC->GetNumericAttribute(UCrossguardAttributeSet::GetMaxHealthAttribute()) : -1.0f;
	}

	float FScenarioContext::Stat(const AActor* Actor, const FGameplayAttribute& Attribute) const
	{
		const UAbilitySystemComponent* ASC = CrossguardScenarioLocal::FindASC(Actor);
		return ASC != nullptr ? ASC->GetNumericAttribute(Attribute) : 0.0f;
	}

	bool FScenarioContext::HasTag(const AActor* Actor, const FGameplayTag& Tag) const
	{
		const UAbilitySystemComponent* ASC = CrossguardScenarioLocal::FindASC(Actor);
		return ASC != nullptr && Tag.IsValid() && ASC->HasMatchingGameplayTag(Tag);
	}

	bool FScenarioContext::HasTag(const AActor* Actor, const TCHAR* TagName) const
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
		if (!Tag.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("HasTag: no gameplay tag %s"), TagName));
			return false;
		}
		return HasTag(Actor, Tag);
	}

	bool FScenarioContext::IsDead(const AActor* Actor) const
	{
		return HasTag(Actor, CrossguardTags::State_Dead);
	}

	ACrossguardPlayerState* FScenarioContext::PlayerState(const AActor* Actor) const
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		return Pawn != nullptr ? Pawn->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	}

	float FScenarioContext::WeaponValue(const ACrossguardCharacter* Character, ECrossguardEquipmentSlot Slot) const
	{
		const UCrossguardEquipmentComponent* Equipment = Character != nullptr ? Character->GetEquipmentComponent() : nullptr;
		const FCrossguardWeaponRow* Row = Equipment != nullptr ? Equipment->GetWeaponRow(Slot) : nullptr;
		if (Row == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("WeaponValue: %s holds no weapon in that slot"), *GetNameSafe(Character)));
			return 0.0f;
		}
		return UCrossguardItemLibrary::GetWeaponValue(*Row, Equipment->GetItem(Slot).Rarity);
	}

	float FScenarioContext::ExpectedDamage(const AActor* Source, const AActor* Target, float AttackBase, float MagicBase, float Coefficient,
		float AttackShare) const
	{
		// Mirrors UCrossguardDamageExecution::Execute_Implementation.
		const float TrueDamage = Stat(Source, UCrossguardAttributeSet::GetTrueDamageAttribute());
		double Total = 0.0;
		if (AttackBase > 0.0f)
		{
			Crossguard::FDamageInput Input;
			Input.Base = AttackBase;
			Input.Flat = Stat(Source, UCrossguardAttributeSet::GetAdditionalAttackDamageAttribute());
			Input.Bonus = Stat(Source, UCrossguardAttributeSet::GetAttackDamageBonusAttribute());
			Input.Coefficient = Coefficient;
			Input.Resistance = Stat(Target, UCrossguardAttributeSet::GetAttackResistanceAttribute());
			Input.Penetration = Stat(Source, UCrossguardAttributeSet::GetAttackPenetrationAttribute());
			Input.TrueDamage = TrueDamage;
			Total += Crossguard::CalculateDamage(Input) * FMath::Max(AttackShare, 0.0f);
		}
		if (MagicBase > 0.0f)
		{
			Crossguard::FDamageInput Input;
			Input.Base = MagicBase;
			Input.Flat = Stat(Source, UCrossguardAttributeSet::GetAdditionalMagicDamageAttribute());
			Input.Bonus = Stat(Source, UCrossguardAttributeSet::GetMagicDamageBonusAttribute());
			Input.Coefficient = Coefficient;
			Input.Resistance = Stat(Target, UCrossguardAttributeSet::GetMagicResistanceAttribute());
			Input.Penetration = Stat(Source, UCrossguardAttributeSet::GetMagicPenetrationAttribute());
			Input.TrueDamage = AttackBase > 0.0f ? 0.0f : TrueDamage;
			Total += Crossguard::CalculateDamage(Input);
		}
		return static_cast<float>(Total);
	}

	// --- Game mode and time -----------------------------------------------------------------------

	void FScenarioContext::SetRaid(bool bRaid)
	{
		ACrossguardGameMode* Mode = GameMode();
		UWorld* PlayWorld = World();
		if (Mode == nullptr || PlayWorld == nullptr)
		{
			Fail(TEXT("SetRaid: no game mode"));
			return;
		}
		Mode->bIsRaid = bRaid;
		if (const AGameStateBase* GameState = PlayWorld->GetGameState())
		{
			for (APlayerState* Player : GameState->PlayerArray)
			{
				ACrossguardPlayerState* Progression = Cast<ACrossguardPlayerState>(Player);
				if (Progression == nullptr)
				{
					continue;
				}
				if (bRaid && !Progression->IsInRaid())
				{
					Progression->BeginRaid();
				}
				else if (!bRaid && Progression->IsInRaid())
				{
					Progression->EndRaid(false);
				}
			}
		}
	}

	void FScenarioContext::SetTimeDilation(float Dilation)
	{
		if (UWorld* PlayWorld = World())
		{
			UGameplayStatics::SetGlobalTimeDilation(PlayWorld, Dilation);
		}
	}

	// --- Log --------------------------------------------------------------------------------------

	bool FScenarioContext::LogContains(const FString& Text) const
	{
		return LogCount(Text) > 0;
	}

	int32 FScenarioContext::LogCount(const FString& Text) const
	{
		int32 Count = 0;
		for (const FString& Line : LogLines())
		{
			if (Line.Contains(Text))
			{
				++Count;
			}
		}
		return Count;
	}

	TArray<FString> FScenarioContext::LogLines() const
	{
		return LogCapture.IsValid() ? LogCapture->GetLines() : TArray<FString>();
	}

	void FScenarioContext::ClearLog()
	{
		if (LogCapture.IsValid())
		{
			LogCapture->Clear();
		}
	}

	// --- Runner -----------------------------------------------------------------------------------

	// Owns one scenario from map creation to the end of PIE. Update() is called every frame by the
	// latent command until it returns true.
	class FScenarioRunner
	{
	public:
		explicit FScenarioRunner(FScenario& Scenario)
			: Test(Scenario.Test)
			, Items(MoveTemp(Scenario.Items))
			, Dilation(Scenario.Dilation)
			, TimeoutSeconds(Scenario.TimeoutSeconds)
			, bRaid(Scenario.bRaid)
			, Context(Scenario.Test)
		{
			Context.bKeepDebugLoadout = Scenario.bKeepDebugLoadout;
		}

		~FScenarioRunner()
		{
			StopLogCapture();
		}

		bool Update()
		{
			switch (Phase)
			{
			case EPhase::Start:
				return Start();
			case EPhase::WaitForOldPIE:
				if (GEditor->PlayWorld == nullptr)
				{
					return BeginScenario();
				}
				if (RealSince(PhaseStarted) > CrossguardScenarioLocal::PIEEndTimeout)
				{
					Test.AddError(TEXT("A PIE session was already running and did not end; scenario not run"));
					return true;
				}
				return false;
			case EPhase::WaitForPIE:
				return WaitForPIE();
			case EPhase::Steps:
				return RunSteps();
			case EPhase::EndPIE:
				return EndPIE();
			case EPhase::WaitForEnd:
				if (GEditor == nullptr || GEditor->PlayWorld == nullptr)
				{
					return true;
				}
				if (RealSince(PhaseStarted) > CrossguardScenarioLocal::PIEEndTimeout)
				{
					Test.AddError(FString::Printf(TEXT("PIE did not end within %.0f s"), CrossguardScenarioLocal::PIEEndTimeout));
					return true;
				}
				return false;
			}
			return true;
		}

	private:
		enum class EPhase : uint8
		{
			Start,
			WaitForOldPIE,
			WaitForPIE,
			Steps,
			EndPIE,
			WaitForEnd,
		};

		static double RealSince(double Since) { return FPlatformTime::Seconds() - Since; }

		void SetPhase(EPhase NewPhase)
		{
			Phase = NewPhase;
			PhaseStarted = FPlatformTime::Seconds();
		}

		bool Start()
		{
			if (GEditor == nullptr)
			{
				Test.AddError(TEXT("Scenario tests need the editor (GEditor is null)"));
				return true;
			}
			if (GEditor->PlayWorld != nullptr)
			{
				// A PIE session from before (a user's, or a test that did not clean up).
				GEditor->RequestEndPlayMap();
				SetPhase(EPhase::WaitForOldPIE);
				return false;
			}
			return BeginScenario();
		}

		// Finds the Blueprints, makes the map and asks for PIE. True (done) if it cannot.
		bool BeginScenario()
		{
			UClass* GameModeClass = CrossguardScenarioLocal::FindBlueprintClass(Test, ACrossguardGameMode::StaticClass(), nullptr, CrossguardScenarioLocal::PreferredGameMode,
				TEXT("Create a Blueprint Class with parent CrossguardGameMode named BP_CrossguardGameMode and set it up as docs/next-build-checklist.md section 2 says."));
			if (GameModeClass == nullptr)
			{
				return true;
			}
			UClass* CharacterClass = ResolveCharacterClass(GameModeClass);
			if (CharacterClass == nullptr)
			{
				return true;
			}
			GameModeClassRef.Reset(GameModeClass);
			CharacterClassRef.Reset(CharacterClass);
			Test.AddInfo(FString::Printf(TEXT("Scenario: game mode %s, character %s"), *GameModeClass->GetPathName(), *CharacterClass->GetPathName()));

			UWorld* EditorWorld = FAutomationEditorCommonUtils::CreateNewMap();
			if (EditorWorld == nullptr)
			{
				Test.AddError(TEXT("Scenario set-up: could not create a new map"));
				return true;
			}
			if (AWorldSettings* Settings = EditorWorld->GetWorldSettings())
			{
				Settings->DefaultGameMode = GameModeClass;
			}
			BuildTestMap(*EditorWorld);

			FRequestPlaySessionParams Params;
			Params.WorldType = EPlaySessionWorldType::PlayInEditor;
			PlaySettings.Reset(NewObject<ULevelEditorPlaySettings>(GetTransientPackage()));
			PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
			PlaySettings->SetPlayNumberOfClients(1);
			PlaySettings->SetRunUnderOneProcess(true);
			Params.EditorPlaySettings = PlaySettings.Get();
			GEditor->RequestPlaySession(Params);
			SetPhase(EPhase::WaitForPIE);
			return false;
		}

		// The game mode's Default Pawn Class if it is a Blueprint of ACrossguardCharacter (what PIE
		// uses), else BP_CrossguardCharacter (or another such Blueprint, AI characters excluded).
		UClass* ResolveCharacterClass(UClass* GameModeClass)
		{
			const ACrossguardGameMode* ModeDefaults = GameModeClass->GetDefaultObject<ACrossguardGameMode>();
			UClass* PawnClass = ModeDefaults != nullptr ? ModeDefaults->DefaultPawnClass.Get() : nullptr;
			UClass* CharacterClass = nullptr;
			if (PawnClass != nullptr && PawnClass->IsChildOf(ACrossguardCharacter::StaticClass()) && !PawnClass->HasAnyClassFlags(CLASS_Native)
				&& !PawnClass->IsChildOf(ACrossguardAICharacter::StaticClass()))
			{
				CharacterClass = PawnClass;
			}
			else
			{
				CharacterClass = CrossguardScenarioLocal::FindBlueprintClass(Test, ACrossguardCharacter::StaticClass(), ACrossguardAICharacter::StaticClass(), CrossguardScenarioLocal::PreferredCharacter,
					TEXT("Create a Blueprint Class with parent CrossguardCharacter named BP_CrossguardCharacter, set its tables as docs/next-build-checklist.md sections 2-3 say, and make it the game mode's Default Pawn Class."));
				if (CharacterClass != nullptr)
				{
					Test.AddWarning(FString::Printf(TEXT("%s's Default Pawn Class is %s, not a character Blueprint; scenarios spawn %s (the PIE player still uses the Default Pawn Class)"),
						*GameModeClass->GetName(), *GetNameSafe(PawnClass), *CharacterClass->GetName()));
				}
			}
			if (CharacterClass != nullptr)
			{
				const ACrossguardCharacter* Defaults = CharacterClass->GetDefaultObject<ACrossguardCharacter>();
				if (Defaults->GetEquipmentComponent() == nullptr || Defaults->GetEquipmentComponent()->WeaponTable == nullptr)
				{
					Test.AddWarning(FString::Printf(TEXT("%s: EquipmentComponent Weapon Table is unset (DT_Weapons); Equip will fail"), *CharacterClass->GetName()));
				}
				if (Defaults->GetInventoryComponent() == nullptr || Defaults->GetInventoryComponent()->ItemTable == nullptr)
				{
					Test.AddWarning(FString::Printf(TEXT("%s: InventoryComponent Item Table is unset (DT_Items); GiveItem will fail"), *CharacterClass->GetName()));
				}
			}
			return CharacterClass;
		}

		// A floor to stand on and a Player Start for the PIE player, out of the way.
		void BuildTestMap(UWorld& EditorWorld)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (Cube == nullptr)
			{
				Test.AddError(TEXT("Scenario set-up: /Engine/BasicShapes/Cube not found; there is no floor"));
			}
			else
			{
				// The cube is 100 cm: scaled to FloorSize x FloorSize x 100 cm, its top at Z = 0.
				const float Scale = CrossguardScenarioLocal::FloorSize / 100.0f;
				const FTransform FloorTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -50.0f), FVector(Scale, Scale, 1.0f));
				AStaticMeshActor* Floor = EditorWorld.SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FloorTransform, Params);
				if (Floor != nullptr && Floor->GetStaticMeshComponent() != nullptr)
				{
					Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
					Floor->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				}
				else
				{
					Test.AddError(TEXT("Scenario set-up: could not spawn the floor"));
				}
			}

			if (EditorWorld.SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FTransform(FRotator::ZeroRotator, CrossguardScenarioLocal::LocalPlayerStart), Params) == nullptr)
			{
				Test.AddWarning(TEXT("Scenario set-up: could not spawn a Player Start; the PIE player spawns at the origin"));
			}
		}

		bool WaitForPIE()
		{
			UWorld* PlayWorld = GEditor->PlayWorld;
			if (PlayWorld != nullptr && PlayWorld->HasBegunPlay() && PlayWorld->GetAuthGameMode() != nullptr)
			{
				PlaySettings.Reset();
				return OnPIEReady(*PlayWorld);
			}
			if (RealSince(PhaseStarted) > CrossguardScenarioLocal::PIEStartTimeout)
			{
				Test.AddError(FString::Printf(TEXT("PIE did not start within %.0f s"), CrossguardScenarioLocal::PIEStartTimeout));
				SetPhase(EPhase::EndPIE);
			}
			return false;
		}

		bool OnPIEReady(UWorld& PlayWorld)
		{
			ACrossguardGameMode* Mode = Cast<ACrossguardGameMode>(PlayWorld.GetAuthGameMode());
			if (Mode == nullptr || Mode->GetClass() != GameModeClassRef.Get())
			{
				Test.AddError(FString::Printf(TEXT("PIE runs game mode %s, expected %s (World Settings Game Mode Override not applied?)"),
					*GetNameSafe(PlayWorld.GetAuthGameMode()), *GetNameSafe(GameModeClassRef.Get())));
				if (Mode == nullptr)
				{
					SetPhase(EPhase::EndPIE);
					return false;
				}
			}
			Context.WorldPtr = &PlayWorld;
			Context.GameModePtr = Mode;
			Context.CharacterClassPtr = CharacterClassRef.Get();

			LogCapture = MakeShared<FScenarioLogCapture>();
			Context.LogCapture = LogCapture;
			if (GLog != nullptr)
			{
				GLog->AddOutputDevice(LogCapture.Get());
				bLogRegistered = true;
			}

			if (!FMath::IsNearlyEqual(Dilation, 1.0f))
			{
				Context.SetTimeDilation(Dilation);
			}
			Context.SetRaid(bRaid);
			if (ACrossguardCharacter* Local = Context.LocalPlayer())
			{
				Context.Remember(TEXT("LocalPlayer"), Local);
			}
			StepsStarted = FPlatformTime::Seconds();
			SetPhase(EPhase::Steps);
			return false;
		}

		bool RunSteps()
		{
			UWorld* PlayWorld = Context.World();
			if (PlayWorld == nullptr || GEditor->PlayWorld != PlayWorld)
			{
				Test.AddError(TEXT("PIE ended (or changed) while the scenario was running"));
				SetPhase(EPhase::EndPIE);
				return false;
			}
			if (RealSince(StepsStarted) > TimeoutSeconds)
			{
				Test.AddError(FString::Printf(TEXT("Scenario timed out after %.0f real seconds, at item %d of %d"), TimeoutSeconds, ItemIndex + 1,
					Items.Num()));
				SetPhase(EPhase::EndPIE);
				return false;
			}
			if (Context.IsStopped() || !Items.IsValidIndex(ItemIndex))
			{
				Test.AddInfo(FString::Printf(TEXT("Scenario ended at %.2f s game time"), Context.Now()));
				SetPhase(EPhase::EndPIE);
				return false;
			}

			FScenario::FItem& Item = Items[ItemIndex];
			const float Now = Context.Now();
			switch (Item.Kind)
			{
			case FScenario::FItem::EKind::Step:
				Test.AddInfo(FString::Printf(TEXT("[%.2f s] %s"), Now, *Item.Name));
				if (Item.Action)
				{
					Item.Action(Context);
				}
				NextItem();
				break;
			case FScenario::FItem::EKind::Wait:
				if (!bItemStarted)
				{
					bItemStarted = true;
					ItemDeadline = Now + Item.Seconds;
				}
				if (Now >= ItemDeadline)
				{
					NextItem();
				}
				break;
			case FScenario::FItem::EKind::WaitUntil:
				if (!bItemStarted)
				{
					bItemStarted = true;
					ItemDeadline = Now + Item.Seconds;
					NextPoll = Now;
					Test.AddInfo(FString::Printf(TEXT("[%.2f s] until: %s"), Now, *Item.Name));
				}
				if (Now >= NextPoll)
				{
					NextPoll = Now + Item.PollSeconds;
					if (Item.Condition && Item.Condition(Context))
					{
						NextItem();
						break;
					}
				}
				if (!Context.IsStopped() && Now > ItemDeadline)
				{
					Context.Fail(FString::Printf(TEXT("Timed out after %.1f game seconds waiting until: %s"), Item.Seconds, *Item.Name));
				}
				break;
			}
			return false;
		}

		void NextItem()
		{
			++ItemIndex;
			bItemStarted = false;
		}

		bool EndPIE()
		{
			StopLogCapture();
			if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
			{
				GEditor->RequestEndPlayMap();
				SetPhase(EPhase::WaitForEnd);
				return false;
			}
			return true;
		}

		void StopLogCapture()
		{
			if (bLogRegistered && LogCapture.IsValid() && GLog != nullptr)
			{
				GLog->RemoveOutputDevice(LogCapture.Get());
			}
			bLogRegistered = false;
		}

		FAutomationTestBase& Test;
		TArray<FScenario::FItem> Items;
		float Dilation = 1.0f;
		float TimeoutSeconds = 180.0f;
		bool bRaid = true;

		FScenarioContext Context;
		TSharedPtr<FScenarioLogCapture> LogCapture;
		bool bLogRegistered = false;

		TStrongObjectPtr<UClass> GameModeClassRef;
		TStrongObjectPtr<UClass> CharacterClassRef;
		TStrongObjectPtr<ULevelEditorPlaySettings> PlaySettings;

		EPhase Phase = EPhase::Start;
		double PhaseStarted = 0.0;
		double StepsStarted = 0.0;
		int32 ItemIndex = 0;
		bool bItemStarted = false;
		float ItemDeadline = 0.0f;
		float NextPoll = 0.0f;
	};

	// The latent command the automation framework ticks.
	class FScenarioCommand : public IAutomationLatentCommand
	{
	public:
		explicit FScenarioCommand(const TSharedRef<FScenarioRunner>& InRunner)
			: Runner(InRunner)
		{
		}

		virtual bool Update() override
		{
			return Runner->Update();
		}

	private:
		TSharedRef<FScenarioRunner> Runner;
	};

	// --- Builder ----------------------------------------------------------------------------------

	FScenario::FScenario(FAutomationTestBase& InTest)
		: Test(InTest)
	{
	}

	FScenario& FScenario::Step(const FString& Name, TFunction<void(FScenarioContext&)> Action)
	{
		FItem& Item = Items.AddDefaulted_GetRef();
		Item.Kind = FItem::EKind::Step;
		Item.Name = Name;
		Item.Action = MoveTemp(Action);
		return *this;
	}

	FScenario& FScenario::Wait(float Seconds)
	{
		FItem& Item = Items.AddDefaulted_GetRef();
		Item.Kind = FItem::EKind::Wait;
		Item.Name = FString::Printf(TEXT("wait %.2f s"), Seconds);
		Item.Seconds = FMath::Max(Seconds, 0.0f);
		return *this;
	}

	FScenario& FScenario::WaitUntil(const FString& Name, float TimeoutGameSeconds, TFunction<bool(FScenarioContext&)> Condition, float PollSeconds)
	{
		FItem& Item = Items.AddDefaulted_GetRef();
		Item.Kind = FItem::EKind::WaitUntil;
		Item.Name = Name;
		Item.Condition = MoveTemp(Condition);
		Item.Seconds = FMath::Max(TimeoutGameSeconds, 0.0f);
		Item.PollSeconds = FMath::Max(PollSeconds, 0.0f);
		return *this;
	}

	FScenario& FScenario::TimeDilation(float InDilation)
	{
		Dilation = InDilation;
		return *this;
	}

	FScenario& FScenario::Timeout(float RealSeconds)
	{
		TimeoutSeconds = RealSeconds;
		return *this;
	}

	FScenario& FScenario::Raid(bool bInRaid)
	{
		bRaid = bInRaid;
		return *this;
	}

	FScenario& FScenario::KeepDebugLoadout()
	{
		bKeepDebugLoadout = true;
		return *this;
	}

	void FScenario::Run()
	{
		if (bQueued)
		{
			Test.AddError(TEXT("FScenario::Run called twice: one scenario per test"));
			return;
		}
		bQueued = true;
		const TSharedRef<FScenarioRunner> Runner = MakeShared<FScenarioRunner>(*this);
		ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(Runner));
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace CrossguardScenario
{
	void FScenarioContext::SetHealth(AActor* Actor, float Value)
	{
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		if (ASC == nullptr)
		{
			Fail(TEXT("SetHealth: no ability system"));
			return;
		}
		ASC->SetNumericAttributeBase(UCrossguardAttributeSet::GetHealthAttribute(), Value);
	}

	void FScenarioContext::ApplyStatus(AActor* Actor, FGameplayTag State, float Seconds)
	{
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		if (ASC == nullptr)
		{
			Fail(TEXT("ApplyStatus: no ability system"));
			return;
		}
		UCrossguardSkillAbility::ApplyStatus(ASC, ASC, State, Seconds);
	}
}
#endif
