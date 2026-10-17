#include "Game/CrossguardArtReload.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Character/CrossguardCharacter.h"
#include "Character/CrossguardMobRow.h"
#include "Character/CrossguardMonster.h"
#include "Character/CrossguardWeaponVisualsComponent.h"
#include "Crossguard.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CrossguardGameMode.h"
#include "HAL/IConsoleManager.h"
#include "Items/CrossguardEquipmentComponent.h"
#include "Items/CrossguardInventoryComponent.h"
#include "Items/CrossguardItemTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"

// Named, not anonymous (unity builds).
namespace CrossguardArtReloadLocal
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	bool ParseArray(const FString& JsonText, FJsonArray& OutRows)
	{
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), OutRows);
	}

	FString StringField(const FJsonObject& Object, const TCHAR* Name)
	{
		FString Value;
		Object.TryGetStringField(Name, Value);
		return Value;
	}

	double NumberField(const FJsonObject& Object, const TCHAR* Name, double Default)
	{
		double Value = Default;
		return Object.TryGetNumberField(Name, Value) ? Value : Default;
	}

	// [a, b, c]; Default when missing or malformed.
	FVector TripleField(const FJsonObject& Object, const TCHAR* Name, const FVector& Default)
	{
		const FJsonArray* Values = nullptr;
		if (!Object.TryGetArrayField(Name, Values) || Values == nullptr || Values->Num() != 3)
		{
			return Default;
		}
		FVector Result = FVector::ZeroVector;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			double Number = 0.0;
			if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetNumber(Number))
			{
				return Default;
			}
			Result[Index] = Number;
		}
		return Result;
	}

	// [pitch, yaw, roll] degrees.
	FRotator RotatorField(const FJsonObject& Object, const TCHAR* Name)
	{
		const FVector Values = TripleField(Object, Name, FVector::ZeroVector);
		return FRotator(Values.X, Values.Y, Values.Z);
	}

	// A number (uniform) or [x, y, z]; 1 when missing.
	FVector ScaleField(const FJsonObject& Object, const TCHAR* Name)
	{
		double Uniform = 1.0;
		if (Object.TryGetNumberField(Name, Uniform))
		{
			return FVector(Uniform);
		}
		return TripleField(Object, Name, FVector::OneVector);
	}

	template <typename T>
	TSoftObjectPtr<T> AssetField(const FJsonObject& Object, const TCHAR* Name)
	{
		const FString Path = StringField(Object, Name);
		return Path.IsEmpty() ? TSoftObjectPtr<T>() : TSoftObjectPtr<T>(FSoftObjectPath(Path));
	}

	// Calls Apply(Row, Object) for each object of JsonText whose "id" is a row of Table.
	template <typename TRow>
	int32 ForEachRow(UDataTable* Table, const FString& JsonText, TFunctionRef<void(TRow&, const FJsonObject&)> Apply)
	{
		FJsonArray Rows;
		if (Table == nullptr || Table->GetRowStruct() == nullptr || !Table->GetRowStruct()->IsChildOf(TRow::StaticStruct())
			|| !ParseArray(JsonText, Rows))
		{
			return -1;
		}
		int32 Changed = 0;
		for (const TSharedPtr<FJsonValue>& Value : Rows)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr || !Object->IsValid())
			{
				continue;
			}
			const FString Id = StringField(**Object, TEXT("id"));
			TRow* Row = Id.IsEmpty() ? nullptr : Table->FindRow<TRow>(FName(*Id), TEXT("CgReloadArtData"), false);
			if (Row != nullptr)
			{
				Apply(*Row, **Object);
				++Changed;
			}
		}
		return Changed;
	}

	void AddTable(TSet<UDataTable*>& Tables, UDataTable* Table)
	{
		if (Table != nullptr)
		{
			Tables.Add(Table);
		}
	}

	bool IsGameWorld(const UWorld* World)
	{
		return World != nullptr && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game);
	}

	// Reads Data/<RelativePath>; empty with a warning when the file is missing (a packaged build).
	FString LoadDataFile(const TCHAR* RelativePath)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), RelativePath);
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogCrossguard, Warning, TEXT("CgReloadArtData: %s not found (the JSON sources are only in a checkout)"), *Path);
			Text.Reset();
		}
		return Text;
	}

	int32 ApplyToTables(const TSet<UDataTable*>& Tables, const FString& JsonText, int32 (*Apply)(UDataTable*, const FString&))
	{
		int32 Total = 0;
		if (JsonText.IsEmpty())
		{
			return Total;
		}
		for (UDataTable* Table : Tables)
		{
			const int32 Changed = Apply(Table, JsonText);
			if (Changed < 0)
			{
				UE_LOG(LogCrossguard, Warning, TEXT("CgReloadArtData: could not apply to %s (bad JSON or another row struct)"), *GetNameSafe(Table));
				continue;
			}
			Total += Changed;
		}
		return Total;
	}

	void RunCommand()
	{
		CrossguardArtReload::ReloadAll();
	}

	FAutoConsoleCommand ReloadArtDataCommand(
		TEXT("CgReloadArtData"),
		TEXT("Crossguard (not in Shipping): re-read the art fields of Data/Items/Weapons.json, Data/Items/Items.json and "
			"Data/Mobs/Mobs.json and re-apply them to spawned monsters and held weapons (tuning without restarts)."),
		FConsoleCommandDelegate::CreateStatic(&RunCommand));
}

int32 CrossguardArtReload::ApplyMobArt(UDataTable* Table, const FString& JsonText)
{
	using namespace CrossguardArtReloadLocal;
	return ForEachRow<FCrossguardMobRow>(Table, JsonText, [](FCrossguardMobRow& Row, const FJsonObject& Mob)
	{
		Row.Mesh = AssetField<USkeletalMesh>(Mob, TEXT("mesh"));
		const FString AnimClassPath = StringField(Mob, TEXT("animBlueprint"));
		Row.AnimClass = AnimClassPath.IsEmpty() ? TSoftClassPtr<UAnimInstance>() : TSoftClassPtr<UAnimInstance>(FSoftObjectPath(AnimClassPath));
		Row.MeshScale = static_cast<float>(NumberField(Mob, TEXT("meshScale"), 1.0));
		// Meters in the source.
		Row.MeshOffset = TripleField(Mob, TEXT("meshOffset"), FVector::ZeroVector) * 100.0;
		Row.MeshYaw = static_cast<float>(NumberField(Mob, TEXT("meshYaw"), -90.0));
		const TSharedPtr<FJsonObject>* Animations = nullptr;
		const FJsonObject Empty{};
		const FJsonObject& Anims = Mob.TryGetObjectField(TEXT("animations"), Animations) && Animations != nullptr && Animations->IsValid()
			? **Animations : Empty;
		Row.IdleAnim = AssetField<UAnimSequenceBase>(Anims, TEXT("idle"));
		Row.MoveAnim = AssetField<UAnimSequenceBase>(Anims, TEXT("move"));
		Row.AttackAnim = AssetField<UAnimSequenceBase>(Anims, TEXT("attack"));
		Row.DeathAnim = AssetField<UAnimSequenceBase>(Anims, TEXT("death"));
		Row.DeathMode = StringField(Mob, TEXT("deathMode")) == TEXT("animation") ? ECrossguardDeathMode::Animation : ECrossguardDeathMode::Ragdoll;
	});
}

int32 CrossguardArtReload::ApplyItemArt(UDataTable* Table, const FString& JsonText)
{
	using namespace CrossguardArtReloadLocal;
	return ForEachRow<FCrossguardItemRow>(Table, JsonText, [](FCrossguardItemRow& Row, const FJsonObject& Item)
	{
		Row.ProjectileMesh = AssetField<UStaticMesh>(Item, TEXT("projectileMesh"));
		Row.ProjectileRotation = RotatorField(Item, TEXT("projectileRotation"));
		Row.ProjectileScale = ScaleField(Item, TEXT("projectileScale"));
		Row.TrailVfx = AssetField<UNiagaraSystem>(Item, TEXT("trailVfx"));
		Row.ImpactVfx = AssetField<UNiagaraSystem>(Item, TEXT("impactVfx"));
		Row.ImpactSound = AssetField<USoundBase>(Item, TEXT("impactSound"));
	});
}

int32 CrossguardArtReload::ApplyWeaponArt(UDataTable* Table, const FString& JsonText)
{
	using namespace CrossguardArtReloadLocal;
	return ForEachRow<FCrossguardWeaponRow>(Table, JsonText, [](FCrossguardWeaponRow& Row, const FJsonObject& Weapon)
	{
		const FJsonObject Empty{};
		Row.Mesh = AssetField<UStaticMesh>(Weapon, TEXT("mesh"));
		const FString Socket = StringField(Weapon, TEXT("attachSocket"));
		Row.AttachSocket = Socket.IsEmpty() ? NAME_None : FName(*Socket);
		const TSharedPtr<FJsonObject>* TransformObject = nullptr;
		const FJsonObject& Transform = Weapon.TryGetObjectField(TEXT("relativeTransform"), TransformObject) && TransformObject != nullptr
			&& TransformObject->IsValid() ? **TransformObject : Empty;
		Row.MeshOffset = TripleField(Transform, TEXT("location"), FVector::ZeroVector);
		Row.MeshRotation = RotatorField(Transform, TEXT("rotation"));
		Row.MeshScale = ScaleField(Transform, TEXT("scale"));
		const TSharedPtr<FJsonObject>* ProjectileObject = nullptr;
		const FJsonObject& Projectile = Weapon.TryGetObjectField(TEXT("projectile"), ProjectileObject) && ProjectileObject != nullptr
			&& ProjectileObject->IsValid() ? **ProjectileObject : Empty;
		Row.ProjectileMesh = AssetField<UStaticMesh>(Projectile, TEXT("mesh"));
		Row.ProjectileTrailVfx = AssetField<UNiagaraSystem>(Projectile, TEXT("trailVfx"));
		Row.ProjectileImpactVfx = AssetField<UNiagaraSystem>(Projectile, TEXT("impactVfx"));
		Row.ProjectileImpactSound = AssetField<USoundBase>(Projectile, TEXT("impactSound"));
	});
}

int32 CrossguardArtReload::RefreshWorld(UWorld* World)
{
	if (World == nullptr)
	{
		return 0;
	}
	int32 Refreshed = 0;
	// Monsters: the server re-applies the row; BodyVisual replicates to the clients.
	const ACrossguardGameMode* GameMode = World->GetAuthGameMode<ACrossguardGameMode>();
	UDataTable* MobTable = GameMode != nullptr ? GameMode->MobTable.Get() : nullptr;
	if (MobTable != nullptr)
	{
		for (TActorIterator<ACrossguardMonster> It(World); It; ++It)
		{
			ACrossguardMonster* Monster = *It;
			const FCrossguardMobRow* Row = Monster->HasAuthority() && Monster->IsAlive() && !Monster->MobId.IsNone()
				? MobTable->FindRow<FCrossguardMobRow>(Monster->MobId, TEXT("CgReloadArtData"), false) : nullptr;
			if (Row != nullptr && !Row->Mesh.IsNull())
			{
				Monster->ApplyMobArt(*Row);
				++Refreshed;
			}
		}
	}
	// Held weapons: built on every machine that renders them.
	for (TActorIterator<ACrossguardCharacter> It(World); It; ++It)
	{
		if (UCrossguardWeaponVisualsComponent* Visuals = It->GetWeaponVisuals())
		{
			Visuals->RebuildWeapons();
			++Refreshed;
		}
	}
	return Refreshed;
}

FString CrossguardArtReload::ReloadAll()
{
	using namespace CrossguardArtReloadLocal;
	TArray<UWorld*> Worlds;
	TSet<UDataTable*> MobTables;
	TSet<UDataTable*> ItemTables;
	TSet<UDataTable*> WeaponTables;
	if (GEngine != nullptr)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!IsGameWorld(World))
			{
				continue;
			}
			Worlds.Add(World);
			if (const ACrossguardGameMode* GameMode = World->GetAuthGameMode<ACrossguardGameMode>())
			{
				AddTable(MobTables, GameMode->MobTable.Get());
				AddTable(WeaponTables, GameMode->Loot.WeaponTable.Get());
				AddTable(ItemTables, GameMode->Loot.ItemTable.Get());
			}
			for (TActorIterator<ACrossguardCharacter> It(World); It; ++It)
			{
				if (const UCrossguardEquipmentComponent* Equipment = It->GetEquipmentComponent())
				{
					AddTable(WeaponTables, Equipment->WeaponTable.Get());
				}
				if (const UCrossguardInventoryComponent* Inventory = It->GetInventoryComponent())
				{
					AddTable(ItemTables, Inventory->ItemTable.Get());
				}
			}
		}
	}

	const int32 Mobs = ApplyToTables(MobTables, LoadDataFile(TEXT("Mobs/Mobs.json")), &CrossguardArtReload::ApplyMobArt);
	const int32 Items = ApplyToTables(ItemTables, LoadDataFile(TEXT("Items/Items.json")), &CrossguardArtReload::ApplyItemArt);
	const int32 Weapons = ApplyToTables(WeaponTables, LoadDataFile(TEXT("Items/Weapons.json")), &CrossguardArtReload::ApplyWeaponArt);
	int32 Refreshed = 0;
	for (UWorld* World : Worlds)
	{
		Refreshed += RefreshWorld(World);
	}
	const FString Summary = FString::Printf(TEXT("CgReloadArtData: %d mob, %d item, %d weapon rows reloaded; %d actors refreshed in %d worlds"),
		Mobs, Items, Weapons, Refreshed, Worlds.Num());
	UE_LOG(LogCrossguard, Display, TEXT("%s"), *Summary);
	return Summary;
}

#endif // !UE_BUILD_SHIPPING
