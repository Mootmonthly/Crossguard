#pragma once

// Scenario tests: the manual build checklist (docs/next-build-checklist.md) as editor automation
// tests that play the real game in Play-In-Editor. Reference and how-to: docs/editor-tests.md,
// "Scenario tests" and "Writing a scenario".
//
// What a scenario runs in
//   A fresh blank map (FAutomationEditorCommonUtils::CreateNewMap) with a 200 m x 200 m floor (top at
//   Z = 0, collision BlockAll / WorldStatic), a Player Start at (-30 m, -30 m) for the PIE player, and
//   the World Settings' Game Mode Override set to the project's game mode Blueprint (the Blueprint
//   whose class derives from ACrossguardGameMode, preferably BP_CrossguardGameMode). Characters are
//   spawned from the character Blueprint (the game mode's Default Pawn Class if it is a Blueprint of
//   ACrossguardCharacter, else the Blueprint named BP_CrossguardCharacter), so its Weapon / Item
//   tables and Skill Catalog are the ones the checklist sets. PIE runs as one standalone instance; the
//   scenario acts on the PIE world (standalone: it is the server). PIE always ends, also on failure or
//   timeout. Missing Blueprints fail the test with a message naming the asset.
//
// Writing one
//
//   IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyScenario, "Crossguard.Scenario.Area.Name", CROSSGUARD_SCENARIO_FLAGS)
//   bool FMyScenario::RunTest(const FString& Parameters)
//   {
//       using namespace CrossguardScenario;
//       FScenario Scenario(*this);
//       Scenario.Step(TEXT("Spawn"), [](FScenarioContext& C)
//       {
//           ACrossguardCharacter* Player = C.SpawnPlayer(TEXT("Player"), FVector(0, 1000, 0));
//           ACrossguardCharacter* Dummy = C.SpawnDummy(TEXT("Dummy"), FVector(150, 1000, 0));
//           if (!C.Require(Player != nullptr && Dummy != nullptr, TEXT("both spawned"))) return;
//           C.Equip(Player, TEXT("sword_one_handed"));
//           C.Aim(Player, Dummy);
//       });
//       Scenario.Wait(0.5f);
//       Scenario.Step(TEXT("Hit"), [](FScenarioContext& C)
//       {
//           ACrossguardCharacter* Player = C.Get<ACrossguardCharacter>(TEXT("Player"));
//           ...
//           C.Test.TestTrue(TEXT("Attack started"), C.BasicAttack(Player));
//       });
//       Scenario.Run();   // queues the latent command; RunTest returns right after
//       return true;
//   }
//
// Rules
//   - Steps run in order, one per frame, on the game thread, against the PIE world. Wait(Seconds)
//     waits game seconds (World time: real seconds at the default time dilation 1; see
//     TimeDilation). WaitUntil polls a condition each frame (or every PollSeconds) until it is true;
//     past its timeout the test fails and the remaining steps are skipped.
//   - Assert with C.Test.TestEqual / TestTrue / ... (soft: the scenario goes on). Use
//     `if (!C.Require(Cond, What)) return;` for things later steps depend on: Require (and Fail)
//     add an error and skip every remaining step. Skip(Why) stops with a warning (a missing optional
//     asset), the test still passes.
//   - Actors live in the PIE world: keep them by name (SpawnX(Name, ...) remembers them; Get<T>(Name)
//     finds them in later steps), never in raw pointers captured across steps. Numbers between steps:
//     SetNumber / Number.
//   - Positions: the floor top is Z = 0. Character spawn locations are points on the floor (the
//     capsule is placed above). Keep scenes within 50 m of the origin and away from the PIE player at
//     (-30 m, -30 m).
//   - One scenario per test, named Crossguard.Scenario.<Area>.<Name>, flags CROSSGUARD_SCENARIO_FLAGS.
//     Keep file-local helpers in a uniquely named namespace (unity builds).
//   - Log lines written while the scenario runs (any category) are captured: LogContains("blocked").
//     Engine and game warnings / errors logged during the test are also reported by the automation
//     framework itself (errors fail the test).
//
// Only in editor builds with automation tests (WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Items/CrossguardItemTypes.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtr.h"

class ACrossguardCharacter;
class ACrossguardGameMode;
class ACrossguardMonster;
class ACrossguardPlayerState;
struct FGameplayAttribute;

// Scenario tests run in the editor with Play-In-Editor. Listed under the Product filter. In UE 5.5+
// EAutomationTestFlags is an enum class with flag operators; the expression works either way.
#define CROSSGUARD_SCENARIO_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CrossguardScenario
{
	class FScenarioLogCapture;
	class FScenarioRunner;

	// Handed to every step: the PIE world, the game mode, the test (for TestEqual & co.), named
	// actors and the helpers. Everything here runs on the server (standalone PIE).
	class FScenarioContext
	{
	public:
		explicit FScenarioContext(FAutomationTestBase& InTest);
		~FScenarioContext();

		FScenarioContext(const FScenarioContext&) = delete;
		FScenarioContext& operator=(const FScenarioContext&) = delete;

		// The running test: C.Test.TestEqual(...), C.Test.AddInfo(...).
		FAutomationTestBase& Test;

		// --- Where it runs ----------------------------------------------------------------------

		// The PIE world (null once PIE has ended).
		UWorld* World() const { return WorldPtr.Get(); }
		ACrossguardGameMode* GameMode() const { return GameModePtr.Get(); }
		// The character Blueprint class SpawnPlayer / SpawnDummy use.
		UClass* CharacterClass() const { return CharacterClassPtr.Get(); }
		// The PIE player's character (player 1, with a real local player), standing at (-30 m, -30 m).
		// Also remembered as "LocalPlayer". May be null if its pawn failed to spawn.
		ACrossguardCharacter* LocalPlayer() const;
		// Game seconds since PIE started (World->GetTimeSeconds()).
		float Now() const;

		// --- Flow ---------------------------------------------------------------------------------

		// Adds an error and skips every remaining step (PIE still ends cleanly).
		void Fail(const FString& Why);
		// Fail(What) unless bCondition. Returns bCondition: `if (!C.Require(X, TEXT("..."))) return;`
		bool Require(bool bCondition, const FString& What);
		// Adds a warning and skips every remaining step; the test passes (e.g. optional data missing).
		void Skip(const FString& Why);
		bool IsStopped() const { return bStopped; }

		// --- Named actors and numbers between steps ----------------------------------------------

		void Remember(FName Name, AActor* Actor);
		// Null if unknown or destroyed.
		AActor* FindActor(FName Name) const;
		// FindActor cast to T; adds an error if it is missing or of another class.
		template <typename T>
		T* Get(FName Name)
		{
			T* Actor = Cast<T>(FindActor(Name));
			if (Actor == nullptr)
			{
				Test.AddError(FString::Printf(TEXT("Scenario actor '%s' is missing (destroyed, never spawned, or not a %s)"), *Name.ToString(),
					*T::StaticClass()->GetName()));
			}
			return Actor;
		}
		void SetNumber(FName Name, double Value) { Numbers.Add(Name, Value); }
		double Number(FName Name, double Default = 0.0) const;

		// --- Spawning -----------------------------------------------------------------------------
		//
		// Characters come from CharacterClass(). Unless bKeepDebugLoadout is set, their Blueprint's
		// Crossguard|Debug items, skills, inventory, revive delay, loot-takes-all and debug HUD are
		// cleared before they spawn, so a scenario starts from empty hands (Level, bUseDebugProgression
		// and the data tables stay as the Blueprint has them). FloorPoint: the point on the floor
		// under the character.

		// A real player: a PlayerController of the game mode's class (with its PlayerState and team via
		// InitNewPlayer, "?Name=<Name>" and "?Party=<Party>" if given), and a character it possesses
		// through HandleStartingNewPlayer -> RestartPlayer (so a raid map begins the raid for it).
		// IsPlayerControlled() is true (trades, player-kill XP); IsLocallyControlled() is true too
		// (standalone), but it has no local player, input, HUD or camera on screen. Same Party: same
		// team.
		ACrossguardCharacter* SpawnPlayer(FName Name, const FVector& FloorPoint, float Yaw = 0.0f, const FString& Party = FString());
		// A target like the checklist's placed dummy: the character Blueprint possessed by a plain
		// AAIController (it does nothing), so the server sets up its ability system. Not
		// player-controlled, no player state, no team; killing it gives BasicMonster XP.
		ACrossguardCharacter* SpawnDummy(FName Name, const FVector& FloorPoint, float Yaw = 180.0f);
		// An ACrossguardMonster with MobId (stats from the game mode's Mob Table at BeginPlay), possessed
		// by its monster controller: it will fight players and minions it sees within its aggro range.
		ACrossguardMonster* SpawnMonster(FName Name, FName MobId, const FVector& FloorPoint, float Yaw = 0.0f);
		// Any actor (Class: a subclass of T, default T). Configure runs before FinishSpawning (before
		// BeginPlay). Location is used as is (not put on the floor).
		template <typename T>
		T* SpawnActor(FName Name, const FVector& Location, float Yaw = 0.0f, TFunction<void(T&)> Configure = nullptr, UClass* Class = nullptr)
		{
			UClass* SpawnClass = Class != nullptr ? Class : T::StaticClass();
			if (!SpawnClass->IsChildOf(T::StaticClass()))
			{
				Fail(FString::Printf(TEXT("SpawnActor '%s': %s is not a %s"), *Name.ToString(), *SpawnClass->GetName(), *T::StaticClass()->GetName()));
				return nullptr;
			}
			return Cast<T>(SpawnActorInternal(Name, SpawnClass, Location, Yaw, [&Configure](AActor& Spawned)
			{
				if (Configure)
				{
					Configure(*CastChecked<T>(&Spawned));
				}
			}));
		}
		// Moves a character to FloorPoint facing Yaw (controller too).
		void Place(ACrossguardCharacter* Character, const FVector& FloorPoint, float Yaw);

		// Before spawning: keep the character Blueprint's Crossguard|Debug loadout.
		bool bKeepDebugLoadout = false;

		// --- Gear, items, skills ------------------------------------------------------------------

		// Equips a fresh weapon (no affixes) in the active weapon set, without the inventory (server
		// equip, like the Debug items). Equipping a two-handed weapon in the main hand first empties
		// the off hand. Errors (unknown weapon, Weapon Table unset, shield in the main hand) fail the
		// scenario. Returns success.
		bool Equip(ACrossguardCharacter* Character, FName WeaponId, ECrossguardEquipmentSlot Slot = ECrossguardEquipmentSlot::MainHand,
			ECrossguardRarity Rarity = ECrossguardRarity::Common);
		bool Unequip(ACrossguardCharacter* Character, ECrossguardEquipmentSlot Slot);
		// Adds Count of an item-table item (category from DT_Items, e.g. "healing_potion", "arrow",
		// "gold_coin") to the inventory. Returns how many did not fit (an unknown id adds an error and
		// returns Count).
		int32 GiveItem(ACrossguardCharacter* Character, FName ItemId, int32 Count = 1, ECrossguardRarity Rarity = ECrossguardRarity::Common);
		// The same with an explicit category (SkillGem: ItemId is the skill id).
		int32 GiveItemOfCategory(ACrossguardCharacter* Character, ECrossguardItemCategory Category, FName ItemId, int32 Count = 1,
			ECrossguardRarity Rarity = ECrossguardRarity::Common);
		// Equips these skills as the loadout (replacing it), as DebugSkills does: players learn them
		// first (LearnSkillForTesting); the loadout validates them against Skill Capacity. Without a
		// SkillCatalog on the character's LoadoutComponent, it falls back to granting the native C++
		// classes (CrossguardNativeSkills) straight on the ability system (no passives from data).
		// Returns success; failures add errors.
		bool GrantSkills(ACrossguardCharacter* Character, const TArray<FName>& SkillIds);

		// --- Acting -------------------------------------------------------------------------------

		// Turns body and controller to Yaw, level (pitch 0). Melee swings use the body's facing at once.
		// A raised guard faces the player's view (GetBaseAimRotation), which a PlayerController reads
		// from its camera manager, updated once a frame: Wait(0.2f) after turning a guard before it is
		// hit.
		void Face(ACrossguardCharacter* Character, float Yaw);
		// Turns the view from the eyes to the target's centre (pitch included) and the body to its yaw,
		// so aimed skills (fireball, heal, ...) and melee swings go at it. An AI controller (dummies)
		// keeps only the yaw.
		void Aim(ACrossguardCharacter* Character, const AActor* Target);
		void AimAt(ACrossguardCharacter* Character, const FVector& Point);
		// Left click: TryWeaponAttack. False while the previous attack recovers, drawing, stunned, ...
		bool BasicAttack(ACrossguardCharacter* Character);
		// Right click held: raises the guard (block with a shield, parry with a parrying weapon).
		// True if State.Guarding is on afterwards. LowerGuard releases it.
		bool RaiseGuard(ACrossguardCharacter* Character);
		void LowerGuard(ACrossguardCharacter* Character);
		// Activates a granted, non-passive skill by id (as E does with it selected). False if it is
		// not granted or cannot activate (cooldown, weapon, Muted, ...).
		bool ActivateSkill(ACrossguardCharacter* Character, FName SkillId);
		// A granted skill's CooldownTags are on the character.
		bool IsOnCooldown(const ACrossguardCharacter* Character, FName SkillId) const;
		// Lethal damage through the native damage effect, so death runs as in play (body, kill XP to
		// Killer's player). No Killer: the victim's own (no XP).
		void Kill(AActor* Victim, AActor* Killer = nullptr);

		// --- Reading state ------------------------------------------------------------------------

		// Attribute values from the actor's ability system; -1 (Health / MaxHealth) or 0 (Stat)
		// without one.
		float Health(const AActor* Actor) const;
		float MaxHealth(const AActor* Actor) const;
		float Stat(const AActor* Actor, const FGameplayAttribute& Attribute) const;
		bool HasTag(const AActor* Actor, const FGameplayTag& Tag) const;
		// By name, e.g. TEXT("State.Stunned"). False (with an error) for an unknown tag.
		bool HasTag(const AActor* Actor, const TCHAR* TagName) const;
		// State.Dead.
		bool IsDead(const AActor* Actor) const;
		ACrossguardPlayerState* PlayerState(const AActor* Actor) const;
		// The equipped weapon's AD or MD at its rarity (what a basic attack hits for before stats).
		float WeaponValue(const ACrossguardCharacter* Character, ECrossguardEquipmentSlot Slot = ECrossguardEquipmentSlot::MainHand) const;
		// What one hit from Source to Target should take off Target's Health right now, by the same
		// formula as UCrossguardDamageExecution with both characters' current stats:
		// (Base + AAD|AMD) x (1 + ADB|MDB x Coefficient) x (1 - AR|MR x (1 - AP|MP)) + True Damage,
		// Attack part x AttackShare. Ignores Eldritch Shield and the Health floor at 0.
		float ExpectedDamage(const AActor* Source, const AActor* Target, float AttackBase, float MagicBase = 0.0f, float Coefficient = 1.0f,
			float AttackShare = 1.0f) const;

		template <typename T>
		TArray<T*> FindAll() const
		{
			TArray<T*> Found;
			if (UWorld* PlayWorld = World())
			{
				for (TActorIterator<T> It(PlayWorld); It; ++It)
				{
					Found.Add(*It);
				}
			}
			return Found;
		}
		// The T closest to Point within Radius cm, or null.
		template <typename T>
		T* FindNearest(const FVector& Point, float Radius) const
		{
			T* Best = nullptr;
			double BestDistanceSquared = FMath::Square(static_cast<double>(Radius));
			if (UWorld* PlayWorld = World())
			{
				for (TActorIterator<T> It(PlayWorld); It; ++It)
				{
					const double DistanceSquared = FVector::DistSquared(It->GetActorLocation(), Point);
					if (DistanceSquared <= BestDistanceSquared)
					{
						Best = *It;
						BestDistanceSquared = DistanceSquared;
					}
				}
			}
			return Best;
		}

		// --- Game mode and time -------------------------------------------------------------------

		// The game mode's bIsRaid. On: every player not in a raid begins one (BeginRaid). Off: every
		// player in one ends it as a failure (EndRaid(false): raid XP lost), as on a hub map. Scenarios
		// start with it on (FScenario::Raid).
		void SetRaid(bool bRaid);
		// Global time dilation (World Settings, clamped by the engine to 0.0001..20). Game timers,
		// cooldowns, ability waits and Wait() all speed up together.
		void SetTimeDilation(float Dilation);

		// --- Log ----------------------------------------------------------------------------------

		// Lines logged since the scenario started (or the last ClearLog), as "Category: message".
		bool LogContains(const FString& Text) const;
		int32 LogCount(const FString& Text) const;
		TArray<FString> LogLines() const;
		void ClearLog();

	private:
		friend class FScenarioRunner;

		AActor* SpawnActorInternal(FName Name, UClass* Class, const FVector& Location, float Yaw, TFunctionRef<void(AActor&)> Configure);
		// Deferred spawn of a Crossguard character above FloorPoint; bAIController: possessed by its
		// AI controller on spawn.
		ACrossguardCharacter* SpawnCharacter(FName Name, UClass* Class, const FVector& FloorPoint, float Yaw, bool bAIController,
			TFunctionRef<void(ACrossguardCharacter&)> Configure);

		TWeakObjectPtr<UWorld> WorldPtr;
		TWeakObjectPtr<ACrossguardGameMode> GameModePtr;
		TWeakObjectPtr<UClass> CharacterClassPtr;
		TMap<FName, TWeakObjectPtr<AActor>> Actors;
		TMap<FName, double> Numbers;
		TSharedPtr<FScenarioLogCapture> LogCapture;
		bool bStopped = false;
	};

	// Builds a scenario in RunTest, then Run() queues it as a latent command.
	class FScenario
	{
	public:
		explicit FScenario(FAutomationTestBase& InTest);

		// One action, run in its own frame.
		FScenario& Step(const FString& Name, TFunction<void(FScenarioContext&)> Action);
		// Game seconds.
		FScenario& Wait(float Seconds);
		// Polls Condition every frame (or every PollSeconds of game time) until it returns true. Fails
		// the scenario after TimeoutGameSeconds of game time. Conditions may act too (attack until dead).
		FScenario& WaitUntil(const FString& Name, float TimeoutGameSeconds, TFunction<bool(FScenarioContext&)> Condition, float PollSeconds = 0.0f);

		// Options, before Run().
		// Global time dilation from the start (default 1). Speeds long timers: Wait / WaitUntil and
		// timeouts are game seconds, so they shrink in real time with it.
		FScenario& TimeDilation(float Dilation);
		// Real seconds the steps may take in all before the scenario fails (default 180). PIE start and
		// end have their own limits (60 s and 30 s).
		FScenario& Timeout(float RealSeconds);
		// Game mode bIsRaid at the start (default true, as in docs/next-build-checklist.md section 4).
		FScenario& Raid(bool bRaid);
		// Keep the character Blueprint's Crossguard|Debug loadout on spawned characters.
		FScenario& KeepDebugLoadout();

		// Queues the scenario. Call once, last thing in RunTest.
		void Run();

	private:
		struct FItem
		{
			enum class EKind : uint8
			{
				Step,
				Wait,
				WaitUntil,
			};
			EKind Kind = EKind::Step;
			FString Name;
			TFunction<void(FScenarioContext&)> Action;
			TFunction<bool(FScenarioContext&)> Condition;
			float Seconds = 0.0f;
			float PollSeconds = 0.0f;
		};

		friend class FScenarioRunner;

		FAutomationTestBase& Test;
		TArray<FItem> Items;
		float Dilation = 1.0f;
		float TimeoutSeconds = 180.0f;
		bool bRaid = true;
		bool bKeepDebugLoadout = false;
		bool bQueued = false;
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
