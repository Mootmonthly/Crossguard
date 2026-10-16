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
//   scenario acts on the PIE world (standalone: it is the server). With FScenario::Clients(N) it runs
//   a server and N real clients in one process instead (see "Networked scenarios" in the context).
//   PIE always ends, also on failure or timeout. Missing Blueprints fail the test with a message
//   naming the asset.
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
//     Keep file-local helpers in a uniquely named namespace (unity builds). A name that `using
//     namespace` brings into RunTest must not also be in a game .cpp's anonymous namespace (e.g. the
//     skills' constants): in a unity build both are visible there and the name is ambiguous.
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
class ACrossguardMinion;
class ACrossguardMonster;
class ACrossguardPlayerState;
class AStaticMeshActor;
class APawn;
struct FGameplayAttribute;

// Scenario tests run in the editor with Play-In-Editor. Listed under the Product filter. In UE 5.5+
// EAutomationTestFlags is an enum class with flag operators; the expression works either way.
#define CROSSGUARD_SCENARIO_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A visual scenario (Crossguard.Visual.*, docs/editor-tests.md "Visual run") takes screenshots, so it
// needs a rendering editor: NonNullRHI keeps it off the list under -nullrhi (the default run), and
// Tools\run_editor_tests.bat -Visual runs it.
#define CROSSGUARD_VISUAL_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

class ACameraActor;

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

		// --- Networked scenarios (FScenario::Clients) ---------------------------------------------
		//
		// PIE runs a server and real clients in one process; each client has its own world, local
		// player, PlayerController and pawn. Steps still run against the server world; these reach
		// the clients' worlds. Replication is asynchronous: read client state through
		// FScenario::WaitUntilOnClient (or WaitUntil), never right after a server-side change.

		// Number of clients (0 in a standalone scenario). Listen server: client 0 is the host, whose
		// world is the server world itself (so its "copies" are the server actors).
		int32 ClientCount() const { return NumClients; }
		bool IsNetworked() const { return NumClients > 0; }
		// The server world (World()).
		UWorld* ServerWorld() const { return World(); }
		// Client Index's world; null if Index is out of range or the world is gone.
		UWorld* ClientWorld(int32 Index) const;
		// Client Index's own PlayerController and character, in its world (its local copies).
		APlayerController* ClientController(int32 Index) const;
		ACrossguardCharacter* ClientPawn(int32 Index) const;
		// The server-side character and PlayerController of client Index (matched by PlayerState
		// PlayerId). Also remembered as "Client<Index>" and "Client<Index>.Controller".
		ACrossguardCharacter* ServerPawnOf(int32 Index) const;
		APlayerController* ServerControllerOf(int32 Index) const;
		// Client Index's copy of a server actor (matched by net GUID): null while it has not been
		// replicated yet, is not relevant to that client (stealth), or was destroyed there. For the
		// listen host (client 0) it is the server actor itself.
		AActor* OnClient(const AActor* ServerActor, int32 Index) const;
		template <typename T>
		T* OnClient(const AActor* ServerActor, int32 Index) const
		{
			return Cast<T>(OnClient(ServerActor, Index));
		}
		// Server: Player joins Ally's team (ACrossguardPlayerState::SetTeamId), so they are allies.
		// Players log in on teams of their own.
		void MakeAllies(ACrossguardCharacter* Player, const ACrossguardCharacter* Ally);

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
		// Configure runs on the deferred spawn, after MobId is set and before BeginPlay and possession
		// (CombatRole, FixedHealth, AggroRange, ...; a MobId's row still applies over them at BeginPlay).
		ACrossguardMonster* SpawnMonster(FName Name, FName MobId, const FVector& FloorPoint, float Yaw = 0.0f,
			TFunction<void(ACrossguardMonster&)> Configure = nullptr);
		// A minion of Summoner, spawned as summon skills do (UCrossguardSummonAbility::SpawnMinion):
		// owned and instigated by Summoner, SetSummoner before BeginPlay (its team), facing the
		// summoner's yaw, possessed by its minion controller. MinionClass: a subclass of
		// ACrossguardMinion (default ACrossguardMinion). Configure runs before BeginPlay (CombatRole,
		// FixedHealth, ...). Unlike a summon: no life span, always spawned (not nudged out of others),
		// and not dismissed when the summoner casts again.
		ACrossguardMinion* SpawnMinion(FName Name, ACrossguardCharacter* Summoner, const FVector& FloorPoint, UClass* MinionClass = nullptr,
			TFunction<void(ACrossguardMinion&)> Configure = nullptr);
		// A static engine cube scaled to Size (cm), centred at Center, blocking everything (pawns,
		// weapon traces, line of sight: BlockAll). A wall: SpawnBlockingBox(Name, {500, Y, 200}, {20, 600, 400}).
		AStaticMeshActor* SpawnBlockingBox(FName Name, const FVector& Center, const FVector& Size);
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

		// Sets Health directly (e.g. one hit from death).
		void SetHealth(AActor* Actor, float Value);

		// A timed state tag on Actor (State.Silenced, State.NextAttack.Riposte, ...), as skills apply it
		// (crowd control skipped on the immune).
		void ApplyStatus(AActor* Actor, FGameplayTag State, float Seconds);

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
		// The character has this non-passive skill granted (on a client: once the spec has replicated).
		bool HasSkill(const ACrossguardCharacter* Character, FName SkillId) const;
		// Lethal damage through the native damage effect, so death runs as in play (body, kill XP to
		// Killer's player). No Killer: the victim's own (no XP).
		void Kill(AActor* Victim, AActor* Killer = nullptr);
		// One hit of Attack / Magic base damage from Source to Victim through the native damage effect
		// (as Kill, but these amounts): resistances, bonuses and the victim's damage reactions apply
		// (a monster turns on Source); guards and dodges do not. No Source: the victim's own. Returns
		// the Health it took off. Fails the scenario without ability systems.
		float Damage(AActor* Source, AActor* Victim, float Attack, float Magic = 0.0f);

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
		// Whom an AI pawn is fighting: a monster's or minion's controller target
		// (GetCurrentTarget), else its AI controller's focus actor. Null without one.
		AActor* TargetOf(const APawn* Pawn) const;
		// Weapon clanks (blocks, parries, clashes: MulticastWeaponClank -> OnWeaponClank) the character
		// has played since it spawned. 0 for other actors. The clank is played by the defender.
		int32 ClankCount(const AActor* Actor) const;
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

		// --- Server travel --------------------------------------------------------------------------

		// Call in the step that starts a server travel (UWorld::ServerTravel, a party's raid launch).
		// The items after that step wait until the server world (PIE instance 0) is MapPackage (a long
		// package name, "/Game/Maps/Dev/L_DevSandbox"; matched without the PIE prefix), has begun play
		// and runs an ACrossguardGameMode; then they go on against it: World() and GameMode() are the
		// new ones, remembered actors and LookFrom cameras are forgotten (they were in the old world),
		// and game time is the new world's. The scenario fails if the server has not arrived within
		// TimeoutRealSeconds. Clients travel on their own: wait for them with WaitUntilOnClient.
		void ExpectServerTravel(const FString& MapPackage, float TimeoutRealSeconds = 90.0f);
		// Between ExpectServerTravel and the arrival.
		bool IsTravelling() const { return !TravelMapPackage.IsEmpty(); }

		// --- Screenshots (visual scenarios) -------------------------------------------------------

		// Points a viewport's camera: a camera actor at Eye looking at LookAt becomes the view target of
		// the local player of the server world (ClientIndex -1: standalone, the PIE player's viewport) or
		// of client ClientIndex's world (spawned there). Reused on later calls. The pawns stay where they
		// are. Wait a frame before the screenshot.
		void LookFrom(const FVector& Eye, const FVector& LookAt, int32 ClientIndex = -1);
		// Saves what a game viewport shows right now to Saved/EditorTests/Screenshots/<Test>/<Name>.png
		// (<Test>: the test name after "Crossguard.Visual."). ClientIndex -1: the server world's viewport;
		// else client ClientIndex's. Needs a rendering editor (Tools\run_editor_tests.bat -Visual); under
		// -nullrhi it adds an error. Returns success.
		bool Screenshot(const FString& Name, int32 ClientIndex = -1);

		// --- Log ----------------------------------------------------------------------------------

		// Lines logged since the scenario started (or the last ClearLog), as "Category: message".
		bool LogContains(const FString& Text) const;
		int32 LogCount(const FString& Text) const;
		TArray<FString> LogLines() const;
		void ClearLog();

	private:
		friend class FScenarioRunner;

		AActor* SpawnActorInternal(FName Name, UClass* Class, const FVector& Location, float Yaw, TFunctionRef<void(AActor&)> Configure);
		// Every frame: LookFrom cameras stay the view targets (the engine switches back to the pawn).
		void KeepCameras();
		// Deferred spawn of a Crossguard character above FloorPoint; bAIController: possessed by its
		// AI controller on spawn.
		// SpawnOwner: owner and instigator (summoned minions).
		ACrossguardCharacter* SpawnCharacter(FName Name, UClass* Class, const FVector& FloorPoint, float Yaw, bool bAIController,
			TFunctionRef<void(ACrossguardCharacter&)> Configure, ACrossguardCharacter* SpawnOwner = nullptr);

		TWeakObjectPtr<UWorld> WorldPtr;
		TWeakObjectPtr<ACrossguardGameMode> GameModePtr;
		// Networked: client Index runs in PIE instance FirstClientInstance + Index (listen server: 0,
		// the host; dedicated server: 1, instance 0 being the server).
		int32 NumClients = 0;
		int32 FirstClientInstance = 0;
		TWeakObjectPtr<UClass> CharacterClassPtr;
		TMap<FName, TWeakObjectPtr<AActor>> Actors;
		TMap<FName, double> Numbers;
		// LookFrom's camera per viewport (ClientIndex -1 for the server world's).
		TMap<int32, TWeakObjectPtr<ACameraActor>> Cameras;
		TSharedPtr<FScenarioLogCapture> LogCapture;
		bool bStopped = false;
		// ExpectServerTravel: the destination's long package name (empty: not travelling), and when
		// to give up (FPlatformTime::Seconds).
		FString TravelMapPackage;
		double TravelDeadline = 0.0;
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
		// Waits Seconds of game time, calling Watch every frame (record Health drops, top up, act).
		// Never fails on its own: it simply ends after Seconds (contrast WaitUntil).
		FScenario& WaitWatching(const FString& Name, float Seconds, TFunction<void(FScenarioContext&)> Watch);
		// WaitUntil polled against client Index's world (networked scenarios): Condition gets that
		// world too. Fails at once if the client world is gone.
		FScenario& WaitUntilOnClient(int32 Index, const FString& Name, float TimeoutGameSeconds,
			TFunction<bool(FScenarioContext&, UWorld& ClientWorld)> Condition, float PollSeconds = 0.0f);

		// Options, before Run().
		// Networked PIE in one process (docs/editor-tests.md, "Networked scenarios"): a listen server
		// whose host is client 0 plus Count - 1 more clients, or with bDedicatedServer a dedicated
		// server and Count clients (1..8). Every client is a real PIE client with a local player; PIE
		// is ready once each one is logged in with a pawn (ServerPawnOf / ClientPawn). Default: one
		// standalone instance, the server.
		FScenario& Clients(int32 Count, bool bDedicatedServer = false);
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
				WaitWatching,
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
		int32 NumClients = 0;
		bool bDedicatedServer = false;
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
