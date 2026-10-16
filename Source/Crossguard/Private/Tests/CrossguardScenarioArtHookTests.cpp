// Scenario tests: the art hooks (docs/art-placeholder-plan.md, "How the art hooks into the code").
// Held weapon meshes from the weapon table (first- and third-person copies, holster and draw), the
// placeholder swing, OnAttackStarted / OnAttackHit / OnDamageTaken, a monster's attack cue, and mob
// and minion meshes from data. Framework: CrossguardScenario.h, docs/editor-tests.md.
//
// The data has no art yet, so the tests put engine assets in for the test: a weapon row's Mesh is set
// in memory just for the equip (the component keeps what it built) and put back in the same step; a
// mob row's mesh likewise around the spawn.

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Camera/CameraComponent.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMinion.h"
#include "Character/CrossguardMobRow.h"
#include "Character/CrossguardMonster.h"
#include "Character/CrossguardWeaponVisualsComponent.h"
#include "Combat/CrossguardCosmetics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Game/CrossguardGameMode.h"
#include "Items/CrossguardEquipmentComponent.h"

namespace CrossguardScenarioArtHooks
{
	using FContext = CrossguardScenario::FScenarioContext;

	// Engine and project assets that exist in every checkout.
	const TCHAR* const ArtCylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const ArtMannyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");

	// Keep the scenes apart from other tests' and the PIE player at (-30 m, -30 m).
	constexpr float ArtSceneY = 1500.0f;

	TSoftObjectPtr<UStaticMesh> ArtCylinder()
	{
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(ArtCylinderPath));
	}

	// Equips WeaponId in Slot with the row's Mesh set to the engine cylinder for the equip only.
	bool ArtEquipWithMesh(FContext& C, ACrossguardCharacter* Character, FName WeaponId, ECrossguardEquipmentSlot Slot)
	{
		UDataTable* Table = Character->GetEquipmentComponent()->WeaponTable;
		FCrossguardWeaponRow* Row = Table != nullptr ? Table->FindRow<FCrossguardWeaponRow>(WeaponId, TEXT("ArtTest"), false) : nullptr;
		if (!C.Require(Row != nullptr, FString::Printf(TEXT("weapon row '%s' in the character's Weapon Table"), *WeaponId.ToString())))
		{
			return false;
		}
		const TSoftObjectPtr<UStaticMesh> Saved = Row->Mesh;
		Row->Mesh = ArtCylinder();
		const bool bEquipped = C.Equip(Character, WeaponId, Slot);
		Row->Mesh = Saved;
		return bEquipped;
	}

	// What the native art delegates reported.
	struct FArtRecord
	{
		int32 Started = 0;
		FCrossguardAttackCue LastCue;
		int32 Hits = 0;
		TWeakObjectPtr<AActor> LastHitTarget;
		int32 Damaged = 0;
		float LastAmount = 0.0f;
		TWeakObjectPtr<AActor> LastSource;
		bool bLastBlocked = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioArtWeaponMeshes, "Crossguard.Scenario.Art.WeaponMeshes", CROSSGUARD_SCENARIO_FLAGS)
bool FCrossguardScenarioArtWeaponMeshes::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioArtHooks;
	FScenario Scenario(*this);
	Scenario.Step(TEXT("Equip a sword and a shield with meshes"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, ArtSceneY, 0.0f));
		if (!C.Require(Player != nullptr && Player->GetWeaponVisuals() != nullptr, TEXT("player with a weapon visuals component")))
		{
			return;
		}
		if (!ArtEquipWithMesh(C, Player, TEXT("sword_one_handed"), ECrossguardEquipmentSlot::MainHand)
			|| !ArtEquipWithMesh(C, Player, TEXT("shield_one_handed"), ECrossguardEquipmentSlot::OffHand))
		{
			return;
		}
		const UCrossguardWeaponVisualsComponent* Visuals = Player->GetWeaponVisuals();
		for (const ECrossguardEquipmentSlot Slot : {ECrossguardEquipmentSlot::MainHand, ECrossguardEquipmentSlot::OffHand})
		{
			const TCHAR* Hand = Slot == ECrossguardEquipmentSlot::MainHand ? TEXT("main hand") : TEXT("off hand");
			const UStaticMeshComponent* FirstPerson = Visuals->GetWeaponMesh(Slot, true);
			const UStaticMeshComponent* ThirdPerson = Visuals->GetWeaponMesh(Slot, false);
			if (!C.Require(FirstPerson != nullptr && ThirdPerson != nullptr, FString::Printf(TEXT("%s: first- and third-person meshes made"), Hand)))
			{
				return;
			}
			C.Test.TestTrue(*FString::Printf(TEXT("%s: the cylinder"), Hand), FirstPerson->GetStaticMesh() != nullptr
				&& FirstPerson->GetStaticMesh()->GetPathName() == ArtCylinderPath && ThirdPerson->GetStaticMesh() == FirstPerson->GetStaticMesh());
			C.Test.TestTrue(*FString::Printf(TEXT("%s: shown while drawn"), Hand), FirstPerson->IsVisible() && ThirdPerson->IsVisible());
			C.Test.TestTrue(*FString::Printf(TEXT("%s: first person only for its player"), Hand), FirstPerson->bOnlyOwnerSee != 0);
			C.Test.TestTrue(*FString::Printf(TEXT("%s: third person hidden from its player"), Hand), ThirdPerson->bOwnerNoSee != 0);
			C.Test.TestTrue(*FString::Printf(TEXT("%s: third person on the body"), Hand), ThirdPerson->GetAttachParent() == Player->GetMesh());
			const USceneComponent* Parent = FirstPerson->GetAttachParent();
			C.Test.TestTrue(*FString::Printf(TEXT("%s: first person on the arms or the camera"), Hand),
				Parent != nullptr && (Parent->IsA<UCameraComponent>() || Parent->ComponentHasTag(Visuals->FirstPersonArmsTag)));
			C.Test.TestEqual(*FString::Printf(TEXT("%s: no collision"), Hand), static_cast<int32>(ThirdPerson->GetCollisionEnabled()),
				static_cast<int32>(ECollisionEnabled::NoCollision));
		}
	});
	Scenario.Step(TEXT("Holster: hidden"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		Player->GetEquipmentComponent()->HolsterWeapons();
		const UCrossguardWeaponVisualsComponent* Visuals = Player->GetWeaponVisuals();
		const UStaticMeshComponent* FirstPerson = Visuals->GetWeaponMesh(ECrossguardEquipmentSlot::MainHand, true);
		const UStaticMeshComponent* ThirdPerson = Visuals->GetWeaponMesh(ECrossguardEquipmentSlot::MainHand, false);
		if (!C.Require(FirstPerson != nullptr && ThirdPerson != nullptr, TEXT("meshes kept while holstered")))
		{
			return;
		}
		C.Test.TestFalse(TEXT("holstered: hidden"), FirstPerson->IsVisible() || ThirdPerson->IsVisible());
		Player->GetEquipmentComponent()->RequestSwitchWeaponSet(0);
		const UStaticMeshComponent* Shield = Visuals->GetWeaponMesh(ECrossguardEquipmentSlot::OffHand, false);
		C.Test.TestFalse(TEXT("still hidden while drawing"), FirstPerson->IsVisible() || (Shield != nullptr && Shield->IsVisible()));
	});
	Scenario.WaitUntil(TEXT("Drawn again"), 5.0f, [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		return Player == nullptr || Player->GetEquipmentComponent()->AreWeaponsReady();
	});
	Scenario.Step(TEXT("Drawn: shown; unequipped: gone; a weapon without a mesh shows nothing"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Player == nullptr)
		{
			return;
		}
		const UCrossguardWeaponVisualsComponent* Visuals = Player->GetWeaponVisuals();
		const UStaticMeshComponent* FirstPerson = Visuals->GetWeaponMesh(ECrossguardEquipmentSlot::MainHand, true);
		C.Test.TestTrue(TEXT("drawn: shown again"), FirstPerson != nullptr && FirstPerson->IsVisible());
		C.Unequip(Player, ECrossguardEquipmentSlot::OffHand);
		C.Test.TestNull(TEXT("unequipped shield: no mesh"), Visuals->GetWeaponMesh(ECrossguardEquipmentSlot::OffHand, false));
		// The data's wand has no mesh (Data/Items/Weapons.json): nothing, as before art.
		C.Equip(Player, TEXT("wand_one_handed"));
		C.Test.TestNull(TEXT("a weapon without a mesh: nothing (first person)"), Visuals->GetWeaponMesh(ECrossguardEquipmentSlot::MainHand, true));
		C.Test.TestNull(TEXT("a weapon without a mesh: nothing (third person)"), Visuals->GetWeaponMesh(ECrossguardEquipmentSlot::MainHand, false));
	});
	Scenario.Run();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioArtAttackEvents, "Crossguard.Scenario.Art.AttackAndDamageEvents", CROSSGUARD_SCENARIO_FLAGS)
bool FCrossguardScenarioArtAttackEvents::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioArtHooks;
	// The player's attack events and the dummy's damage events, recorded by the native delegates.
	const TSharedRef<FArtRecord> PlayerRecord = MakeShared<FArtRecord>();
	const TSharedRef<FArtRecord> DummyRecord = MakeShared<FArtRecord>();
	FScenario Scenario(*this);
	Scenario.Step(TEXT("Player with a sword (mesh by override) facing a dummy"), [PlayerRecord, DummyRecord](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, ArtSceneY, 0.0f), 0.0f);
		ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150.0f, ArtSceneY, 0.0f));
		if (!C.Require(Player != nullptr && Dummy != nullptr && Player->GetWeaponVisuals() != nullptr, TEXT("player and dummy spawned")))
		{
			return;
		}
		// MeshOverrides: a Blueprint-only mesh for a weapon id, over the table.
		Player->GetWeaponVisuals()->MeshOverrides.Add(TEXT("sword_one_handed"), ArtCylinder());
		Player->GetWeaponVisuals()->bPlaceholderSwing = true;
		if (!C.Equip(Player, TEXT("sword_one_handed")))
		{
			return;
		}
		C.Test.TestNotNull(TEXT("override mesh shown"), Player->GetWeaponVisuals()->GetWeaponMesh(ECrossguardEquipmentSlot::MainHand, true));
		C.Aim(Player, Dummy);
		Player->OnAttackStartedNative.AddLambda([PlayerRecord](ACrossguardCharacter*, const FCrossguardAttackCue& Cue)
		{
			++PlayerRecord->Started;
			PlayerRecord->LastCue = Cue;
		});
		Player->OnAttackHitNative.AddLambda([PlayerRecord](ACrossguardCharacter*, FVector, AActor* Target)
		{
			++PlayerRecord->Hits;
			PlayerRecord->LastHitTarget = Target;
		});
		Dummy->OnDamageTakenNative.AddLambda([DummyRecord](ACrossguardCharacter*, float Amount, AActor* Source, FVector, bool bBlocked)
		{
			++DummyRecord->Damaged;
			DummyRecord->LastAmount = Amount;
			DummyRecord->LastSource = Source;
			DummyRecord->bLastBlocked = bBlocked;
		});
	});
	Scenario.Wait(0.3f);
	Scenario.Step(TEXT("Swing"), [PlayerRecord](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		if (Player == nullptr || !C.Require(C.BasicAttack(Player), TEXT("the swing started")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("OnAttackStarted fired once"), PlayerRecord->Started, 1);
		C.Test.TestTrue(TEXT("melee cue"), PlayerRecord->LastCue.Kind == ECrossguardAttackKind::Melee);
		C.Test.TestTrue(TEXT("with the sword"), PlayerRecord->LastCue.bHasWeapon && PlayerRecord->LastCue.WeaponType == ECrossguardWeaponType::Sword);
		C.Test.TestTrue(TEXT("lasting the attack interval"), PlayerRecord->LastCue.Duration > 0.0f);
		C.Test.TestTrue(TEXT("placeholder swing moving the first-person sword"), Player->GetWeaponVisuals()->IsSwinging());
	});
	// Past the placeholder swing (at most 0.8 s) too.
	Scenario.Wait(1.0f);
	Scenario.Step(TEXT("Hit landed; direct damage"), [PlayerRecord, DummyRecord](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		ACrossguardCharacter* Dummy = C.Get<ACrossguardCharacter>(TEXT("Dummy"));
		if (Player == nullptr || Dummy == nullptr)
		{
			return;
		}
		C.Test.TestEqual(TEXT("dummy's OnDamageTaken fired"), DummyRecord->Damaged, 1);
		C.Test.TestTrue(TEXT("damage > 0 from the player, not blocked"), DummyRecord->LastAmount > 0.0f
			&& DummyRecord->LastSource.Get() == Player && !DummyRecord->bLastBlocked);
		C.Test.TestEqual(TEXT("player's OnAttackHit fired"), PlayerRecord->Hits, 1);
		C.Test.TestTrue(TEXT("on the dummy"), PlayerRecord->LastHitTarget.Get() == Dummy);
		C.Test.TestFalse(TEXT("placeholder swing over"), Player->GetWeaponVisuals()->IsSwinging());
		const int32 CountBefore = Dummy->GetDamageTakenCount();
		C.Damage(Player, Dummy, 5.0f);
		C.Test.TestEqual(TEXT("a damage-path hit also fires OnDamageTaken"), Dummy->GetDamageTakenCount(), CountBefore + 1);
		C.Test.TestEqual(TEXT("and the delegate"), DummyRecord->Damaged, 2);
	});
	Scenario.Run();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioArtMonsterAttack, "Crossguard.Scenario.Art.MonsterAttackEvent", CROSSGUARD_SCENARIO_FLAGS)
bool FCrossguardScenarioArtMonsterAttack::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioArtHooks;
	FScenario Scenario(*this);
	Scenario.Step(TEXT("A monster next to a player"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0.0f, ArtSceneY, 0.0f), 0.0f);
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), NAME_None, FVector(150.0f, ArtSceneY, 0.0f), 180.0f);
		C.Require(Player != nullptr && Monster != nullptr, TEXT("player and monster spawned"));
	});
	Scenario.WaitUntil(TEXT("The monster's attack started"), 10.0f, [](FScenarioContext& C)
	{
		const ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		return Monster == nullptr || Monster->GetAttackStartedCount() > 0;
	});
	Scenario.Step(TEXT("The player took its hit"), [](FScenarioContext& C)
	{
		const ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
		C.Test.TestTrue(TEXT("player's OnDamageTaken fired"), Player != nullptr && Player->GetDamageTakenCount() > 0);
	});
	Scenario.Run();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioArtMobMesh, "Crossguard.Scenario.Art.MobMeshFromData", CROSSGUARD_SCENARIO_FLAGS)
bool FCrossguardScenarioArtMobMesh::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioArtHooks;
	FScenario Scenario(*this);
	Scenario.Step(TEXT("A mob row with a mesh; a minion with one"), [](FScenarioContext& C)
	{
		const TSoftObjectPtr<USkeletalMesh> Manny{FSoftObjectPath(ArtMannyPath)};
		USkeletalMesh* MannyMesh = Manny.LoadSynchronous();
		if (MannyMesh == nullptr)
		{
			C.Skip(FString::Printf(TEXT("%s not in the project"), ArtMannyPath));
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
		Row->MeshScale = 1.5f;
		Row->MeshOffset = FVector(0.0f, 0.0f, 10.0f);
		ACrossguardMonster* Monster = C.SpawnMonster(TEXT("Monster"), MobId, FVector(0.0f, ArtSceneY, 0.0f));
		*Row = Saved;
		if (!C.Require(Monster != nullptr, TEXT("monster spawned")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("monster: the row's mesh"), Monster->GetMesh()->GetSkeletalMeshAsset() == MannyMesh);
		C.Test.TestFalse(TEXT("monster: placeholder cylinder hidden"), Monster->GetPlaceholderMesh()->IsVisible());
		C.Test.TestTrue(TEXT("monster: mesh scale"), Monster->GetMesh()->GetRelativeScale3D().Equals(FVector(1.5f), 0.01f));
		C.Test.TestTrue(TEXT("monster: replicated body visual set"), Monster->BodyVisual.Mesh == Manny);

		ACrossguardCharacter* Summoner = C.SpawnPlayer(TEXT("Summoner"), FVector(-400.0f, ArtSceneY, 0.0f));
		ACrossguardMinion* Minion = C.SpawnMinion(TEXT("Minion"), Summoner, FVector(-250.0f, ArtSceneY, 0.0f), nullptr,
			[&Manny](ACrossguardMinion& Spawned) { Spawned.BodyVisual.Mesh = Manny; });
		if (C.Require(Minion != nullptr, TEXT("minion spawned")))
		{
			C.Test.TestTrue(TEXT("minion: its mesh"), Minion->GetMesh()->GetSkeletalMeshAsset() == MannyMesh);
			C.Test.TestFalse(TEXT("minion: placeholder hidden"), Minion->GetPlaceholderMesh()->IsVisible());
		}
	});
	Scenario.Run();
	return true;
}

// A mob row's state animations (Data/Mobs/Mobs.json `animations`) play without an Anim Blueprint:
// idle while it stands, the attack when an attack starts, death once it dies.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioArtMobStateAnims, "Crossguard.Scenario.Art.MobStateAnimations", CROSSGUARD_SCENARIO_FLAGS)
bool FCrossguardScenarioArtMobStateAnims::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioArtHooks;
	FScenario Scenario(*this);
	Scenario.Step(TEXT("A mob with idle, attack and death animations from data stands"), [](FScenarioContext& C)
	{
		UDataTable* Mobs = C.GameMode() != nullptr ? C.GameMode()->MobTable.Get() : nullptr;
		if (!C.Require(Mobs != nullptr, TEXT("the game mode's Mob Table")))
		{
			return;
		}
		FName MobId;
		for (const FName RowName : Mobs->GetRowNames())
		{
			const FCrossguardMobRow* Row = Mobs->FindRow<FCrossguardMobRow>(RowName, TEXT("ArtTest"), false);
			if (Row != nullptr && Row->AnimClass.IsNull() && !Row->IdleAnim.IsNull() && !Row->AttackAnim.IsNull() && !Row->DeathAnim.IsNull()
				&& Row->IdleAnim.LoadSynchronous() != nullptr)
			{
				MobId = RowName;
				break;
			}
		}
		if (MobId.IsNone())
		{
			C.Skip(TEXT("no mob row with idle, attack and death animations (or the art is not in the project)"));
			return;
		}
		// Far from the PIE player (out of aggro range): it stands.
		C.SpawnMonster(TEXT("Monster"), MobId, FVector(0.0f, -5000.0f, 0.0f));
	});
	Scenario.Wait(0.3f);
	Scenario.Step(TEXT("Idle, then an attack starts: the attack animation"), [](FScenarioContext& C)
	{
		ACrossguardMonster* Monster = C.Get<ACrossguardMonster>(TEXT("Monster"));
		const FCrossguardMobRow* Row = Monster != nullptr ? C.GameMode()->MobTable->FindRow<FCrossguardMobRow>(Monster->MobId, TEXT("ArtTest"), false) : nullptr;
		if (!C.Require(Monster != nullptr && Row != nullptr, TEXT("monster and its row")))
		{
			return;
		}
		C.Test.TestTrue(TEXT("standing: idle"), Monster->GetPlayingBodyAnim() == Row->IdleAnim.Get());
		FCrossguardAttackCue Cue;
		Cue.Kind = ECrossguardAttackKind::Melee;
		Monster->OnAttackStartedNative.Broadcast(Monster, Cue);
		C.Test.TestTrue(TEXT("attacking: the attack animation"), Monster->GetPlayingBodyAnim() == Row->AttackAnim.Get());
		C.Kill(Monster);
		Monster->UpdateBodyAnimation();
		C.Test.TestTrue(TEXT("dead: the death animation"), Monster->GetPlayingBodyAnim() == Row->DeathAnim.Get());
	});
	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
