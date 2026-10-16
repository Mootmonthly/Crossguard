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
#include "Camera/CameraActor.h"
#include "Character/CrossguardAICharacter.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "Character/CrossguardMinionController.h"
#include "Character/CrossguardMonster.h"
#include "Character/CrossguardMonsterController.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CrossguardRules/Combat.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
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
		// Unbuffered: GLog then calls Serialize on the logging thread right away. A buffered device gets
		// lines later from the dedicated log thread, after the test has already looked (UE 5.8).
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

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

	// --- Networked -------------------------------------------------------------------------------

	UWorld* FScenarioContext::ClientWorld(int32 Index) const
	{
		if (Index < 0 || Index >= NumClients || GEditor == nullptr)
		{
			return nullptr;
		}
		const FWorldContext* Context = GEditor->GetPIEWorldContext(FirstClientInstance + Index);
		return Context != nullptr && !Context->RunAsDedicated ? Context->World() : nullptr;
	}

	APlayerController* FScenarioContext::ClientController(int32 Index) const
	{
		UWorld* Client = ClientWorld(Index);
		if (Client == nullptr)
		{
			return nullptr;
		}
		// The listen host's world is the server world, with the remote players' controllers too: its own
		// is the one with a local player.
		for (FConstPlayerControllerIterator It = Client->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller != nullptr && Cast<ULocalPlayer>(Controller->Player) != nullptr)
			{
				return Controller;
			}
		}
		return nullptr;
	}

	ACrossguardCharacter* FScenarioContext::ClientPawn(int32 Index) const
	{
		const APlayerController* Controller = ClientController(Index);
		return Controller != nullptr ? Cast<ACrossguardCharacter>(Controller->GetPawn()) : nullptr;
	}

	APlayerController* FScenarioContext::ServerControllerOf(int32 Index) const
	{
		UWorld* Server = World();
		const APlayerController* ClientSide = ClientController(Index);
		if (Server == nullptr || ClientSide == nullptr)
		{
			return nullptr;
		}
		if (ClientSide->GetWorld() == Server)
		{
			return const_cast<APlayerController*>(ClientSide);
		}
		// The PlayerState's PlayerId is replicated and unique per login.
		const APlayerState* ClientState = ClientSide->PlayerState;
		if (ClientState == nullptr)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = Server->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller != nullptr && Controller->PlayerState != nullptr && Controller->PlayerState->GetPlayerId() == ClientState->GetPlayerId())
			{
				return Controller;
			}
		}
		return nullptr;
	}

	ACrossguardCharacter* FScenarioContext::ServerPawnOf(int32 Index) const
	{
		const APlayerController* Controller = ServerControllerOf(Index);
		return Controller != nullptr ? Cast<ACrossguardCharacter>(Controller->GetPawn()) : nullptr;
	}

	AActor* FScenarioContext::OnClient(const AActor* ServerActor, int32 Index) const
	{
		UWorld* Server = World();
		UWorld* Client = ClientWorld(Index);
		if (ServerActor == nullptr || Server == nullptr || Client == nullptr)
		{
			return nullptr;
		}
		if (Client == Server)
		{
			return const_cast<AActor*>(ServerActor);
		}
		// The server assigns every replicated actor a net GUID that the client's cache shares (UE 5.8:
		// UNetDriver::GetNetGuidCache, FNetGUIDCache::GetNetGUID / GetObjectFromNetGUID).
		const UNetDriver* ServerDriver = Server->GetNetDriver();
		const UNetDriver* ClientDriver = Client->GetNetDriver();
		if (ServerDriver == nullptr || ClientDriver == nullptr || !ServerDriver->GetNetGuidCache().IsValid() || !ClientDriver->GetNetGuidCache().IsValid())
		{
			return nullptr;
		}
		const FNetworkGUID Guid = ServerDriver->GetNetGuidCache()->GetNetGUID(ServerActor);
		if (!Guid.IsValid())
		{
			return nullptr;
		}
		AActor* Copy = Cast<AActor>(ClientDriver->GetNetGuidCache()->GetObjectFromNetGUID(Guid, true));
		return IsValid(Copy) && Copy->GetWorld() == Client ? Copy : nullptr;
	}

	void FScenarioContext::MakeAllies(ACrossguardCharacter* Player, const ACrossguardCharacter* Ally)
	{
		ACrossguardPlayerState* PlayerSide = PlayerState(Player);
		const ACrossguardPlayerState* AllySide = PlayerState(Ally);
		if (PlayerSide == nullptr || AllySide == nullptr || AllySide->GetTeamId() < 0)
		{
			Fail(TEXT("MakeAllies: both need a player state with a team"));
			return;
		}
		PlayerSide->SetTeamId(AllySide->GetTeamId());
	}

	bool FScenarioContext::HasSkill(const ACrossguardCharacter* Character, FName SkillId) const
	{
		return CrossguardScenarioLocal::FindSkillSpec(CrossguardScenarioLocal::FindASC(Character), SkillId) != nullptr;
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
		TFunctionRef<void(ACrossguardCharacter&)> Configure, ACrossguardCharacter* SpawnOwner)
	{
		UWorld* PlayWorld = World();
		if (PlayWorld == nullptr || Class == nullptr || !Class->IsChildOf(ACrossguardCharacter::StaticClass()))
		{
			Fail(FString::Printf(TEXT("Cannot spawn '%s': no PIE world or no character class"), *Name.ToString()));
			return nullptr;
		}
		const float HalfHeight = Class->GetDefaultObject<ACrossguardCharacter>()->GetDefaultHalfHeight();
		const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), FloorPoint + FVector(0.0f, 0.0f, HalfHeight + CrossguardScenarioLocal::SpawnClearance));
		ACrossguardCharacter* Character = PlayWorld->SpawnActorDeferred<ACrossguardCharacter>(Class, Transform, SpawnOwner, SpawnOwner,
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
		// UE 5.8: with no net driver (standalone PIE) a controller is local only if it has a ULocalPlayer,
		// which ours lack, so GAS refused LocalPredicted abilities. Mark it local before possession.
		Controller->SetAsLocalPlayerController();

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

	ACrossguardMonster* FScenarioContext::SpawnMonster(FName Name, FName MobId, const FVector& FloorPoint, float Yaw,
		TFunction<void(ACrossguardMonster&)> Configure)
	{
		ACrossguardCharacter* Character = SpawnCharacter(Name, ACrossguardMonster::StaticClass(), FloorPoint, Yaw, true,
			[MobId, &Configure](ACrossguardCharacter& Spawned)
			{
				ACrossguardMonster* Monster = CastChecked<ACrossguardMonster>(&Spawned);
				Monster->MobId = MobId;
				if (Configure)
				{
					Configure(*Monster);
				}
			});
		return Cast<ACrossguardMonster>(Character);
	}

	ACrossguardMinion* FScenarioContext::SpawnMinion(FName Name, ACrossguardCharacter* Summoner, const FVector& FloorPoint, UClass* MinionClass,
		TFunction<void(ACrossguardMinion&)> Configure)
	{
		if (Summoner == nullptr)
		{
			Fail(FString::Printf(TEXT("SpawnMinion '%s': no summoner"), *Name.ToString()));
			return nullptr;
		}
		UClass* SpawnClass = MinionClass != nullptr ? MinionClass : ACrossguardMinion::StaticClass();
		if (!SpawnClass->IsChildOf(ACrossguardMinion::StaticClass()))
		{
			Fail(FString::Printf(TEXT("SpawnMinion '%s': %s is not an ACrossguardMinion"), *Name.ToString(), *SpawnClass->GetName()));
			return nullptr;
		}
		ACrossguardCharacter* Character = SpawnCharacter(Name, SpawnClass, FloorPoint, static_cast<float>(Summoner->GetActorRotation().Yaw), true,
			[Summoner, &Configure](ACrossguardCharacter& Spawned)
			{
				ACrossguardMinion* Minion = CastChecked<ACrossguardMinion>(&Spawned);
				Minion->SetSummoner(Summoner);
				if (Configure)
				{
					Configure(*Minion);
				}
			},
			Summoner);
		ACrossguardMinion* Minion = Cast<ACrossguardMinion>(Character);
		if (Minion != nullptr)
		{
			Require(Minion->GetController() != nullptr, FString::Printf(TEXT("SpawnMinion '%s' has its controller"), *Name.ToString()));
		}
		return Minion;
	}

	AStaticMeshActor* FScenarioContext::SpawnBlockingBox(FName Name, const FVector& Center, const FVector& Size)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Require(Cube != nullptr, TEXT("/Engine/BasicShapes/Cube exists (SpawnBlockingBox)")))
		{
			return nullptr;
		}
		AStaticMeshActor* Box = SpawnActor<AStaticMeshActor>(Name, Center, 0.0f, [Cube](AStaticMeshActor& Spawned)
		{
			UStaticMeshComponent* Mesh = Spawned.GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		});
		if (Box != nullptr)
		{
			// The engine cube is 100 cm, centred.
			Box->SetActorScale3D(Size / 100.0f);
		}
		return Box;
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

	float FScenarioContext::Damage(AActor* Source, AActor* Victim, float Attack, float Magic)
	{
		UAbilitySystemComponent* VictimASC = CrossguardScenarioLocal::FindASC(Victim);
		UAbilitySystemComponent* SourceASC = Source != nullptr ? CrossguardScenarioLocal::FindASC(Source) : VictimASC;
		if (VictimASC == nullptr || SourceASC == nullptr)
		{
			Fail(FString::Printf(TEXT("Damage: %s or %s has no ability system"), *GetNameSafe(Source), *GetNameSafe(Victim)));
			return 0.0f;
		}
		const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UCrossguardDamageEffect::StaticClass(), 1.0f, SourceASC->MakeEffectContext());
		if (!Spec.IsValid())
		{
			Fail(TEXT("Damage: could not make the damage spec"));
			return 0.0f;
		}
		const float Before = Health(Victim);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Attack, Attack);
		Spec.Data->SetSetByCallerMagnitude(CrossguardTags::SetByCaller_Damage_Magic, Magic);
		SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
		return Before - Health(Victim);
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

	AActor* FScenarioContext::TargetOf(const APawn* Pawn) const
	{
		AController* Controller = Pawn != nullptr ? Pawn->GetController() : nullptr;
		if (const ACrossguardMonsterController* MonsterController = Cast<ACrossguardMonsterController>(Controller))
		{
			return MonsterController->GetCurrentTarget();
		}
		if (const ACrossguardMinionController* MinionController = Cast<ACrossguardMinionController>(Controller))
		{
			return MinionController->GetCurrentTarget();
		}
		const AAIController* AI = Cast<AAIController>(Controller);
		return AI != nullptr ? AI->GetFocusActor() : nullptr;
	}

	int32 FScenarioContext::ClankCount(const AActor* Actor) const
	{
		const ACrossguardCharacter* Character = Cast<ACrossguardCharacter>(Actor);
		return Character != nullptr ? Character->GetWeaponClankCount() : 0;
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

	// --- Screenshots ------------------------------------------------------------------------------

	void FScenarioContext::LookFrom(const FVector& Eye, const FVector& LookAt, int32 ClientIndex)
	{
		UWorld* Target = ClientIndex < 0 ? World() : ClientWorld(ClientIndex);
		APlayerController* Controller = ClientIndex < 0 ? (Target != nullptr ? Target->GetFirstPlayerController() : nullptr) : ClientController(ClientIndex);
		if (Target == nullptr || Controller == nullptr)
		{
			Fail(FString::Printf(TEXT("LookFrom: no world or local player controller for viewport %d"), ClientIndex));
			return;
		}
		const FRotator Facing = (LookAt - Eye).Rotation();
		ACameraActor* Camera = Cameras.FindRef(ClientIndex).Get();
		if (Camera == nullptr)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			Camera = Target->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, Facing, Params);
			if (Camera == nullptr)
			{
				Fail(TEXT("LookFrom: could not spawn a camera"));
				return;
			}
			Cameras.Add(ClientIndex, Camera);
		}
		Camera->SetActorLocationAndRotation(Eye, Facing);
		Controller->SetViewTarget(Camera);
	}

	void FScenarioContext::KeepCameras()
	{
		// The engine puts the view back on the pawn after some events (possession, restart, the game
		// mode's own SetViewTargetWithBlend); every frame, a LookFrom camera takes it back.
		for (const TPair<int32, TWeakObjectPtr<ACameraActor>>& Entry : Cameras)
		{
			ACameraActor* Camera = Entry.Value.Get();
			UWorld* Target = Entry.Key < 0 ? World() : ClientWorld(Entry.Key);
			APlayerController* Controller = Entry.Key < 0 ? (Target != nullptr ? Target->GetFirstPlayerController() : nullptr) : ClientController(Entry.Key);
			if (Camera != nullptr && Controller != nullptr && Controller->GetViewTarget() != Camera)
			{
				Controller->SetViewTarget(Camera);
			}
		}
	}

	bool FScenarioContext::Screenshot(const FString& Name, int32 ClientIndex)
	{
		if (!FApp::CanEverRender())
		{
			Test.AddError(FString::Printf(TEXT("Screenshot '%s': the editor cannot render (-nullrhi); run Tools\\run_editor_tests.bat -Visual"), *Name));
			return false;
		}
		UWorld* Target = ClientIndex < 0 ? World() : ClientWorld(ClientIndex);
		UGameViewportClient* ViewportClient = Target != nullptr ? Target->GetGameViewport() : nullptr;
		FSceneViewport* Viewport = ViewportClient != nullptr ? ViewportClient->Viewport : nullptr;
		const FIntPoint Size = Viewport != nullptr ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		if (Viewport == nullptr || Size.X <= 0 || Size.Y <= 0)
		{
			Test.AddError(FString::Printf(TEXT("Screenshot '%s': %s has no game viewport"), *Name, ClientIndex < 0 ? TEXT("the server world") : *FString::Printf(TEXT("client %d"), ClientIndex)));
			return false;
		}
		// Slate's screenshot of the PIE window draws the scene and the UMG widgets (the HUD, open windows)
		// together; the viewport's own ReadPixels is the scene alone, so it is only the fallback.
		TArray<FColor> Pixels;
		FIntPoint Captured = Size;
		const TSharedPtr<SWindow> Window = ViewportClient->GetWindow();
		FIntVector SlateSize = FIntVector::ZeroValue;
		if (Window.IsValid() && FSlateApplication::IsInitialized() && FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, SlateSize)
			&& Pixels.Num() == SlateSize.X * SlateSize.Y && SlateSize.X > 0)
		{
			Captured = FIntPoint(SlateSize.X, SlateSize.Y);
		}
		else
		{
			Pixels.Reset();
			if (!Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
			{
				Test.AddError(FString::Printf(TEXT("Screenshot '%s': ReadPixels failed (%d pixels for %d x %d)"), *Name, Pixels.Num(), Size.X, Size.Y));
				return false;
			}
		}
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}
		FString Folder = Test.GetBeautifiedTestName();
		Folder.RemoveFromStart(TEXT("Crossguard.Visual."));
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("EditorTests") / TEXT("Screenshots") / Folder;
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = Directory / Name + TEXT(".png");
		if (!FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), Captured.X, Captured.Y, EGammaSpace::sRGB)))
		{
			Test.AddError(FString::Printf(TEXT("Screenshot '%s': could not write %s"), *Name, *Path));
			return false;
		}
		Test.AddInfo(FString::Printf(TEXT("Screenshot: %s (%d x %d)"), *FPaths::ConvertRelativePathToFull(Path), Captured.X, Captured.Y));
		return true;
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
			, NumClients(Scenario.NumClients)
			, bDedicatedServer(Scenario.bDedicatedServer)
			, Context(Scenario.Test)
		{
			Context.bKeepDebugLoadout = Scenario.bKeepDebugLoadout;
			Context.NumClients = NumClients;
			Context.FirstClientInstance = bDedicatedServer ? 1 : 0;
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
			if (NumClients > 0)
			{
				// One process (UE 5.8 PlayLevel.cpp): listen server = instance 0 is the host, 1..N-1 the
				// clients; PIE_Client = instance 0 a dedicated server (RunAsDedicated), 1..N the clients.
				PlaySettings->SetPlayNetMode(bDedicatedServer ? EPlayNetMode::PIE_Client : EPlayNetMode::PIE_ListenServer);
				PlaySettings->SetPlayNumberOfClients(NumClients);
			}
			else
			{
				PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
				PlaySettings->SetPlayNumberOfClients(1);
			}
			PlaySettings->SetRunUnderOneProcess(true);
			PlaySettings->bLaunchSeparateServer = false;
			PlaySettings->NewWindowWidth = 1280;
			PlaySettings->NewWindowHeight = 720;
			PlaySettings->SetClientWindowSize(FIntPoint(1280, 720));
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
			// Networked PIE: the net GUID cache only addresses a level whose outers are "name stable", which
			// a loaded map is through RF_WasLoaded. This map was never saved, so give its world and level the
			// flag (PIE's duplicate copies it); otherwise every spawn warns "PersistentLevel NOT Supported".
			EditorWorld.SetFlags(RF_WasLoaded);
			if (EditorWorld.PersistentLevel != nullptr)
			{
				EditorWorld.PersistentLevel->SetFlags(RF_WasLoaded);
			}

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
					// Visual run: a 200x scaled cube shadows its own top (acne) and goes black; it needs no shadow.
					Floor->GetStaticMeshComponent()->SetCastShadow(false);
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

			// Light, for the visual run (nothing is drawn headless): a movable sun, a sky and a sky light,
			// as a new default level has, so nothing needs building.
			if (FApp::CanEverRender())
			{
				ADirectionalLight* Sun = EditorWorld.SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(),
					FTransform(FRotator(-45.0f, 30.0f, 0.0f), FVector(0.0f, 0.0f, 500.0f)), Params);
				if (Sun != nullptr && Sun->GetLightComponent() != nullptr)
				{
					Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
					Sun->GetLightComponent()->SetIntensity(5.0f);
				}
				EditorWorld.SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity, Params);
				ASkyLight* Sky = EditorWorld.SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FTransform(FVector(0.0f, 0.0f, 500.0f)), Params);
				if (Sky != nullptr && Sky->GetLightComponent() != nullptr)
				{
					Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
					Sky->GetLightComponent()->bRealTimeCapture = true;
				}
			}
		}

		// The server world: PIE instance 0 (standalone, listen host or dedicated server). Not
		// GEditor->PlayWorld: after the editor's PIE tick loop that is left on the last instance ticked.
		static UWorld* ServerPlayWorld()
		{
			const FWorldContext* Context = GEditor != nullptr ? GEditor->GetPIEWorldContext(0) : nullptr;
			return Context != nullptr ? Context->World() : nullptr;
		}

		// Networked: every client world is up with its own logged-in player and pawn, which the server
		// also knows (so steps can use ServerPawnOf / ClientPawn at once).
		bool ClientsReady(UWorld& PlayWorld)
		{
			if (NumClients == 0)
			{
				return true;
			}
			const AGameStateBase* GameState = PlayWorld.GetGameState();
			if (GameState == nullptr || GameState->PlayerArray.Num() < NumClients)
			{
				return false;
			}
			Context.WorldPtr = &PlayWorld;
			for (int32 Index = 0; Index < NumClients; ++Index)
			{
				const UWorld* Client = Context.ClientWorld(Index);
				if (Client == nullptr || !Client->HasBegunPlay() || Context.ClientPawn(Index) == nullptr || Context.ServerPawnOf(Index) == nullptr)
				{
					return false;
				}
			}
			return true;
		}

		bool WaitForPIE()
		{
			UWorld* PlayWorld = ServerPlayWorld();
			if (PlayWorld != nullptr && PlayWorld->HasBegunPlay() && PlayWorld->GetAuthGameMode() != nullptr && ClientsReady(*PlayWorld))
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
			for (int32 Index = 0; Index < NumClients; ++Index)
			{
				Context.Remember(FName(*FString::Printf(TEXT("Client%d"), Index)), Context.ServerPawnOf(Index));
				Context.Remember(FName(*FString::Printf(TEXT("Client%d.Controller"), Index)), Context.ServerControllerOf(Index));
			}
			if (NumClients > 0)
			{
				Test.AddInfo(FString::Printf(TEXT("Networked: %s, %d client(s) logged in"), bDedicatedServer ? TEXT("dedicated server") : TEXT("listen server"), NumClients));
			}
			StepsStarted = FPlatformTime::Seconds();
			SetPhase(EPhase::Steps);
			return false;
		}

		bool RunSteps()
		{
			UWorld* PlayWorld = Context.World();
			if (PlayWorld == nullptr || ServerPlayWorld() != PlayWorld)
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

			Context.KeepCameras();
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
			case FScenario::FItem::EKind::WaitWatching:
				if (!bItemStarted)
				{
					bItemStarted = true;
					ItemDeadline = Now + Item.Seconds;
					Test.AddInfo(FString::Printf(TEXT("[%.2f s] watching %.2f s: %s"), Now, Item.Seconds, *Item.Name));
				}
				if (Item.Action)
				{
					Item.Action(Context);
				}
				if (Now >= ItemDeadline)
				{
					NextItem();
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
		int32 NumClients = 0;
		bool bDedicatedServer = false;

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

	FScenario& FScenario::WaitWatching(const FString& Name, float Seconds, TFunction<void(FScenarioContext&)> Watch)
	{
		FItem& Item = Items.AddDefaulted_GetRef();
		Item.Kind = FItem::EKind::WaitWatching;
		Item.Name = Name;
		Item.Action = MoveTemp(Watch);
		Item.Seconds = FMath::Max(Seconds, 0.0f);
		return *this;
	}

	FScenario& FScenario::WaitUntilOnClient(int32 Index, const FString& Name, float TimeoutGameSeconds,
		TFunction<bool(FScenarioContext&, UWorld&)> Condition, float PollSeconds)
	{
		return WaitUntil(FString::Printf(TEXT("[client %d] %s"), Index, *Name), TimeoutGameSeconds,
			[Index, Condition = MoveTemp(Condition)](FScenarioContext& C)
			{
				UWorld* ClientWorld = C.ClientWorld(Index);
				if (ClientWorld == nullptr)
				{
					C.Fail(FString::Printf(TEXT("client %d's world is gone (not a networked scenario, or it left)"), Index));
					return false;
				}
				return Condition && Condition(C, *ClientWorld);
			}, PollSeconds);
	}

	FScenario& FScenario::Clients(int32 Count, bool bInDedicatedServer)
	{
		NumClients = FMath::Clamp(Count, 1, 8);
		bDedicatedServer = bInDedicatedServer;
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
