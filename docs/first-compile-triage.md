# First Compile Triage (UE 5.8)

A reference for the first build on the Windows PC ([pc-session.md](pc-session.md), step 3). The
game module, the tests and `Tools/editor_setup.py` were written without an engine to compile
against. This sheet lists the engine calls most likely to break, with the fix to try first.

How it was made: a sweep of `Source/Crossguard` (game and `Private/Tests`), `Source/CrossguardRules`
and `Tools/editor_setup.py`. It includes every comment that says UNCERTAIN, unsure, verify or
"engine call", and [editor-tests.md](editor-tests.md) "Scenario framework: engine calls to check
first". It also used scripted heuristics for these: unity-build name clashes, shadowed variables,
`Printf`/`UE_LOG` format arguments, RPC `_Implementation`/`_Validate` pairs, UHT default arguments,
missing includes and mixed int/float `FMath` calls. The code turned out careful: the heuristics
found almost nothing. What is left are mostly **API-version guesses**: they can only be confirmed
against the 5.8 headers, so "unsure" below means it.

**How to use it**: read the first error in `Saved\Build\errors.txt` and find its file:line or API
in the tables below. If it is listed, apply the fix given. If not, use the generic recipes at the
end. Fix the smallest thing, rebuild, repeat. Do not change game rules to make something compile.

## What fails the build by default

Our targets use `DefaultBuildSettings = BuildSettingsVersion.Latest` and
`IncludeOrderVersion = EngineIncludeOrderVersion.Latest`.

| Kind | Default for a game module | Effect here |
| --- | --- | --- |
| Hard errors (C2xxx, C3xxx), UHT errors, link errors (LNK2019/2001) | always fatal | The tables below |
| Shadowed variables C4456 / C4457 / C4458 / C4459 | **error** (ShadowVariableWarningLevel = Error since BuildSettingsVersion V2) | See A1. Rename the local. |
| Deprecation C4996 (`UE_DEPRECATED`) | **warning** (bWarningsAsErrors is false) | Does not stop the build. Fix later, or leave it. |
| Double to float truncation C4244 (LWC `FVector` math into `float`) | off (UnsafeTypeCastWarningLevel = Off) | Noise at most. Many `const float X = Vec.Size()` sites; ignore. |
| Undefined macro in `#if` (C4668) | warning | Only `WITH_EDITOR` / `WITH_DEV_AUTOMATION_TESTS` are used, both always defined. |
| Unused locals (C4189) | warning | Ignore |

Unsure: 5.6+ may change one of these defaults under `Latest`. If a plain warning stops the
build, the log line says which setting made it an error. Do **not** turn `bWarningsAsErrors`
on. Do not lower `ShadowVariableWarningLevel` either: rename the variable instead.

Unity builds: the first build compiles about 30 `.cpp` files per unity blob. These can mask a
missing include, and they can surface name clashes between anonymous namespaces. To find which
`.cpp` owns an error, rebuild with `-DisableUnity`: pass it through `Tools\build_editor.ps1` if it
forwards extra arguments, or run `Build.bat CrossguardEditor Win64 Development
-Project=...\Crossguard.uproject -DisableUnity` by hand.

## A. Likely compile errors, most likely first

| # | Where | Call | Why it may fail | Most likely fix |
| --- | --- | --- | --- | --- |
| A1 | `Private/Character/CrossguardMonsterController.cpp:27` | `const ACrossguardCharacter* Character = Cast<...>(Candidate);` inside an `AAIController` method | `AController` has a (private) member `Character`. MSVC may report C4458 "declaration hides class member", which is an error by default. Unsure whether MSVC warns for a private base member. Every other shadowing heuristic came back clean. | **Applied** (renamed to `CandidateCharacter`). |
| A2 | `Private/Character/CrossguardAICharacter.cpp:47` | `GetMesh()->SetOwnerNoSee(false)` | `USkeletalMeshComponent` used without `#include "Components/SkeletalMeshComponent.h"`. With include order `Latest`, `Character.h` may only forward-declare it. A unity build hides this (`CrossguardCharacter.cpp` includes it); `-DisableUnity` or adaptive unity shows C2027 "use of undefined type". It is the only missing include the scan found. | **Applied** (include added). |
| A3 | `ActivationBlockedTags` / `ActivationOwnedTags` written in constructors: `AbilitySystem/CrossguardGameplayAbility.cpp:16-20`, `AbilitySystem/CrossguardPassiveAbility.cpp:12-17`, `Combat/CrossguardWeaponAttackAbility.cpp:24-27`, `Combat/CrossguardGuard.cpp:304-306`, `Skills/CrossguardArtSkills.cpp:220,307,405-407`, `Skills/CrossguardWeaponSkills.cpp:538,656`, `Skills/CrossguardSupportSkills.cpp:443` | direct `UGameplayAbility` member access | GAS has been moving tag containers behind accessors. 5.5 did it for `AbilityTags` (the code already uses `SetAssetTags` / `GetAssetTags`). If 5.8 did the same to the activation tag containers, you get C4996 (a warning, harmless) or, if they are now private, C2248 "cannot access protected/private member". Unsure which, if either. | Open `Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/Abilities/GameplayAbility.h` and search for `ActivationBlockedTags`. If a constructor-only setter exists (as `SetAssetTags` does), build the container locally and call the setter. If it is only deprecated, leave it, or wrap the constructor body in `PRAGMA_DISABLE_DEPRECATION_WARNINGS` / `PRAGMA_ENABLE_DEPRECATION_WARNINGS`. |
| A4 | `Spec.Data->DynamicGrantedTags` (15 sites): `Skills/CrossguardSkillAbility.cpp:49,70,90,118,144,187`, `Skills/CrossguardAuraSkills.cpp:118,196`, `Skills/CrossguardArtSkills.cpp:258`, `AbilitySystem/CrossguardGameplayAbility.cpp:116`, `Items/CrossguardEquipmentComponent.cpp:522,526,532,561,565` | `FGameplayEffectSpec::DynamicGrantedTags` (public member) | In 5.3 `DynamicAssetTags` went private behind `AddDynamicAssetTag` (`CrossguardInventoryComponent.cpp:1608` already uses that). `DynamicGrantedTags` was still public through 5.5. Unsure about 5.8. These tags matter: they are how status effects grant `State.*` and how cooldowns grant their tag. | If C2248: look in `GameplayEffect.h` (`struct FGameplayEffectSpec`) for an accessor such as `AddDynamicGrantedTag` / `GetDynamicGrantedTags` and use it, keeping behavior the same. If only C4996, leave it. **Do not** switch to `AddDynamicAssetTag`: asset tags are not granted to the target, so every status effect would silently stop working. |
| A5 | `Private/Character/CrossguardCharacter.cpp:127-160` | `UInputMappingContext::MapKey(Action, Key)` returning `FEnhancedActionKeyMapping&`, then `.Modifiers.Add(NewObject<UInputModifierNegate>(...))`; `InvertY->bX` / `bZ`; `UInputAction::ValueType` written (`:101`) | Enhanced Input reorganized mapping storage in 5.4-5.6 (profiles, default key mappings). `MapKey` existed through 5.5; its return type and `Modifiers` access are the version-sensitive parts. Unsure. | If `MapKey` returns something else: `FEnhancedActionKeyMapping& M = Context->MapKey(...);` may need to become a copy that is modified and then written back (check `InputMappingContext.h` for the 5.8 way). If `ValueType` is no longer writable: look for a setter in `InputAction.h`, or make the input assets in the editor and leave `DefaultMappingContext` set (the code only builds defaults when none is set). |
| A6 | `Private/AbilitySystem/CrossguardEffects.cpp:101` | `EGameplayModOp::Multiplicitive` (and `Additive`) | 5.4/5.5 renamed the modifier ops (`AddBase`, `MultiplyAdditive`, `DivideAdditive`, `MultiplyCompound`, `AddFinal`) and kept the old names as hidden aliases. If 5.8 removed the aliases: C2039. | `EGameplayModOp::MultiplyAdditive` (same value 1, same "sum of (mod-1)" math) and `EGameplayModOp::AddBase` (value 0). |
| A7 | `Private/Character/CrossguardAICharacter.cpp:363` | `FGameplayAbilitySpec::GetPrimaryInstance()` | Instancing changed in 5.5 (NonInstanced deprecated, instance lists made private). Most likely a C4996 warning at worst. | If removed: `Spec.GetAbilityInstances()` and take the first (`InstancedPerActor` has exactly one). |
| A8 | Scenario framework PIE start, `Private/Tests/CrossguardScenario.cpp:1109-1116` | `FRequestPlaySessionParams` (`PlayInEditorDataTypes.h`), `.WorldType = EPlaySessionWorldType::PlayInEditor`, `.EditorPlaySettings = ULevelEditorPlaySettings*`, `SetPlayNetMode(EPlayNetMode::PIE_Standalone)`, `SetPlayNumberOfClients`, `SetRunUnderOneProcess`, `GEditor->RequestPlaySession(Params)`, `GEditor->RequestEndPlayMap()`, `GEditor->PlayWorld` | Editor API, flagged in editor-tests.md. Signatures were stable from 5.0 to 5.5. The field may be `TObjectPtr`, which assignment from a raw pointer handles. Low compile risk; the runtime risk is in D1. | If a field or setter moved: search `PlayInEditorDataTypes.h` / `LevelEditorPlaySettings.h`. As a last resort, replace the whole PIE start with `ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false))` from `Tests/AutomationEditorCommon.h` (it uses the user's play settings; then make sure they are "Standalone, 1 player"). |
| A9 | `Private/Tests/CrossguardScenario.cpp:1097` | `FAutomationEditorCommonUtils::CreateNewMap()` (`Tests/AutomationEditorCommon.h`, UnrealEd) | Flagged in editor-tests.md. Returns `UWorld*` through 5.5. | If renamed: `FAutomationEditorCommonUtils::CreateNewMap` overloads, or `UEditorLoadingAndSavingUtils::NewBlankMap(false)`. |
| A10 | `Private/Tests/CrossguardScenario.cpp:113-119` | `IAssetRegistry::IsLoadingAssets`, `SearchAllAssets(true)`, `GetDerivedClassNames(TArray<FTopLevelAssetPath>, TSet<FTopLevelAssetPath>, TSet<FTopLevelAssetPath>&)`, `UClass::GetClassPathName()` | UE 5.1+ signatures. The `{Base->GetClassPathName()}` braced init must convert to `const TArray<FTopLevelAssetPath>&`. | If C2664 on the braced list: `TArray<FTopLevelAssetPath> Bases; Bases.Add(Base->GetClassPathName());` |
| A11 | `Private/Tests/CrossguardScenario.cpp:188,198` | `FOutputDevice::Serialize(const TCHAR*, ELogVerbosity::Type, const FName&) override`, `CanBeUsedOnAnyThread() const override` | Overriding one of the overloaded `Serialize`s. If 5.8 changed the pure virtual: C3668 "did not override". | Copy the 3-argument pure-virtual signature from `Misc/OutputDevice.h`. `GLog->AddOutputDevice` / `RemoveOutputDevice` are unchanged. |
| A12 | `Public/Game/CrossguardGameMode.h` | overrides `PreLogin(Options, Address, UniqueId, ErrorMessage)`, `InitNewPlayer(PC, UniqueId, Options, Portal)`, `HandleSeamlessTravelPlayer(AController*&)`, `HandleStartingNewPlayer_Implementation`, `Logout` | `PreLogin` gained an async sibling (`PreLoginAsync`) in 5.x. The sync one still existed in 5.5. The tests call `Mode->InitNewPlayer` / `HandleStartingNewPlayer_Implementation` directly (`CrossguardScenario.cpp:386,402`), which is legal because our override is public. | If C3668 on `PreLogin`: match the 5.8 signature in `GameModeBase.h`, or move the team-full check to `PreLoginAsync`, keeping the same behavior. |
| A13 | `Public/Game/CrossguardPlayerState.h:80,82` | `CopyProperties(APlayerState*) override`, `OnSetUniqueId() override` | `OnSetUniqueId` is a lesser-known virtual. Unsure it still exists by that name. | If C3668: search `PlayerState.h` for `SetUniqueId`; the hook may be `OnSetUniqueId` or gone. In the worst case, override `SetUniqueId(const FUniqueNetIdRepl&)`, call `Super`, then run our code. |
| A14 | `Private/Character/CrossguardCharacter.cpp:665` / `.h:217` | `IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override` | Stable since UE4. Listed because it was asked about. | If C3668: copy the signature from `Actor.h`. |
| A15 | `Private/Skills/CrossguardWeaponSkills.cpp:636-638, 688-690` | `UAbilityTask_ApplyRootMotionJumpForce::ApplyRootMotionJumpForce(this, Name, FRotator, Distance, Height, Duration, MinimumLandedTriggerTime, bFinishOnLanded, ERootMotionFinishVelocityMode, SetVelocityOnFinish, ClampVelocityOnFinish, PathOffsetCurve, TimeMappingCurve)` (13 arguments) | Matches 5.0-5.5. `OnFinish` is a no-parameter dynamic delegate; the handlers are `UFUNCTION()`. | If C2660 (argument count): open `AbilityTask_ApplyRootMotionJumpForce.h` and line the arguments up. Same for `ApplyRootMotionConstantForce` (11 arguments, last `bEnableGravity`) at `Skills/CrossguardArtSkills.cpp:104-106`. |
| A16 | Test flags `Private/Tests/CrossguardTestUtils.h:20`, `CrossguardScenario.h:93` | `EAutomationTestFlags::EditorContext \| EAutomationTestFlags::EngineFilter` / `ProductFilter` | 5.5 made `EAutomationTestFlags` an enum class with flag operators (`ApplicationContextMask` moved to `EAutomationTestFlags_ApplicationContextMask`, which we do not use). | If `IMPLEMENT_SIMPLE_AUTOMATION_TEST`'s static_assert complains: wrap as `static_cast<EAutomationTestFlags>(...)`; flags are fine as they are in 5.5. |
| A17 | `Private/Tests/*.cpp` `C.Test.TestEqual(...)` | `FAutomationTestBase::TestEqual` overloads (int32, int64, float, double, FVector, FString...) | Mixed argument types (float with double, int32 with int64) give C2666 "ambiguous". The tests cast consistently (`static_cast<float>(C.Number(...))`, `static_cast<int64>(...)` for bank gold), and the scan found no mixed call. Listed because LWC makes `GetActorLocation().Z` a double. | Cast both sides to `float` (or `int32`), as the neighboring lines do. |
| A18 | `Private/Skills/CrossguardWeaponSkills.cpp:634` | `FMath::Max(ToTarget.Size2D() - LionStopShort, 0.0f)` (double with float) | UE5's `FMath::Max` / `Min` have mixed float/double overloads. Fine unless 5.8 dropped them. | `FMath::Max(static_cast<float>(ToTarget.Size2D()) - LionStopShort, 0.0f)` |

Checked and **not** expected to break (no action): `AbilityActivatedCallbacks.AddUObject/Remove`
(`Interaction/CrossguardGatherNode.cpp:149,259`, `Skills/CrossguardSupportSkills.cpp:306,358`),
`FGameplayAbilitySpec::SourceObject.Get()` (weak pointer, `Tests/CrossguardScenarioMonsterAITests.cpp:161`),
`GetCurrentSourceObject()`, `SetAssetTags` / `GetAssetTags`, `FGameplayAbilitySpec(Class, Level, InputID, SourceObject)`,
`FindAbilitySpecFromHandle`, `TryActivateAbilityByClass`, `RemoveActiveEffectsWithGrantedTags`,
`AddDynamicAssetTag`, native gameplay tags (`UE_DECLARE_GAMEPLAY_TAG_EXTERN` with `CROSSGUARD_API`,
as Lyra does), GE constructors (only `DurationPolicy` / `DurationMagnitude` / `Period` /
`Modifiers` / `Executions`; no `InheritableOwnedTagsContainer`, granted, remove-on or stacking
members, which became GE components in 5.3), `DEFINE_ATTRIBUTE_CAPTUREDEF` and the execution
calculation API, `LaunchCharacter(FVector, bool, bool)`, `MaxWalkSpeed` / `MaxFlySpeed` /
`BrakingDecelerationFlying`, `MoveToActor` / `MoveToLocation` returning
`EPathFollowingRequestResult::Type` and `GetMoveStatus()`, `TAutoConsoleVariable<bool>`,
`SpawnActorDeferred` (owner / instigator types), overlap/hit delegate handler signatures (all
`UFUNCTION()`), RPC `_Implementation` / `_Validate` pairs (55 checked), UHT default arguments,
`UPROPERTY` types, `Printf` / `UE_LOG` argument counts and `%s` arguments (all `TCHAR*` or `*FString`),
`IsDataValid(FDataValidationContext&)` under `WITH_EDITOR`, every engine `#include` path,
module dependencies (`AIModule`, `EnhancedInput`, `GameplayAbilities/Tags/Tasks`,
`UnrealEd` + `AssetRegistry` in editor builds), and engine macro names (`check`, `verify`, `IN`,
`OUT`, `PI`, Windows `ERROR` / `DELETE`) used as identifiers in `CrossguardRules` (none).

Unity name clashes: the same helper names live in an anonymous namespace in a game file and in a
test namespace. These are `InventoryOf` (`Game/CrossguardGuildSubsystem.cpp:32` vs
`Tests/CrossguardScenarioSocialTests.cpp:52`), `TradeOf` (`Items/CrossguardTradeComponent.cpp:24`
vs `SocialTests.cpp:57`) and `Guilds` (`Game/CrossguardGuildComponent.cpp:12` vs
`SocialTests.cpp:164`). Their parameter types differ, so overload resolution picks the right one.
If a unity build still reports C2668 "ambiguous call" there, qualify the test's call
(`CrossguardScenarioSocial::TradeOf(A)`).

## B. Deprecation warnings (do not stop the build)

Expect C4996 lines for any of A3, A4, A6 or A7 that 5.8 deprecated without removing. Leave them
for a later cleanup commit; they are not the first error. `Saved\Build\errors.txt` holds errors
only, so warnings show up in `build.log`.

## C. Link errors (LNK2019 / LNK2001 "unresolved external")

Since 5.3 many engine classes are `MinimalAPI`: only some member functions are exported. A call to
a non-exported engine function compiles fine, then fails to link. Candidates, all believed to be
exported:

| Where | Symbol | If it does not link |
| --- | --- | --- |
| `Tests/CrossguardDataTests.cpp:86` | `UDataTable::CreateTableFromJSONString` (editor only) | Use `FDataTableImporterJSON` / `UDataTableFunctionLibrary::FillDataTableFromJSONString(Table, Json)`. |
| `Tests/CrossguardScenario.cpp:1097` | `FAutomationEditorCommonUtils::CreateNewMap` | See A9. |
| `Tests/CrossguardTestUtils.h:47-65` | `UWorld::CreateWorld`, `GEngine->CreateNewWorldContext`, `DestroyWorldContext`, `UWorld::InitializeActorsForPlay`, `DestroyWorld` | Mirrors the engine's own `GameplayEffectsTest.cpp`. If one does not link, copy what that file does in 5.8. |

An unresolved symbol from **our** module usually means a declared-but-undefined function: grep
for the name and add the definition. An unresolved symbol from **another engine module** means a
missing entry in `Crossguard.Build.cs`. The usual ones would be `NavigationSystem`, `NetCore`,
`DeveloperSettings`, `UMG` or `Slate`; none of them is used today.

## D. Runtime (after it builds)

| # | Where | What may go wrong | What to try |
| --- | --- | --- | --- |
| D1 | `Tests/CrossguardScenario.cpp` `BeginScenario` / `WaitForPIE` | Headless (`-nullrhi`) PIE may not start: there is no level viewport, so "PIE did not start" appears. | Run the tests once without `-nullrhi` (`Tools\run_editor_tests.bat` option, if it has one, or the Session Frontend). Otherwise set `PlaySettings->LastExecutedPlayModeType = PlayMode_InEditorFloating` before `RequestPlaySession`, or use `FStartPIECommand` (A8). |
| D2 | `Tests/CrossguardScenario.cpp:1111` | `NewObject<ULevelEditorPlaySettings>` gets class defaults, not the user's config, so a default may be odd (window size, `bLaunchSeparateServer`). | Copy from `GetDefault<ULevelEditorPlaySettings>()` with `DuplicateObject`, then set the three fields. |
| D3 | `SetAssetTags(...)` in ability constructors | The 5.5 setter is meant for the CDO. If 5.8 added an `ensure(HasAnyFlags(RF_ClassDefaultObject))`, every instanced ability (`InstancedPerActor`) would log an ensure when created. | Guard it: `if (HasAnyFlags(RF_ClassDefaultObject)) { SetAssetTags(...); }`. Instances copy tags from the CDO anyway. |
| D4 | `CrossguardCharacter::IsNetRelevantFor` (stealth, [anti-cheat.md](anti-cheat.md)) | Iris replication does not call `IsNetRelevantFor`. If the project or engine has Iris on (`net.Iris.UseIrisReplication 1`), stealth hiding silently does nothing. `Crossguard.Scenario.*StealthRelevancy` would catch it. | Keep Iris off (the 5.8 default as far as known), or port the rule to an Iris filter later. |
| D5 | `DynamicGrantedTags` status effects | If 5.8 stopped honoring `DynamicGrantedTags` at runtime (it compiles but no tag appears), every `State.*` test fails at once ("State.Rooted not present"). | Check `showdebug abilitysystem`. The fix is the same as A4. |
| D6 | Monsters and minions without a nav mesh | `MoveToActor` fails, so the controllers fall back to direct steering (`bDirectMove`). Expected in test maps. | Nothing. Pathing is a by-hand check. |
| D7 | `Tools/editor_setup.py` (all marked `UNCERTAIN`) | `DataTableFunctionLibrary.fill_data_table_from_json_file(table, path, struct)` (3-argument form; falls back to 2 arguments, then `CSVImportFactory`); `unreal.find_object(None, "/Script/Crossguard.<Struct>")` at `:239`; `Blueprint.generated_class()` at `:273`; `Object.modify()` at `:282`; `MathLibrary.class_is_child_of` at `:305`; factory properties `struct` (`:351`), `data_asset_class` (`:490`), `parent_class` (`:538`); `AssetImportData.scripted_add_filename` at `:400`; `LevelEditorSubsystem.new_level` / `save_current_level` in the commandlet (`:797,899`); the `NavMeshBoundsVolume` brush (`:823-825`). | One FAIL line names the step. Fallbacks in order: `unreal.load_object(None, path)` or `unreal.<StructName>.static_struct()` for structs; `unreal.EditorAssetLibrary.load_blueprint_class(path)` for `generated_class`; `-Mode Editor` (full editor process) for map creation. The property names match the 5.x C++ fields (`UDataTableFactory::Struct`, `UDataAssetFactory::DataAssetClass`, `UBlueprintFactory::ParentClass`). If one is not exposed, `set_editor_property` raises with the list of valid names. |
| D8 | Extra scenario players (`Tests/CrossguardScenario.cpp:358-410`) | A `SpawnActor`'d `APlayerController` with no `UPlayer` may not count as local, so `LocalPredicted` abilities would not activate, or the engine would not create `PlayerInput`. | See editor-tests.md, item 4. Checking `Controller->IsLocalController()` right after spawning shows it. |

## Generic recipes for errors not listed

| Error | Usual cause here | Fix |
| --- | --- | --- |
| C2027 / C2039 on an engine type ("undefined type", "is not a member") | A missing include, hidden by `IncludeOrderVersion.Latest` (engine headers include less) | Add the engine header to the `.cpp`. Search the engine `Source` for `class ENGINE_API UXxx` or `class UXxx` to find which header. |
| C2248 (cannot access private/protected) on an engine member | An engine field became private | Use the getter/setter next to it in the header (see A3/A4) |
| C3668 (method with override specifier did not override) | The engine virtual signature changed | Copy the signature from the engine header |
| C2664 / C2660 (argument conversion / count) | The engine function signature changed | Line the arguments up with the header and keep the meaning |
| C2666 / C2668 (ambiguous) | float vs double (LWC), int32 vs int64, or a unity-blob name clash | Cast, or qualify the namespace |
| C4456-C4459 (declaration hides ...) | A local or lambda parameter has the same name as an outer one or a member | Rename the inner one |
| UHT error (before compiling) | A `UPROPERTY` / `UFUNCTION` type or default UHT cannot parse | The message names file:line. Usually remove the default argument or change the type to a UHT-supported one. |
| `static_assert` in `UE_LOG` / `FString::Printf` | `%s` given an `FString` or `FName` | `*Str`, `*Name.ToString()` |

Keep each fix minimal, rebuild, and commit the fixes with a message naming this sheet's row (for
example "First compile: A3 activation tags via setter").

## First build results (UE 5.8, 2026-10-15)

The build compiled first time (after A1, A2); none of A3-A18 or C fired. Editor set-up passed. The
first test run was 36/103; every failure was in the tests, not the game. Now 103/103.

| Cause | Fix |
| --- | --- |
| D8 confirmed. 5.8 `APlayerController::IsLocalController`: with no net driver (standalone PIE) a controller is local only with a `ULocalPlayer`, so GAS refused `LocalPredicted` abilities for `SpawnPlayer`'s players | `SpawnPlayer` calls `Controller->SetAsLocalPlayerController()` before possession |
| Log capture got nothing: GLog only delivers to buffered devices on its dedicated log thread, after the test has looked | `FScenarioLogCapture::CanBeUsedOnMultipleThreads()` returns true (unbuffered; it was already mutex-guarded) |
| 5.8 `USceneComponent::IsVisible()` is false whenever `bHiddenInGame`, which a capsule always is | Stealth test reads `GetVisibleFlag()` (what `SetVisibility` sets) |
| Spawner tests matched monsters by owner; possession makes the AI controller the owner | `SpawnedBy` matches by `GetHomeLocation()` within the spawner's `Radius` |
| MonsterTeam: the minion spawned straight behind M1 and, with no NavMesh (D6), walked into its back | Minion spawned 3 m off the line |
| ArrowBlockedNoStun: the archer's first arrow lands in the set-up wait, before the shield is up | Health reset to max when the shield goes up |

Still there, harmless: `CooldownGameplayEffectClass 'CrossguardCooldownEffect' grants no tags`
(5.8 inspects the effect's class defaults; our cooldown tags are dynamic and
`UCrossguardGameplayAbility::GetCooldownTags` reports them, so cooldowns work), and
`No GameplayCueNotifyPaths` (no cues yet).

## Overnight code (unbuilt)

Code written on 2026-10-16 after the first build: the placeholder UI (`Source/Crossguard/*/UI`,
`Tests/CrossguardScenarioUITests.cpp`, `UMG` / `Slate` / `SlateCore` in `Crossguard.Build.cs`), the
stealth channel close and RVO avoidance, and the data-passive requirements. Reviewed by reading
only. Fixed before the build: the Iris check is now inside `#if defined(UE_WITH_IRIS) && UE_WITH_IRIS`,
`FindActorChannelRef` gets an explicit `TWeakObjectPtr<AActor>`, the skill wheel uses `UE_PI`
(not `PI`), `UIHelpers.h` forward-declares `APawn`, and `Tests/CrossguardScenarioNetworkTests.cpp`
qualifies `CrossguardScenarioNet::StealthSeconds` (`Skills/CrossguardSupportSkills.cpp` has a
`StealthSeconds` in an anonymous namespace: ambiguous if a unity blob holds both). Not reviewed
here: the save and party files (written by other agents the same night).

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| O1 | `Private/Character/CrossguardCharacter.cpp:724` | `UNetDriver::IsUsingIrisReplication()` | Name guessed from 5.x. Now compiled only when `UE_WITH_IRIS` is 1. | If C2039: delete the guarded block (Iris is off, D4) or use `UE::Net::ShouldUseIrisReplication()` / the driver's `bIsUsingIris` if it exists. |
| O2 | `CrossguardCharacter.cpp:718` | `UNetDriver::GetReplicationDriver()` | Believed public and inline in 5.x. | If missing: test `NetDriver->ReplicationDriver != nullptr` (public `UPROPERTY` in 5.x). |
| O3 | `CrossguardCharacter.cpp:735` | `UNetConnection::FindActorChannelRef(const TWeakObjectPtr<AActor>&)` | Present since 4.2x; signature unsure in 5.8. | `Connection->ActorChannelMap().FindRef(this)`. |
| O4 | `CrossguardCharacter.cpp:736` | `UChannel::Closing` (public bitfield) | If made private or deprecated (C2248 / C4996). | `Channel->IsClosing()` if it exists; else drop the check (closing twice is a no-op in `UChannel::Close`). |
| O5 | `CrossguardCharacter.cpp:743,750` | `UNetConnection::PlayerController`, `OwningActor`, `ViewTarget`, `Children` | Public `TObjectPtr` members in 5.x. | If one went behind an accessor: `GetPlayerController(nullptr)`, `GetConnectionViewTarget()`; `OwningActor` has `GetOwningActor()` in some versions. |
| O6 | `CrossguardCharacter.cpp:756` | `UChannel::Close(EChannelCloseReason::Relevancy)` | Stable since 4.22. | If C2660: copy the 5.8 signature from `Engine/Channel.h`. |
| O7 | `Private/Character/CrossguardAICharacter.cpp:49-51,107-113` | `bUseRVOAvoidance`, `AvoidanceConsiderationRadius`, `AvoidanceWeight` (constructor), `FNavAvoidanceMask::SetFlagsDirectly`, `SetAvoidanceGroupMask` / `SetGroupsToAvoidMask` / `SetGroupsToIgnoreMask` | Public `UCharacterMovementComponent` members in 5.x. | If the flag is private: `SetAvoidanceEnabled(true)` in `BeginPlay`. If `SetFlagsDirectly` is missing: set `bGroup0..31` or call the `int32` overloads (`SetAvoidanceGroup(int32)` etc.). |
| O8 | `Public/UI/CrossguardWidgetBase.h:72` | `UWidgetTree::ConstructWidget<T>(UClass*)` for `UCrossguardUIButton` / `UCrossguardItemGridWidget` (user widgets inside user widgets) | 5.x routes `UUserWidget` classes through `CreateWidget(WidgetTree, ...)`. If 5.8 static_asserts or returns null for them: | `CreateWidget<WidgetT>(this, WidgetT::StaticClass())` for `UUserWidget` subclasses (`if constexpr (TIsDerivedFrom<WidgetT, UUserWidget>::Value)`). |
| O9 | `Private/UI/CrossguardWidgetBase.cpp:30` | `WidgetTree` created by hand if `UUserWidget::Initialize` did not | `Initialize` creates it for native classes in 5.x, so this branch should not run. A name clash ("WidgetTree" already exists) would show as a fatal `NewObject` error. | Remove the branch, or use `NewObject<UWidgetTree>(this, NAME_None, RF_Transient)`. |
| O10 | `Private/UI/CrossguardUIHelpers.cpp:102` | `UGameplayAbility::GetCooldownTimeRemaining(const FGameplayAbilityActorInfo*)` on the CDO, `UAbilitySystemComponent::AbilityActorInfo` | Public in 5.x. | If private: `AbilitySystem->GetActiveEffectsTimeRemaining(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(*Ability->GetCooldownTags()))` and take the max. |
| O11 | `Private/UI/CrossguardHUD.cpp:129` | `UInputAction::ValueType` written | Same as A5 (it compiled in the character). | As A5. |
| O12 | `Private/UI/CrossguardHUD.cpp:154` | `AActor::EnableInput` on an `AHUD` makes an Enhanced Input component | Runtime: if `Cast<UEnhancedInputComponent>(InputComponent)` is null, the log says "UI keys are not bound". | `Config/DefaultInput.ini` already sets `DefaultInputComponentClass`; otherwise create it by hand: `InputComponent = NewObject<UEnhancedInputComponent>(this); InputComponent->RegisterComponent(); PlayerOwner->PushInputComponent(InputComponent);`. |
| O13 | `Private/Tests/CrossguardScenarioUITests.cpp:111-112` | the HUD made its widgets | Runtime: `ACrossguardHUD::CanShowUI` (`HUD.cpp:70`) needs `UWorld::GetGameViewport()`. If headless PIE has none, the HUD makes no widgets and these two checks fail (the rest of the test builds its own widgets and still runs). | **Applied**: the test skips the HUD-made widget checks when there is no game viewport. |
| O14 | `Private/UI/*.cpp` | `UTextBlock::SetAutoWrapText`, `USizeBox::SetMaxDesiredHeight`, `UEditableTextBox::SetHintText`, `FCoreStyle::GetDefaultFontStyle(FName, float)`, `FAnchors(float, float)`, `FInputModeGameAndUI::SetHideCursorDuringCapture`, `APlayerController::GetHUD<T>()` | UMG setters as of 5.1-5.5. | If one is missing: the matching public property, or the setter's new name in the header next to it. |
| O15 | `Crossguard.Build.cs` | `UMG`, `Slate`, `SlateCore` added | An LNK2019 on a UMG symbol means a module is missing; `InputCore` is already there. | Add the module the symbol's header lives in. |

Shadowing, unity names, `Printf` formats and brace narrowing were checked by hand and by script on
the new files: nothing else found. Not checked against an engine: whether UHT accepts the UI
`UFUNCTION`s (they use only `FString`, `FLinearColor`, `int32`, `float`, `bool`, our UENUMs and
`UObject` pointers).
