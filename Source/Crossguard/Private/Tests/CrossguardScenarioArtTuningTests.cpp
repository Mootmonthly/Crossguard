// Scenario tests: art tuning (docs/art-placeholder-plan.md, "Tuning art without restarting"). A
// projectile row's mesh rotation and scale (Items.json projectileRotation / projectileScale) on the
// flying mesh, and CgReloadArtData putting a changed mob meshScale / weapon relativeTransform on what
// is already spawned. Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// Rows are changed in memory for the test only and put back in the same step.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMobRow.h"
#include "Character/CrossguardMonster.h"
#include "Character/CrossguardWeaponVisualsComponent.h"
#include "Combat/CrossguardCosmetics.h"
#include "Combat/CrossguardProjectile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Game/CrossguardArtReload.h"
#include "Game/CrossguardGameMode.h"
#include "HAL/IConsoleManager.h"
#include "Items/CrossguardEquipmentComponent.h"

namespace CrossguardScenarioArtTuning
{
	// Engine and project assets that exist in every checkout.
	const TCHAR* const TuningCylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const TuningMannyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");

	// Away from the other art scenes (ArtSceneY 1500) and the PIE player at (-30 m, -30 m).
	constexpr float TuningSceneY = 3000.0f;
}

// An ammo / throwable row's projectileRotation and projectileScale reach the flying mesh: an arrow
// modelled along +Y turned to face its flight.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioArtProjectileRotation, "Crossguard.Scenario.Art.ProjectileMeshRotation", CROSSGUARD_SCENARIO_FLAGS)
bool FCrossguardScenarioArtProjectileRotation::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioArtTuning;
	FScenario Scenario(*this);
	Scenario.Step(TEXT("A projectile with an item row's mesh, rotation and scale"), [](FScenarioContext& C)
	{
		FCrossguardItemRow Row;
		Row.ProjectileMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TuningCylinderPath));
		Row.ProjectileRotation = FRotator(10.0f, -90.0f, 0.0f);
		Row.ProjectileScale = FVector(0.5f, 0.5f, 2.0f);
		const FCrossguardProjectileVisual FromRow = CrossguardCosmetics::ProjectileVisualFromItem(Row);
		C.Test.TestTrue(TEXT("visual from the row: rotation"), FromRow.MeshRotation.Equals(Row.ProjectileRotation, 0.01f));
		C.Test.TestTrue(TEXT("visual from the row: scale"), FromRow.MeshScale.Equals(Row.ProjectileScale, 0.001f));

		// Over a Blueprint's own look: the row's mesh brings its rotation and scale along.
		FCrossguardProjectileVisual Merged;
		Merged.MeshRotation = FRotator(0.0f, 45.0f, 0.0f);
		Merged.MergeFrom(FromRow);
		C.Test.TestTrue(TEXT("merged over a Blueprint's: the row's rotation"), Merged.MeshRotation.Equals(Row.ProjectileRotation, 0.01f));

		// High above the scene, flying +X: nothing to hit in the step.
		ACrossguardProjectile* Projectile = C.SpawnActor<ACrossguardProjectile>(TEXT("Arrow"), FVector(0.0f, TuningSceneY, 2000.0f), 0.0f,
			[&Merged](ACrossguardProjectile& Spawned) { Spawned.Visual = Merged; });
		if (!C.Require(Projectile != nullptr && Projectile->GetVisualMesh() != nullptr, TEXT("projectile spawned")))
		{
			return;
		}
		const UStaticMeshComponent* Mesh = Projectile->GetVisualMesh();
		C.Test.TestTrue(TEXT("the row's mesh flies"), Mesh->GetStaticMesh() != nullptr && Mesh->GetStaticMesh()->GetPathName() == TuningCylinderPath);
		C.Test.TestTrue(TEXT("mesh turned by projectileRotation"), Mesh->GetRelativeRotation().Equals(Row.ProjectileRotation, 0.1f));
		C.Test.TestTrue(TEXT("mesh scaled by projectileScale"), Mesh->GetRelativeScale3D().Equals(Row.ProjectileScale, 0.001f));
		Projectile->Destroy();
	});
	Scenario.Run();
	return true;
}

// CgReloadArtData: a mob row's meshScale / meshYaw and a weapon row's relativeTransform changed in the
// JSON reach a monster and a held weapon that are already spawned (no restart). The JSON here is the
// test's own text, as it would be read from Data/.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioArtReloadArtData, "Crossguard.Scenario.Art.ReloadArtData", CROSSGUARD_SCENARIO_FLAGS)
bool FCrossguardScenarioArtReloadArtData::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioArtTuning;
	FScenario Scenario(*this);
	Scenario.Step(TEXT("The console command exists"), [](FScenarioContext& C)
	{
		const IConsoleObject* Command = IConsoleManager::Get().FindConsoleObject(TEXT("CgReloadArtData"));
		C.Test.TestTrue(TEXT("CgReloadArtData is a console command"), Command != nullptr && Command->AsCommand() != nullptr);
	});
	Scenario.Step(TEXT("A monster's meshScale and meshYaw change in the JSON: the spawned monster takes them"), [](FScenarioContext& C)
	{
		const TSoftObjectPtr<USkeletalMesh> Manny{FSoftObjectPath(TuningMannyPath)};
		if (Manny.LoadSynchronous() == nullptr)
		{
			C.Skip(FString::Printf(TEXT("%s not in the project"), TuningMannyPath));
			return;
		}
		UDataTable* Mobs = C.GameMode() != nullptr ? C.GameMode()->MobTable.Get() : nullptr;
		if (!C.Require(Mobs != nullptr && Mobs->GetRowNames().Num() > 0, TEXT("the game mode's Mob Table")))
		{
			return;
		}
		const FName MobId = Mobs->GetRowNames()[0];
		FCrossguardMobRow* Row = Mobs->FindRow<FCrossguardMobRow>(MobId, TEXT("ArtTest"), false);
		const FCrossguardMobRow Saved = *Row;
		Row->Mesh = Manny;
		Row->AnimClass.Reset();
		Row->MeshScale = 1.0f;
		Row->MeshYaw = -90.0f;
		// Far from the PIE player (out of aggro range): it stands.
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), MobId, FVector(0.0f, TuningSceneY + 3000.0f, 0.0f));
		const FString Json = FString::Printf(TEXT("[{\"id\": \"%s\", \"mesh\": \"%s\", \"meshScale\": 1.7, \"meshYaw\": 0, \"meshOffset\": [0, 0, 0.1]}]"),
			*MobId.ToString(), TuningMannyPath);
		const int32 Changed = CrossguardArtReload::ApplyMobArt(Mobs, Json);
		const int32 Refreshed = CrossguardArtReload::RefreshWorld(C.World());
		const FCrossguardMobRow Reloaded = *Row;
		*Row = Saved;
		if (!C.Require(Monster != nullptr, TEXT("monster spawned")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("one mob row reloaded"), Changed, 1);
		C.Test.TestTrue(TEXT("row: meshScale, meshYaw, meshOffset in cm"), FMath::IsNearlyEqual(Reloaded.MeshScale, 1.7f)
			&& FMath::IsNearlyEqual(Reloaded.MeshYaw, 0.0f) && Reloaded.MeshOffset.Equals(FVector(0.0f, 0.0f, 10.0f), 0.01f));
		C.Test.TestTrue(TEXT("monsters refreshed"), Refreshed > 0);
		C.Test.TestTrue(TEXT("the spawned monster's mesh scale"), Monster->GetMesh()->GetRelativeScale3D().Equals(FVector(1.7f), 0.01f));
		C.Test.TestEqual(TEXT("the spawned monster's mesh yaw"), static_cast<float>(Monster->GetMesh()->GetRelativeRotation().Yaw), 0.0f, 0.5f);
		C.Test.TestTrue(TEXT("replicated body visual updated"), FMath::IsNearlyEqual(Monster->BodyVisual.MeshScale, 1.7f));
		C.Test.TestEqual(TEXT("not a JSON array: -1"), CrossguardArtReload::ApplyMobArt(Mobs, TEXT("{}")), -1);
	});
	Scenario.Step(TEXT("A weapon's relativeTransform changes in the JSON: the held mesh is rebuilt"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, TuningSceneY, 0.0f));
		UDataTable* Weapons = Player != nullptr ? Player->GetEquipmentComponent()->WeaponTable.Get() : nullptr;
		FCrossguardWeaponRow* Row = Weapons != nullptr ? Weapons->FindRow<FCrossguardWeaponRow>(TEXT("sword_one_handed"), TEXT("ArtTest"), false) : nullptr;
		if (!C.Require(Row != nullptr && Player->GetWeaponVisuals() != nullptr, TEXT("a player with the sword's weapon row and weapon visuals")))
		{
			return;
		}
		const FCrossguardWeaponRow Saved = *Row;
		Row->Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TuningCylinderPath));
		Row->MeshScale = FVector::OneVector;
		const bool bEquipped = C.Equip(Player, TEXT("sword_one_handed"));
		const FString Json = FString::Printf(TEXT("[{\"id\": \"sword_one_handed\", \"mesh\": \"%s\", \"relativeTransform\": {\"rotation\": [0, 0, 30], \"scale\": 2}}]"),
			TuningCylinderPath);
		const int32 Changed = CrossguardArtReload::ApplyWeaponArt(Weapons, Json);
		CrossguardArtReload::RefreshWorld(C.World());
		*Row = Saved;
		if (!C.Require(bEquipped, TEXT("sword equipped")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("one weapon row reloaded"), Changed, 1);
		const UStaticMeshComponent* ThirdPerson = Player->GetWeaponVisuals()->GetWeaponMesh(ECrossguardEquipmentSlot::MainHand, false);
		if (C.Require(ThirdPerson != nullptr, TEXT("the third-person sword mesh")))
		{
			C.Test.TestTrue(TEXT("rebuilt with the new scale"), ThirdPerson->GetRelativeScale3D().Equals(FVector(2.0f), 0.001f));
			C.Test.TestEqual(TEXT("rebuilt with the new roll"), static_cast<float>(ThirdPerson->GetRelativeRotation().Roll), 30.0f, 0.5f);
		}
	});
	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
