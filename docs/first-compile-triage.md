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

## Batch 3 and 4 build results (UE 5.8, 2026-10-16)

Built after two fixes; none of the ST / SV / PB / HM / HR / TR / AH / RV / NK rows fired at compile
time. Runs: headless 166/166, net 10/10, visual 8/8 (after the test fixes below). Backend smoke 70 ok,
1 skip (the expired-ticket check needs `CROSSGUARD_MATCH_TICKET_TTL_SEC=10`), 0 fail. The local
cluster (hub + match server + two clients, Nakama) played the whole loop: log in, hub, party,
matchmaking, ticket, raid, extraction, team back to its hub with the party; saves correct in Nakama.

| Cause | Fix |
| --- | --- |
| `ACrossguardGameMode` lost its closing `};` in 5d15df7 (UHT: "Found EOF") | Restored |
| `Url.IsEmpty() ? nullptr : MakeShared<...>` (no common type for TSharedRef / nullptr) | if / else in `UCrossguardNakamaSaveBackend::Configure` |
| HubReturn test read the raider's player state after extraction; unpossessing clears it (crash) | Held before `HandleExtraction` |
| GameInstance subsystems `NewObject`ed with the transient package as outer (ensure) | A bare `UGameInstance` outer |
| Hub UI tests counted the owner's Debug inventory rubies on the PIE player | `HubEmptyInventory` first |
| Art.WeaponMeshes assumed the dagger has no mesh (art now imported) | The wand |
| Cluster: `NakamaUrl=http://...` unquoted in DefaultGame.ini was read as `http:` (login: no response) | Quoted |
| Cluster: the menu's UI-only input outlived the travel to the hub (no movement, no keys) | `ACrossguardHUD::BeginPlay` sets game input |
| Cluster: players were named after the PC; invites by name found nobody | Non-Shipping: `-CrossguardPlayerId=` is the player name |
| Cluster: L_DevSandbox had no extraction zone or point of interest | `editor_setup` RAID_ACTORS |
| Party window showed `refused (AlreadySearching)`; the menu showed no reason for a refused Join | Readable lines; `UCrossguardConnectionErrorSubsystem` |

Not done: Build Paths in L_Hub (an editor click; a scripted nav build stays locked by
`AsyncLoadLock`). A restarted hub with the same `-ServerId=` takes its own leases back at once (no
60 s wait): expected.

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
| R1 | `Private/Character/CrossguardCharacter.cpp` (`PartyDecline`) | `UFUNCTION(Exec) PartyDecline(const FString& Which)` typed with no argument; `FString::IsNumeric` | Exec parsing of a missing last `FString` is believed to give an empty string (then the first invite is declined); if the console says "missing argument", that path is dead. | Type `PartyDecline 1`; or split into `PartyDeclineFirst()`. |
| R2 | `Private/Skills/CrossguardSpellSkills.cpp` (`UCrossguardSummonCompanionAbility::ActivateAbility`) | `TSubclassOf<ACrossguardMinion>::GetDefaultObject()`, `GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()` on the CDO | Believed fine in 5.x. | `GetDefault<ACrossguardMinion>(CompanionClass.Get())`. |
| R3 | Same (runtime) | The Greater Demon is now 3x (capsule about 1 m radius, 5.3 m tall), lifted to stand on the summoner's floor | Spawn uses `AdjustIfPossibleButDontSpawnIfColliding`: under a low ceiling or in a corridor it may not spawn; it may not fit doors or the NavMesh agent. Watch `Skills.SummonGreaterDemon` / `SummonGreaterDemonFights` (the feet check allows 10 cm). | Spawn it farther ahead (`CompanionDistance`) or use `AdjustIfPossibleButAlwaysSpawn`; add a large NavMesh agent if it gets stuck. |
| R4 | `Public/Game/CrossguardPlayerState.h` | `ServerRequestAttributeAllocation(Allocation, bool bPayWithGold)`; `RequestAttributeAllocation(..., bool bPayWithGold = false)` | A `BlueprintCallable` with a defaulted `bool`; UHT accepts it. Existing Blueprint calls get a new pin (false). | None expected. |
| R5 | `Private/Tests/CrossguardScenarioPartyTests.cpp` (`PartyInviteInboxDecline`) | `Registry.InvitesFor(TCHAR_TO_UTF8(*Id), 0.0)` | The `const char*` temporary becomes a `std::string` inside the call: fine. | Wrap in `std::string(...)` if a compiler objects. |

Shadowing, unity names, `Printf` formats and brace narrowing were checked by hand and by script on
the new files: nothing else found. Not checked against an engine: whether UHT accepts the UI
`UFUNCTION`s (they use only `FString`, `FLinearColor`, `int32`, `float`, `bool`, our UENUMs and
`UObject` pointers).

### Saving and parties (second review)

The save code (`CrossguardRules` `Json` / `Save`, `Game/CrossguardSave*`, `CrossguardLocalSaveBackend`,
`Tests/CrossguardScenarioSaveTests.cpp`) and the party code (`CrossguardRules` `Party`,
`Game/CrossguardParty*`, `Tests/CrossguardScenarioPartyTests.cpp`, game mode and character
`Party*` commands), reviewed by reading, plus the extraction fix (design-decisions.md, Extraction).
Checked and believed fine: the save backend `UINTERFACE` (C++-only, pure virtuals, no `UFUNCTION`s,
like `IAbilitySystemInterface`); `UCLASS(Config = Game)` with `UPROPERTY(Config)`;
`FWorldDelegates::OnWorldCleanup` handler `(UWorld*, bool, bool)`; `GIsAutomationTesting`
(`CoreGlobals.h`); every party `Replicated` property registered `COND_OwnerOnly`; `UWorld::ServerTravel`
returning `bool`; `APlayerController::ClientTravel(Url, TRAVEL_Absolute)`; the rules classes the game
calls are exported (`CROSSGUARDRULES_API`); named namespaces in the new `.cpp`s; no new shadowing found.

| # | Where | What | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| S1 | `Tests/CrossguardScenarioSaveTests.cpp` helpers | `FScenarioContext` unqualified inside `namespace CrossguardScenarioSave` | Would not compile (C2061). | **Applied**: `using CrossguardScenario::FScenarioContext;` in the namespace (as the other test files do). |
| S2 | `Game/CrossguardGameMode.cpp` `HandleExtraction` | `FTimerDelegate::CreateWeakLambda`, `FTimerManager::SetTimerForNextTick(const FTimerDelegate&)` | Both in 5.x. | If either fails: `Character->SetLifeSpan(0.1f);` instead of the next-tick timer. |
| S3 | Extraction and permanent death | `APawn::UnPossessed` clears the player state's pawn (`APlayerState::GetPawn()` null afterwards) | 5.x behaviour; the save relies on it (no pawn: the last record's carried gear), and so do `Raid.ExtractionZone` and `Save.ExtractedPlayerLeavesRaid` ("The player has no pawn"). | If it stays set: in `UCrossguardSaveSubsystem::BuildRecord` treat a pawn whose `GetController()` is null as no pawn. |
| S4 | Extraction | The pawn is unpossessed, then destroyed; the controller stays in its playing state, so `ServerRestartPlayer` does nothing for it (as after a death). | If 5.8 moves the controller to `NAME_Inactive` on unpossess, a forged `ServerRestartPlayer` would spawn a fresh pawn in the raid. | **Applied**: `ACrossguardGameMode::PlayerCanRestart` refuses on a raid map once the player state `HasFinishedRaid()` (set by `EndRaid`, reset by `BeginRaid`). |
| S5 | `APlayerState::CopyProperties` (seamless travel) | Engine order assumed: `SetPlayerId` before `SetUniqueId` | `OnSetUniqueId` keys the save and party view by `PlayerIdOf` (`session-<PlayerId>` without Steam). If the id is set after, a session-id player would load the wrong record after travel. | Steam ids are unaffected. Otherwise call `Saves->PlayerJoined(Next)` and the party refresh again at the end of `ACrossguardPlayerState::CopyProperties`. |
| S6 | `ACrossguardPlayerState::OnSetUniqueId` | Party view refreshed there too | **Applied**: seamless travel spawns the new player state (and may run the party component's `BeginPlay` refresh) before it has its id, so the raid map would show no party. | — |
| S7 | `ACrossguardGameMode::HandleSeamlessTravelPlayer` | `JoinTeam` runs after `Super`, which already spawned the pawn and began the raid (`HandleStartingNewPlayer`) | Not a build error. For that moment the team id is the hub's (copied in `CopyProperties`), possibly another party's id on the raid map. Harmless today (same frame, nothing reads teams in between). | If spawning ever depends on the team: move `JoinTeam` into an `InitSeamlessTravelPlayer` override (called before `HandleStartingNewPlayer`). |
| S8 | Raid launch (`UCrossguardPartySubsystem::StartRaid`) | `bUseSeamlessTravel = true` | PIE needs `net.AllowPIESeamlessTravel 1` (docs/parties.md); without it PIE does not travel seamlessly and the hub's player states (party keys, save records on them) are dropped; the save subsystem still has each record per id. | Set the cvar for PIE tests of the launch. |
| S9 | `CrossguardRules` `Save.h` | First `std::function` in the rules module (exceptions off in Unreal) | MSVC may warn (C4577 / C4530); warnings do not stop the build. | Nothing, or `#pragma warning(disable: ...)` in the rules module. |
| S10 | `ACrossguardGameMode::HandlePermanentDeath` | Now unpossesses before `EndRaid(false)` | **Applied**: the failure's save no longer reads the dead pawn (which would hold its gear if the body did not spawn); it keeps the raid start's record (no carried gear). | — |

Extraction behaviour changed (Scope B): `HandleExtraction` saves, then the character leaves the
world (hidden and unpossessed at once, destroyed next tick); `ACrossguardCharacter::HandleInteract`
refuses while `HasLeftRaid()`; extract portals ignore it as a keeper. `Raid.ExtractionZone` and
`Raid.ExtractPortal` now read the player state they remembered instead of the (gone) pawn; new test
`Crossguard.Scenario.Save.ExtractedPlayerLeavesRaid`.

### Bundle requirements and thrown knives (rulings)

A bundle's requirement switches off its included skills (`UCrossguardLoadoutComponent::IsSkillSwitchedOn`,
`Crossguard::ResolveLoadoutSources`; gates in `UCrossguardGameplayAbility::CanActivateAbility`, the
data passive, Multiscale and Immutable) and a thrown throwing knife gathers dagger riders
(`UCrossguardInventoryComponent::Throw`, `FCrossguardRiderAttack::ForThrown`). Reviewed by reading;
rules part unit tested.

| # | Where | What | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| B1 | `Private/Skills/CrossguardSupportSkills.cpp` `UCrossguardImmutableAbility::OnGiveAbility` | Binds `OnEquipmentChanged` and keeps state on the instance; guarded by `IsInstantiated()` | Assumes 5.8 calls `OnGiveAbility` / `OnRemoveAbility` on the per-actor instance (as 5.x does). If only the CDO gets it, `Passive.Immutable` is never added (`MithrilKnightNeedsFullPlate` and `Immutable` fail on the tag). | Move the binding to `OnAvatarSet`, or let the loadout component add / remove the tag itself on `OnEquipmentChanged`. |
| B2 | `Private/Skills/CrossguardLoadoutComponent.cpp` `RebuildSkillSources` | `TMap<FName, TArray<TArray<FName>>>` (not a `UPROPERTY`), range-for over the returned `std::unordered_map` | Nested containers are fine outside reflection. | — |
| B3 | `Private/Items/CrossguardInventoryComponent.cpp` `Throw` | `FGameplayEffectSpec::GetSetByCallerMagnitude(FGameplayTag, bool, float)` then `SetSetByCallerMagnitude` | Same calls as `CrossguardGuard.cpp`. | — |
| B4 | `Private/Skills/CrossguardAuraSkills.cpp`, `CrossguardSupportSkills.cpp` | `AddUniqueDynamic` / `RemoveDynamic` on `OnEquipmentChanged` with new `UFUNCTION()`s (`OnOwnerEquipmentChanged`, `UpdatePassiveImmunity`) | UHT needs the `UFUNCTION()` (present). | — |

### Reconnecting mid-raid (ruling)

A raider whose connection drops is held for a reconnect: new `ACrossguardPlayerController`
(`PawnLeavingGame`, `CleanupPlayerState` overrides; set as the game mode's `PlayerControllerClass`),
`ACrossguardGameMode` `Login` override, `HoldForReconnect` / `RestoreHeldPlayer` / `ResumeHeldCharacter`
/ `ReleaseHeldPlayer`, development player ids (`ACrossguardPlayerState::DevPlayerId`, replicated;
`DevPlayerIdFor`), tests `Tests/CrossguardScenarioReconnectTests.cpp`. Docs: persistence.md
"Reconnecting". Reviewed by reading; nothing built.

| # | Where | What | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| RC1 | `Public/Game/CrossguardPlayerController.h` | Overrides `APlayerController::PawnLeavingGame()` and `CleanupPlayerState()` (declared public here) | Both virtual in 5.x (`PawnLeavingGame` destroys the pawn; `APlayerController::CleanupPlayerState` calls `PlayerState->OnDeactivated()`, which destroys it). If C3668 (not virtual / renamed): | Hold in an `OnNetCleanup(UNetConnection*)` override instead (before `Super`), and keep the player state by overriding `ACrossguardPlayerState::OnDeactivated()` (skip `Destroy` when held). |
| RC2 | Disconnect order (runtime) | Assumed 5.x order in `APlayerController::Destroyed`: pawn handled first (`PawnLeavingGame` when `Player == nullptr` on the server, which a closed connection and the tests' controllers both are), then `AController::Destroyed` → `GameMode->Logout` → `CleanupPlayerState` | If `Logout` runs first, or `PawnLeavingGame` is not called for a closed connection, the pawn is destroyed and `Reconnect.*` fail at "the raider's character was not destroyed". | Call `HoldForReconnect` from an `OnNetCleanup` override (and from `Destroyed` before `Super` for the tests). |
| RC3 | `ACrossguardGameMode::Login` | Override of `AGameModeBase::Login(UPlayer*, ENetRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl&, FString& ErrorMessage)` | Signature as of 5.0-5.5. If C3668: | Copy the 5.8 signature from `GameModeBase.h`. Only used to tell this process's own player (a `ULocalPlayer`) for `pie-<n>` ids. |
| RC4 | `HoldForReconnect`, `ReleaseHeldPlayer` | `APawn::SetPlayerState(APlayerState*)` called from the game mode | Believed public in 5.x. If C2248 (protected): | Give `ACrossguardCharacter` a public wrapper `void SetHeldPlayerState(APlayerState* State) { SetPlayerState(State); }`. |
| RC5 | `RestoreHeldPlayer` | `Controller->PlayerState = HeldState` (public `TObjectPtr` member), `SetOwner`, `ForceNetUpdate`, fresh player state `Destroy()` | What `AGameMode::FindInactivePlayer` does in 5.x. If the member went private: | `Controller->SetPlayerState(HeldState)` if 5.8 has it. |
| RC6 | `DevPlayerIdFor` | `UEngine::GetWorldContextFromWorld(const UWorld*)` → `FWorldContext::WorldType`, `PIEInstance`; `FParse::Param` / `FParse::Value`; `FChar::IsAlnum`; range-for over a temporary `FString` | 5.x APIs. | `GetWorld()->GetOutermost()->GetPIEInstanceID()` for the instance; `TChar<TCHAR>::IsAlnum`; index loop. |
| RC7 | `HoldForReconnect` | `UWorld::bIsTearingDown`, `UCharacterMovementComponent::bRunPhysicsWithNoController` (public members) | 5.x. | Drop the teardown check (`HasLeftRaid` / map end does not destroy controllers one by one); `SetRunPhysicsWithNoController` if a setter replaced the flag. |
| RC8 | `ResumeHeldCharacter` | `UAbilitySystemComponent::SetNumericAttributeBase` after `Possess` (possession sets Health to full); `APlayerController::ClientSetRotation(FRotator, bool = false)` | 5.x. Runtime: if Health comes back full, `PossessedBy` ran later than assumed. | Restore Health on the next tick (`SetTimerForNextTick`). |
| RC9 | `FCrossguardHeldPlayer` | A plain (non-`USTRUCT`) struct in `CrossguardGameMode.h` holding `TWeakObjectPtr`s, used in a non-`UPROPERTY` `TMap` | Fine for UHT (not reflected). | — |
| RC10 | Blueprint game mode | `PlayerControllerClass = ACrossguardPlayerController` in the C++ constructor | If `BP_CrossguardGameMode` saved its own Player Controller Class, the C++ default does not reach it: no hold (the pawn is destroyed on disconnect, as before) and `Reconnect.*` skip with a message. | Set Player Controller Class to `CrossguardPlayerController` on the Blueprint. |

### Demon reach and next-attack skills on throws (rulings)

The Greater Demon's reach is its base 2 m times its scale (`UCrossguardSummonCompanionAbility::ActivateAbility`:
`Minion.AttackRange = AttackRange * Scale`, 6 m). Every next-attack skill whose weapon accepts a thrown
knife (Dagger) or axe (Axe) rides on the throw (`GatherRiders`: a throw with a weapon type counts as a
melee attack with it; riposte excluded); `UCrossguardInventoryComponent::Throw` applies the weapon
multiplier and flat bonus to the throw's damage and hands the Silence, Grounding and Mortal Strike flags
to the projectile; `ACrossguardProjectile::HandleImpact` now applies on-hit riders through
`UCrossguardWeaponAttackAbility::ApplyRiders` / `FinishMeleeHits` (arrows behave as before). New tests
`Skills.ThrownKnifeSlash`, `Skills.ThrownAxeChopDown`. Reviewed by reading; nothing built.

| # | Where | What | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| T1 | `Private/Combat/CrossguardProjectile.cpp` | Includes `Combat/CrossguardWeaponAttackAbility.h`; `FinishMeleeHits(Landed, ShooterASC, {OtherActor})` builds a `TArray<AActor*>` from a braced list | Same call shape as `ACrossguardAICharacter::TryAttack` (`{Target}`). | `TArray<AActor*>{OtherActor}` or a named local. |
| T2 | `Skills.ThrownAxeChopDown` (runtime) | `chop_down` activates with a throwing axe in hand (held throwable counts as an Axe for `SatisfiesWeaponRequirement`) | If the held-throwable check does not reach Chop Down's `RequiredWeapons`, the test stops at "Chop Down activates". | Check `UCrossguardEquipmentComponent::SetHeldThrowable` ran (key 3 readied the axes). |
| T3 | Greater Demon (runtime) | 6 m reach: it swings from farther away; `MoveToward(..., AttackRange * 0.5)` stops it 3 m from its target | `SummonGreaterDemonFights` expects hits on a dummy 4 m from the warlock; should still pass (more reach, not less). | — |

## Batch 3 (unbuilt)

### Seamless travel test (`Crossguard.Net.SeamlessTravelHubToRaid`)

New `Tests/CrossguardScenarioTravelTests.cpp` (namespace `CrossguardScenarioTravel`, helpers prefixed
`Seamless`) and `FScenarioContext::ExpectServerTravel` in the scenario framework (the runner's
`FollowServerTravel`: items pause until PIE instance 0's world is the destination, then the context
follows it). Reviewed by reading; nothing built or run.

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| ST1 | Test, first step | `IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"))`, `IConsoleVariable::Set(..., ECVF_SetByCode)`, `GetInt`, `GetString` | The cvar name is from memory (5.x lifts the "Seamless travel currently NOT supported in single process PIE" refusal in `AGameModeBase::CanServerTravel` with it). If it does not exist the test skips with a warning; if it exists but the engine still refuses, `StartRaid` returns `TravelFailed` and the test says so. | Search `GameModeBase.cpp` (`CanServerTravel`) for the 5.8 switch and use its name. If 5.8 has none: the fallback is to call `CopyProperties` + `HandleSeamlessTravelPlayer` on a cloned player state by hand (not written). |
| ST2 | Runtime: PIE seamless travel | No `TransitionMap` in `Config/DefaultEngine.ini` (`GameMapsSettings`) | Assumed: with none, `FSeamlessTravelHandler` uses an empty transient transition world. If PIE logs a transition-map error or the travel stalls ("did not arrive ... in time" with the server world still the old one): | Make an empty saved map (e.g. `/Game/Maps/L_Transition`) and set `TransitionMap=/Game/Maps/L_Transition.L_Transition` under `[/Script/EngineSettings.GameMapsSettings]`. |
| ST3 | `CrossguardScenario.cpp` `FollowServerTravel`, test `SeamlessIsRaidMap` | `UWorld::RemovePIEPrefix(World->GetPackage()->GetName())` compared with `/Game/Maps/Dev/L_DevSandbox` | Assumed the PIE destination package is `/Game/Maps/Dev/UEDPIE_<n>_L_DevSandbox`. If the name differs, the travel "never arrives" (the error names the server world's package). | Compare `FPackageName::GetShortName` of both, or `World->GetMapName()` ending with `L_DevSandbox`. |
| ST4 | Test, `RunTest` | `FPackageName::DoesPackageExist(FString)` | 5.x also has `FPackagePath` overloads; the explicit `FString` picks the string one. | `FPackageName::DoesPackageExist(SeamlessRaidMap, nullptr)`. |
| ST5 | Runtime: the editor during PIE seamless travel | `GEditor->PlayWorld`, `GEditor->GetPIEWorldContext(0)->World()` follow the travel | The runner reads instance 0's context world; `PlayWorld` only to tell whether PIE runs. If PIE ending after the travel hangs ("PIE did not end within 30 s"), the editor lost track of the travelled world. | End PIE from the context: `GEditor->RequestEndPlayMap()` is already used; report it (engine side). |
| ST6 | Runtime: engine order in seamless travel | `APlayerState::CopyProperties` copies the engine `PlayerId` before `SetUniqueId` (S5) | Client 1 has no `DevPlayerId` (a remote PIE client), so its save id is `session-<PlayerId>`; the test is the check for S5. If "player id (save and party key)" or "save still tracked" fails for client 1 only: | S5's fix: call `Saves->PlayerJoined(Next)` and the party refresh again at the end of `ACrossguardPlayerState::CopyProperties`. |
| ST7 | Runtime: the raid map | `L_DevSandbox`'s game mode must be a `CrossguardGameMode` with `bIsRaid` on (the C++ default; `editor_setup` does not change it) | If the Blueprint's `bIsRaid` is off, no raid begins on arrival and the arrival wait times out. | Leave `bIsRaid` on in `BP_CrossguardGameMode` (test maps use `?Hub` or `SetRaid`). |
| ST8 | Runtime: carried gear | The hub record is taken by `MarkDirty` when the inventory / equipment changes (the watcher) and by `PlayerReady` | **Applied**: `UCrossguardPartySubsystem::StartRaid` now saves the travellers (`SaveNow`, `FlushPendingSaves`) before travelling, and `ACrossguardGameMode::ReturnToHub` does the same on the way back. A watcher miss no longer shows in this test. | — |

### Stash and vendor screens (`Crossguard.Scenario.UI.StashAndVendor`, `Crossguard.Visual.HubUI`)

New `UI/CrossguardStashWidget`, `UI/CrossguardVendorWidget`, `Private/UI/CrossguardHUDHubWindows.cpp`
(more `ACrossguardHUD` members; namespace `CrossguardHUDHubLocal`), `Interaction/CrossguardStashChest`,
`Interaction/CrossguardVendorNPC`, `Tests/CrossguardScenarioHubUITests.cpp` (namespace
`CrossguardScenarioHubUI`, helpers prefixed `Hub`); `OpenStashScreen` / `OpenVendorScreen` client RPCs
on the stash and vendor components; `ICrossguardInteractable::GetInteractPrompt`. Reviewed by
reading; nothing built or run.

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| SV1 | `Public/UI/CrossguardHUD.h` | `UFUNCTION(Exec) void OpenStash()` / `OpenVendor()` on the HUD | Assumed the console reaches the HUD's exec functions (as `AHUD::ShowHUD`). If `OpenStash` says "Command not recognized": | Move the two exec functions to `ACrossguardCharacter` (calling the HUD's `ShowStashWindow(nullptr)`). |
| SV2 | `Private/UI/CrossguardStashWidget.cpp` `IsFromStash` | `Drag.GetOuter() == StashGrid` | Relies on `UCrossguardItemGridWidget::StartDrag` creating the operation with the grid as outer. If drags from the stash are stored again instead of moved / taken: | Add a `SourceGrid` (weak) property to `UCrossguardItemDragOperation`, set in `StartDrag`, and compare that. |
| SV3 | `Private/UI/CrossguardVendorWidget.cpp` | `MakeButton(Label, nullptr, 13)` (`TFunction<void()>` from `nullptr`) | `TFunction` has a null constructor; if the overload is ambiguous: | `TFunction<void()>()`. |
| SV4 | `Private/UI/CrossguardHUDHubWindows.cpp`, `Private/UI/CrossguardHudWidget.cpp` | `Cast<ICrossguardInteractable>(const AActor*)` returning `const ICrossguardInteractable*` | Same shape as the HUD widget's interact search. | `Cast<const ICrossguardInteractable>` or a `const_cast` of the actor. |
| SV5 | `Private/Interaction/CrossguardStashChest.cpp`, `...VendorNPC.cpp` | `ConstructorHelpers::FObjectFinder<UStaticMesh>` for `/Engine/BasicShapes/Cube.Cube`, `Cylinder.Cylinder` | As the gather node's sphere. | — |
| SV6 | `Private/Tests/CrossguardScenarioHubUITests.cpp` | `if (ACrossguardHUD* Hud = ...; Hud != nullptr && ...)` (C++17 if with initializer) | UE 5.8 builds C++20; only an older toolchain setting would refuse it. | Split into a declaration and an `if`. |
| SV7 | Runtime: `Scenario.UI.StashAndVendor` | `ClientOpenStash` / `ClientOpenVendor` on the PIE player's (standalone) player state run locally | Assumed a client RPC on a standalone server runs at once (as `ClientStashRejected`). If "the HUD shows the vendor screen" fails while "Log: the vendor opened" passes: | Check the HUD bound the components (`TickHubWindows` ran); broadcast `OnVendorOpened` directly when `GetNetMode() == NM_Standalone`. |
| SV8 | `Tools/editor_setup.py` `add_hub_actors` | `LevelEditorSubsystem.load_level` on an existing map inside the commandlet; `EditorActorSubsystem.get_all_level_actors`; `isinstance(actor, unreal.CrossguardStashChest)` | Python API from memory. | Run with `-Mode Editor`, or place them by hand: drag `CrossguardStashChest` and `CrossguardVendorNPC` from Place Actors (search) next to the player start and save. |

### Attributes, party windows, disconnected marker (`Crossguard.Scenario.UI.PanelsBind`, `Crossguard.UI.RarityContrast`, `Crossguard.Net.ReconnectShowsDisconnected`)

New `UI/CrossguardAttributesWidget`, `UI/CrossguardPartyWidget`, `UI/CrossguardTeamListWidget`,
`Private/UI/CrossguardHUDSocialWindows.cpp` (more `ACrossguardHUD` members; namespace
`CrossguardHUDSocialLocal`), `Tests/CrossguardScenarioUIPanelTests.cpp` (namespace
`CrossguardScenarioUIPanels`); `ACrossguardPlayerState::bDisconnected` (replicated) and
`SetDisconnected`; `FCrossguardPartyInviteView::SecondsLeft` and
`UCrossguardPartyComponent::GetInviteSecondsLeft`; `ACrossguardCharacter::Respec` (Exec);
`CrossguardUI::RarityTextColor` / `RarityBackgroundColor` / `StyleRarityButton` / `ContrastRatio`.
Reviewed by reading; nothing built or run.

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| AP1 | `Public/Character/CrossguardCharacter.h` | `UFUNCTION(Exec) void Respec(const FString& Arguments)` | Relies on the console putting the rest of the line into the last `FString` parameter (`CallFunctionByNameWithArguments`). If only the first number arrives ("Respec 12 10 8" prints the usage): | Make it `Respec(int32 Str, int32 Agi, int32 Int, const FString& Payment)` (the trailing string may be empty). |
| AP2 | `Private/UI/CrossguardAttributesWidget.cpp`, `CrossguardPartyWidget.cpp` | `OnProgressionRejected.AddUniqueDynamic(this, &UCrossguardAttributesWidget::HandleProgressionRejected)` (and `OnPartyRefused`) from a widget | Standard dynamic binding; the handler must be a `UFUNCTION` (it is). | — |
| AP3 | `Private/Game/CrossguardPartyComponent.cpp` | `UWorld::GetRealTimeSeconds()` returning `double` | If 5.8 returns `float`, the `FMath::Max(0.0, ...)` mixes types. | `FMath::Max<double>(...)`. |
| AP4 | `Private/UI/CrossguardHUDSocialWindows.cpp` | `TArray<UCrossguardWindowWidget*>{ AttributesWidget.Get(), PartyWidget.Get() }`; `EKeys::K`, `EKeys::P` | Derived-to-base pointers in an initializer list; needs the widget headers (included). | Build the array with `Add`. |
| AP5 | `Private/UI/CrossguardTeamListWidget.cpp` | `UCanvasPanelSlot::SetAutoSize`, anchors (1, 0) | As the HUD widget's pins. | — |
| AP6 | `Private/Tests/CrossguardScenarioReconnectTests.cpp` (`Net.ReconnectShowsDisconnected`) | `C.SpawnPlayer` and the reconnect helpers in a **networked** (dedicated server) scenario | First use of `SpawnPlayer` with `Clients(N)`: a server-side controller without a connection, marked local (`SetAsLocalPlayerController`). If the raider does not spawn, or its player state never reaches the client: | Use a real client as the raider and hold it with `GameMode->HoldForReconnect(ServerControllerOf(1))` directly (then `ResumeHeldCharacter`). |
| AP7 | `Private/Tests/CrossguardScenarioUIPanelTests.cpp` | `CROSSGUARD_TEST_FLAGS` from `CrossguardTestUtils.h` beside `CROSSGUARD_SCENARIO_FLAGS` in one file | Both headers in one test file is new. | Use `CROSSGUARD_SCENARIO_FLAGS` for `Crossguard.UI.RarityContrast` too. |
| AP8 | Runtime: `Scenario.UI.PanelsBind` | Party and attribute requests from the PIE player run at once (standalone server RPCs) | As `WidgetsBind`'s `RequestCreate`. If "the invite founded a party" fails while `PanelFriend` exists: | Call `UCrossguardPartySubsystem::Invite` directly in the test and keep `InviteByName` for the lookup only. |

### Main menu, hub map, raid portal (`Crossguard.Menu.Urls`, `Crossguard.Scenario.Hub.MainMenuBinds` / `HubMarker` / `RaidPortal`)

New `Game/CrossguardMenuGameMode` (+ `ACrossguardMenuHUD`), `Game/CrossguardHubMarker`,
`Game/CrossguardSettingsSave`, `UI/CrossguardMainMenuWidget` (namespace `CrossguardMainMenuLocal`),
`Interaction/CrossguardRaidPortal` (`CrossguardRaidPortalLocal`), `Interaction/CrossguardPartyBoard`,
`Tests/CrossguardScenarioHubMenuTests.cpp` (`CrossguardScenarioHubMenu`, helpers prefixed `HubMenu`);
edits to `ACrossguardGameMode` (`IsHubMap`, `MenuMapName`, `ReturnToMenu`, `RaidMapName` default) and
`ACrossguardPlayerController` (settings, `ClientShowMessage`, `ClientOpenPartyPanel`);
`Tools/editor_setup.py` `-CreateHub` / `-CreateMenu`. Reviewed by reading; nothing built or run.

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| HM1 | `Private/Game/CrossguardSettingsSave.cpp` | `GEngine->GetMainAudioDevice()` → `FAudioDeviceHandle::GetAudioDevice()` → `FAudioDevice::SetTransientPrimaryVolume` (`AudioDevice.h`) | Name from memory (was `SetTransientMasterVolume` before 5.1). | Use whichever exists; or drop volume to a sound-class mix later. Needs `AudioMixerCore` only if `AudioDevice.h` is not found from Engine. |
| HM2 | `Private/Game/CrossguardPlayerController.cpp` | `virtual void AddYawInput(float)` / `AddPitchInput(float)` overrides on `APlayerController`; `SetPawn` override | Assumed still virtual in 5.8. If "marked override but does not override": | Scale the look axis in the character's `Look` instead (`AddControllerYawInput(Axis.X * Sensitivity)`). |
| HM3 | `Private/Game/CrossguardHubMarker.cpp` | `UWorld::GetLevels()`, `ULevel::Actors` during `InitGame` | Assumed the persistent level's actors are loaded when the game mode is spawned (they are for non-World-Partition maps). If L_Hub opened without `?Hub` still plays as a raid: | Check the map is not World Partition; or also re-check in `StartPlay` before `Super`. |
| HM4 | `Private/Game/CrossguardGameMode.cpp` `ReturnToMenu` | `FPackageName::DoesPackageExist` (long package name), `APlayerController::ClientTravel(URL, TRAVEL_Absolute)` to a local map from a connected client | Assumed it disconnects and loads the menu standalone; the `?MenuReason=` option reaches the menu game mode's `InitGame`. | Fall back to `ClientReturnToMainMenuWithTextReason` (empty `MenuMapName`). |
| HM5 | `Private/UI/CrossguardMainMenuWidget.cpp` | `UEngine::SetClientTravel(UWorld*, const TCHAR*, ETravelType)`; `UKismetSystemLibrary::QuitGame(..., EQuitPreference::Quit, false)`; `UCanvasPanelSlot::SetAutoSize` | Signatures from memory. | `UGameplayStatics::OpenLevel(World, FName(Map), true, TEXT("listen?Hub"))` for Play; `PC->ClientTravel` for Join. |
| HM6 | `Private/Game/CrossguardMenuGameMode.cpp` | `FInputModeUIOnly::SetWidgetToFocus(MainMenu->TakeWidget())`; `DefaultPawnClass = nullptr` with `HandleStartingNewPlayer` not calling Super | No pawn, camera at the origin. | — |
| HM7 | `Private/Tests/CrossguardScenarioHubMenuTests.cpp` | `FURL(nullptr, *Url, TRAVEL_Absolute)`, `FURL::HasOption`, `FURL::Map`; `TestNotEqual(float)` | As the party test's `FURL` use. | `Url.Contains(TEXT("?listen"))`. |
| HM8 | Runtime: `Scenario.Hub.RaidPortal` | `Interact` on a `SpawnPlayer` member calls `ClientShowMessage` (client RPC on a server-side controller with no connection: runs locally, no HUD, no-op) | Assumed harmless. | — |
| HM9 | `Tools/editor_setup.py` `create_hub_map` / `create_menu_map` | `set_editor_property("default_game_mode", unreal.CrossguardMenuGameMode.static_class())`; spawning `CrossguardHubMarker` (an `AInfo`) with `spawn_actor_from_class`; `new_level` making a non-World-Partition map | Python API from memory. | Run with `-Mode Editor`; or place `CrossguardHubMarker` by hand and set World Settings → GameMode Override. |
| HM10 | `Tools/editor_setup.py` `set_game_default_map` | Text edit of `Config/DefaultEngine.ini` `[/Script/EngineSettings.GameMapsSettings]` | The running commandlet keeps the old value in memory (harmless). | Project Settings → Maps & Modes → Game Default Map. |

### Back to the hub (`Crossguard.Scenario.HubReturn.*`, `Crossguard.Net.SeamlessTravelHubToRaid` way back)

`ACrossguardGameMode`: `HubMapName`, `HubReturnCountdownSeconds`, `HeldPlayerReturnGraceSeconds`,
`UpdateHubReturn` / `HubReturnTick` / `ReturnToHub`, `BuildHubReturnUrl`, `ReleaseHeldPlayer(Id,
bRaidOver)`, namespace `CrossguardGameModeLocal`; `UCrossguardPartySubsystem::DropAbsentMembers` and
saves before `StartRaid`'s travel; `UCrossguardHudWidget::GetMessageLine`; new
`Tests/CrossguardScenarioHubReturnTests.cpp` (namespace `CrossguardScenarioHubReturn`, helpers
prefixed `HubReturn`); the travel test's way back. Reviewed by reading; nothing built or run.

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| HR1 | `Public/Game/CrossguardGameMode.h` | `#include "TimerManager.h"` for the `FTimerHandle` member | `FTimerHandle` lives in `Engine/TimerHandle.h` in 5.x; `TimerManager.h` includes it (the save subsystem header does the same). | `#include "Engine/TimerHandle.h"`. |
| HR2 | `CrossguardGameMode.cpp` `UpdateHubReturn` / `HubReturnTick` | `UWorld::GetPlayerControllerIterator()`, `FConstPlayerControllerIterator`, `It->Get()` | As the scenario framework uses them. | — |
| HR3 | `CrossguardGameMode.cpp` `UpdateHubReturn` | `UWorld::GetTimeSeconds()` taken as `double`; `FMath::Max(0.0f, float)` into a `double` | If 5.8 returns `float`, still fine (widened). | `static_cast<double>(...)`. |
| HR4 | `CrossguardGameMode.cpp` `ReturnToHub` | `UWorld::ServerTravel(Url, true)` from a timer callback on a raid map, seamless | As `StartRaid`. Assumed `?Hub` and `?listen` in the URL reach the hub game mode's `InitGame` options after a seamless travel; the hub marker turns `bIsRaid` off anyway. If the hub plays as a raid: | Check the marker is in `L_Hub`'s persistent level (HM3). |
| HR5 | `CrossguardGameMode.cpp` `Logout` | `SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, ...))` | Same shape as `HandleExtraction`'s. | — |
| HR6 | Runtime: hub arrival | `HandleSeamlessTravelPlayer` → `HandleStartingNewPlayer` → `RestartPlayer` for a controller that had no pawn (extracted / dead, unpossessed) | Assumed the engine spawns a pawn for it as for the raid launch (`PlayerCanRestart` is true in the hub: new player states, `bIsRaid` off). If a traveller arrives without a pawn ("both travellers have a pawn" times out): | Call `RestartPlayer(C)` at the end of `HandleSeamlessTravelPlayer` when `!bIsRaid && C->GetPawn() == nullptr`. |
| HR7 | Runtime: `Scenario.HubReturn.LastExtractionStartsCountdown` | `HandleExtraction` on the PIE player's own character; `ClientShowMessage` on the local controller runs at once (standalone) | If "HUD: the countdown line" fails while the log line passes: the HUD widget was not built yet in the test world. | Accept the warning path, or check after a `Wait(0.1f)`. |
| HR8 | Runtime: `Net.SeamlessTravelHubToRaid` way back | The extracted pawns are destroyed next tick; the save record keeps their gear (no pawn: `Last.Carried`) | If "carried items (gold coins banked)" fails in the hub: the extraction save or `ReturnToHub`'s `SaveNow` lost the gear. | Compare the record built right after `HandleExtraction` (`BuildRecord`) with the hub's inventory to see which step dropped it. |
| HR9 | `CrossguardPartySubsystem.cpp` `DropAbsentMembers` | Iterates `Registry.All()` (an `std::unordered_map`), then leaves; `SyncParty` on a raid map (`SetPlayerParty`) | Collected before changing. Moves remaining members' teams just before the travel (harmless; `HandleSeamlessTravelPlayer` joins them again). | — |

### Team return (`Crossguard.Scenario.HubReturn.TeamToHubServer` / `DevWholeServerFallback` / `NoHubServerToMenu`)

`ACrossguardGameMode`: per-team counting in `UpdateHubReturn`, `TeamHubReturnTick`, `ReturnTeamToHub`,
`CancelTeamHubReturn`, `TellTeam`, `IsServerOwnPlayer`, `HubServerAddress` / `GetHubServerAddress`
(`-HubServer=`), `GetHubDestination` / `HasTripBack`, `FCrossguardHubDestination`,
`BuildTeamHubJoinUrl`, `FCrossguardTeamHubTravel` / `ECrossguardTeamHubTravel` /
`FCrossguardTeamHubCountdown` (plain C++ at file scope), `DeadBodies`, `WasSentToMenu`,
`HoldForReconnect` skipping `LeavingForHubIds`; `Tests/CrossguardScenarioTeamReturnTests.cpp`
(namespace `CrossguardScenarioTeamReturn`, helpers prefixed `TeamReturn`). Reviewed by reading;
nothing built or run.

**Removed (owner's dedicated-server architecture):** the leader-hosts-the-hub path
(`RemoteAddressOf`, `TeamHubJoinDelaySeconds`, `TeamHubListenPort`, `BuildTeamHubHostUrl`,
`TeamHubTestLeaderAddress`, `ACrossguardPlayerController::ClientTravelToHub` / `TravelToHubNow`),
`UCrossguardPartySubsystem::JoinReturningParty` and `Crossguard::FPartyRegistry::Join`; tests
`TeamFinishesEarly` and `SoloFinisherHostsHub` replaced. TR1, TR2, TR5 and TR9 are gone with them.

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| TR1 | — | **Removed** (leader address lookup is gone). | — | — |
| TR2 | — | **Removed** (no joining the leader's IP). | — | — |
| TR3 | `CrossguardGameMode.cpp` `UpdateSpectators`, test `TeamReturnWatches` | `APlayerController::PlayerCameraManager` (`TObjectPtr`) → `APlayerCameraManager::PendingViewTarget.Target.Get()` (`Camera/PlayerCameraManager.h`) | `FTViewTarget::Target` assumed a `TObjectPtr<AActor>` (5.x). If a raw pointer (C2228 on `.Get()`): | Drop `.Get()`. |
| TR4 | `CrossguardGameMode.cpp` `IsServerOwnPlayer` | `Cast<ULocalPlayer>(Player->Player)` | `APlayerController::Player` is a public `TObjectPtr<UPlayer>` in 5.x; `Cast` takes it. | `Player->GetLocalPlayer() != nullptr`. |
| TR5 | — | **Removed** (`ClientTravelToHub`; the server calls the engine's `ClientTravel`). | — | — |
| TR6 | `CrossguardGameMode.cpp` `UpdateHubReturn` | Local `struct FRaidCount` in a member function; `FRaidCount& Team = TeamId >= 0 ? ByTeam.FindOrAdd(TeamId) : Unused;` | Plain C++; `TMap::FindOrAdd` returns a reference. | Use `FindOrAdd` into a pointer and a separate unused local. |
| TR7 | `CrossguardGameMode.cpp` `ReturnToHub` | `TSet<FString>::Union` | 5.x API (returns a new set). | Copy `SentToMenuIds` and `Append(LeavingForHubIds)`. |
| TR8 | Runtime: `Scenario.HubReturn.TeamToHubServer` | Scenario players (`SpawnPlayer`) have no `ULocalPlayer`, so they count as remote: they may leave early; the PIE player (with a `ULocalPlayer`) is the server's own player and waits while others raid | If team A never counts down (log "waiting for the other teams"): `SpawnPlayer` controllers got a `UPlayer`. | Check `IsServerOwnPlayer`; compare `Player->Player` for the PIE player and a spawned one. |
| TR9 | — | **Removed** (no party rebuilt by key on a leader's hub). | — | — |
| TR10 | `Game/CrossguardDevMode.cpp` | `TWeakObjectPtr<const UWorld>` at namespace scope; `UWorld::WorldType == EWorldType::PIE`; `FParse::Param(FCommandLine::Get(), TEXT("DevListenHub"))` | A static `TWeakObjectPtr` before UObject init is fine (empty). In Shipping `World` is unused (C4100 is off in UE). | `#include "UObject/WeakObjectPtr.h"`. |
| TR11 | `Game/CrossguardMatchmaking.h` | Plain C++ `ICrossguardMatchmaking` / `FCrossguardNoMatchmaking` / `FCrossguardDevMatchmaking` before a `UCLASS` in one header; `TSharedPtr<ICrossguardMatchmaking>` member of a `UGameInstanceSubsystem` (not a `UPROPERTY`) | UHT ignores plain classes. Unused parameters in inline overrides. | Move the plain classes to their own header if UHT complains. |
| TR12 | Tests `PartyRaidLaunchPlan`, `NoHubServerToMenu` | `MakeShared<FPartyTestMatchmaking>()` into `SetMatchmaking(TSharedPtr<ICrossguardMatchmaking>)`; `SetDevListenModeOverrideForTests(World, false)` (bool → `TOptional<bool>`) | Implicit conversions assumed. | `TSharedPtr<ICrossguardMatchmaking>(MakeShared<...>())`; `TOptional<bool>(false)`. |
| TR13 | `CrossguardGameMode.cpp` `ExitForMatchOver` / `CloseRaidForTime` | `FPlatformMisc::RequestExit(false)`; `DECLARE_MULTICAST_DELEGATE(FCrossguardMatchOver)` member; `if (init; cond)` with a `Cast` in the init | `RequestExit` may want a call-site string in 5.8 (deprecation warning only). | `FPlatformMisc::RequestExitWithStatus(false, 0)`. |

### Art hooks (`Crossguard.Scenario.Art.*`)

New `Combat/CrossguardCosmetics` (structs `FCrossguardAttackCue`, `FCrossguardProjectileVisual`,
`FCrossguardBodyVisual`; namespace `CrossguardCosmetics`), `Character/CrossguardWeaponVisualsComponent`
(namespace `CrossguardWeaponVisualsLocal`), `Private/Character/CrossguardCharacterCosmetics.cpp` (more
`ACrossguardCharacter` members), `Tests/CrossguardScenarioArtHookTests.cpp` (namespace
`CrossguardScenarioArtHooks`, helpers prefixed `Art`). Art fields in the weapon, item and mob rows;
`Niagara` module dependency and plugin entry. Reviewed by reading; nothing built or run.

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| AH1 | `Crossguard.Build.cs`, `Crossguard.uproject` | `PrivateDependencyModuleNames.Add("Niagara")`; plugin `Niagara` enabled | Niagara is on by default; the explicit plugin entry silences UBT's "should list plugin" warning. A link error on `UNiagaraFunctionLibrary` / `Z_Construct_UClass_UNiagaraSystem_NoRegister` means the module is missing. | Keep both entries; check the plugin is not disabled in the editor's Plugins window. |
| AH2 | `CrossguardCosmetics.cpp` | `UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, System, Location, Rotation)`, `SpawnSystemAttached(System, Parent, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true)` | Signatures from memory (5.x). | Match `NiagaraFunctionLibrary.h`; the extra defaulted parameters (`ENCPoolMethod`, `bPreCullCheck`) can stay defaulted. |
| AH3 | Weapon attack ability, `UCrossguardSkillAbility::CommitOrCancel` | `ActivationInfo.GetActivationPredictionKey().IsValidKey()` (server: was it predicted by the owning client?) | Accessor name from memory. If it does not exist: | Use `ActivationInfo.PredictionKeyWhenActivated.IsValidKey()`, or `GetCurrentActivationInfo()`. Worst case pass `!Attacker->IsLocallyControlled() && Attacker->IsPlayerControlled()`. |
| AH4 | `CrossguardAttributeSet.cpp` | `Data.EffectSpec.GetPeriod()`, `GetContext().GetHitResult()`, `GetSetByCallerMagnitude(Tag, false, 1.0f)` | `GetPeriod` from memory. | `Data.EffectSpec.Period` (public member), or `Def->Period.GetValue()`. |
| AH5 | `CrossguardCharacter.h`, `CrossguardProjectile.h` | `FVector_NetQuantize` RPC parameters, `#include "Engine/NetSerialization.h"` | Standard; listed for completeness. | Plain `FVector`. |
| AH6 | `CrossguardWeaponVisualsComponent.cpp` | `NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(...))`, `RegisterComponent`, `AttachToComponent(..., SnapToTargetNotIncludingScale, Socket)`, `SetOnlyOwnerSee`, `SetOwnerNoSee`, `bOnlyOwnerSee` / `bOwnerNoSee` read in the test | Standard runtime components. If the test reads the bitfields as private in 5.8: | Use `UPrimitiveComponent::GetOnlyOwnerSee()` / `GetOwnerNoSee()` if they exist, else drop those two checks. |
| AH7 | `CrossguardAICharacter.cpp` | `USkeletalMeshComponent::SetSkeletalMeshAsset`, `SetAnimInstanceClass`, `TSoftClassPtr<UAnimInstance>::LoadSynchronous` | `SetSkeletalMeshAsset` is the 5.1+ name (the loot container uses it already). | — |
| AH8 | Data table import (`Tools/editor_setup.py`) | `MeshOffset` / `MeshScale` as `{"X","Y","Z"}`, `MeshRotation` as `{"Pitch","Yaw","Roll"}`, soft paths as `""` | Assumed: the JSON importer reads nested struct objects and empty soft paths. If a reimport warns on those columns: | Export them as text, e.g. `"(X=0,Y=0,Z=0)"` / `"(Pitch=0,Yaw=0,Roll=0)"` (`Tools/export_weapon_table.py`, `export_mob_table.py`). |
| AH9 | `CrossguardProjectile.cpp` | `DOREPLIFETIME_CONDITION(..., Visual, COND_InitialOnly)` with `TSoftObjectPtr` members; `MulticastImpact` (Reliable) right before `Destroy()` | Assumed: the initial bunch carries `Visual` before the client's `BeginPlay`, and a reliable multicast sent before `Destroy` arrives. If clients show no mesh / impact: | Use `ReplicatedUsing` with an `OnRep_Visual` calling `ApplyVisual`; for impacts, `SetLifeSpan(0.1f)` + hide instead of `Destroy`. |
| AH10 | Runtime: `Scenario.Art.WeaponMeshes` | The character Blueprint's mesh has `hand_r` / `hand_l` | An unknown socket attaches to the mesh origin (the test still passes). | — |
| AH11 | Runtime: `Scenario.Art.AttackAndDamageEvents` | The scenario dummy has no guard; the 1.5 m sword swing lands | If "dummy's OnDamageTaken fired" is 0: the swing missed (see `Raid.KillDummyLeavesBody`). | — |

### Review (batch 3, by reading)

A second pass over batch 3 (commits after `2305549`, except the return-to-hub code): UHT rules
(UFUNCTION parameters vs UPROPERTY names, BlueprintImplementableEvent / native delegate pairs, RPC
`_Implementation`s, `Replicated` props vs `GetLifetimeReplicatedProps`, soft pointers in rows),
includes, shadowing (scripted: locals and parameters vs own and inherited members, nested scopes,
locals vs anonymous-namespace names of other files in a unity blob), unity names (anonymous
namespaces vs `using namespace`), brace narrowing, the HUD's window members and keys (K, P new; no
key used twice; `CloseAllWindows` closes every open window; the stash and vendor screens close the
others first), the row structs against the exporters (`Tools/check_table_types.py`: OK).

| # | Where | What | Status |
| --- | --- | --- | --- |
| RV1 | `CrossguardAICharacter.cpp` `ApplyBodyVisual` | Local `Body` hid `ACrossguardCharacter::Body` (the loot body; C4458 is an error) | **Applied**: renamed `BodyMeshComponent`. |
| RV2 | `CrossguardWeaponVisualsComponent.cpp` | `PI` (deprecated macro since 5.1, maybe gone in 5.8) | **Applied**: `UE_PI`, as the skill wheel. |
| RV3 | `CrossguardWeaponVisualsComponent.h` | `ClearHand(FHandVisual& Hand)`: parameter named like the member function `Hand` | **Applied**: renamed `Visual` (as the definition). |
| RV4 | `CrossguardPlayerController.cpp` `ApplyFieldOfView` | Local `Camera` named like `APlayerController::Camera` (exec) | **Applied**: renamed `PawnCamera` (in case C4458 counts member functions). |
| RV5 | `CrossguardPlayerState.cpp` `OnSetUniqueId` | Local `Guilds` named like the anonymous-namespace function `Guilds` in `CrossguardGuildComponent.cpp`: C4459 if a unity blob holds both (new files move the blob edges) | **Applied**: renamed `GuildSubsystem`. |
| RV6 | `CrossguardPlayerController.h` | `AddYawInput` / `AddPitchInput` / `SetPawn` overrides | Kept: virtual in every 5.x header known. If "does not override" (C3668): HM2's fix (scale `Axis` in `ACrossguardCharacter::Look`, `CrossguardCharacter.cpp:235`, by the controller's sensitivity) and move the field of view call to `OnPossess` / `BeginPlay`. |
| RV7 | `CrossguardPartyComponent.cpp` | AP3 (`GetRealTimeSeconds` float or double) | No risk: `double - float` is `double`, so `FMath::Max(0.0, ...)` compiles either way. |
| RV8 | Runtime: `Crossguard.Scenario.UI.PanelsBind`, `Visual.UI` | `ACrossguardPlayerController::BeginPlay` loads the settings slot and sets the pawn camera's FOV to the saved value (default 90) | If a picture's framing changed: the character camera's own FOV was not 90. Set the saved FOV (main menu Settings) or leave it. |
| RV9 | Runtime: `GameDefaultMap` | `-CreateMenu` sets it to L_MainMenu: a standalone `-game` run and a packaged game start on the menu | Intended. The editor startup map and PIE are unchanged. |

Not found: UFUNCTION parameter / UPROPERTY name clashes, unity clashes in the new namespaces
(`CrossguardStashWidgetLocal::ItemsIn` and `CrossguardVendorWidgetLocal::ItemsIn` are named and only
pulled in by their own file), missing `_Implementation`s, brace narrowing. Still unverified against
5.8 headers: SV1-SV8, AP1, AP4-AP8, HM1-HM10, AH1-AH11, ST1-ST8 above.

## Batch 4 (unbuilt)

### Nakama backend: REST client, save backend, player identity, menu login

New `Game/CrossguardNakamaClient` (plain C++), `Game/CrossguardNakamaSubsystem`,
`Game/CrossguardNakamaSaveBackend`, `Game/CrossguardLocalPlayer`, `Tests/CrossguardNakamaTests.cpp`
(namespace `CrossguardNakamaTestsLocal`); edits to `CrossguardGameMode` (`PreLoginAsync`,
`NakamaUserIdFor`), `CrossguardPlayerState` (`NakamaUserId`), `CrossguardGuildSubsystem::PlayerIdOf`,
`CrossguardSaveSubsystem` (backend selection, `FlushWrites`), `CrossguardMainMenuWidget` (Log in),
`ICrossguardSaveBackend::FlushWrites`, `Crossguard.Build.cs` (`HTTP`, `Json`), `DefaultEngine.ini`
(`LocalPlayerClassName`), `DefaultGame.ini`. The Go module and the RPCs were run here (unit tests and
`Tools/backend_smoke.py` against Nakama 3.41.0 in Docker); the Unreal side was reviewed by reading only.
How to test after it builds: [backend-setup.md](backend-setup.md).

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| N1 | `CrossguardGameMode.h/.cpp` | `virtual void PreLoginAsync(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, const FOnPreLoginCompleteDelegate& OnComplete) override`; `Self->AGameModeBase::PreLoginAsync(...)` inside the lambda | Signature from the 5.x docs (delegate takes `const FString& ErrorMessage`); the exact parameter list was not checked against 5.8's `GameModeBase.h`. | Copy the declaration from `GameModeBase.h`. If `PreLoginAsync` is missing: drop the override, verify in `PostLogin` instead (accept, then `GameSession->KickPlayer` if the RPC refuses within ~10 s), and keep `NakamaUserId` empty until verified (no save load before). |
| N2 | `CrossguardLocalPlayer.h` | `virtual FString GetGameLoginOptions() const override` | Documented in `Engine/LocalPlayer.h`; assumed the engine adds its result to the login URL the server's `PreLogin` / `InitNewPlayer` get as `Options`. | If the server never sees `NakamaToken` (log "Log in first"): add the option to the travel URLs instead (menu Join, `ClientTravel` URLs from hub / match servers). |
| N3 | `Config/DefaultEngine.ini` | `LocalPlayerClassName=/Script/Crossguard.CrossguardLocalPlayer` under `[/Script/Engine.Engine]` | Assumed key name (`UEngine::LocalPlayerClassName`, a `FSoftClassPath`). | Project Settings → Engine → General Settings → Default Classes → Local Player Class. |
| N4 | `CrossguardNakamaClient.cpp` | `FHttpModule::Get().CreateRequest()` (`TSharedRef<IHttpRequest, ESPMode::ThreadSafe>`), `OnProcessRequestComplete().BindLambda(... (FHttpRequestPtr, FHttpResponsePtr, bool))`, `SetTimeout(float)`, `GetContentAsString` | Stable since 4.26; 5.x callbacks still default to the game thread. | If `SetTimeout` is missing, drop it (global `[HTTP] HttpTotalTimeout`). If the callback signature changed, match `FHttpRequestCompleteDelegate`. |
| N5 | `CrossguardNakamaSaveBackend.cpp` `FlushWrites` | `FHttpModule::Get().GetHttpManager().Tick(0.05f)` (`#include "HttpManager.h"`) | Assumed `FHttpManager::Tick` is public and completes requests (game-thread callbacks) when called by hand during shutdown. | Use `GetHttpManager().Flush(EHttpFlushReason::Shutdown)` once per loop instead. If saves at shutdown still go to `NakamaUnsaved`, raise `ShutdownFlushSeconds` or flush before the world ends. |
| N6 | `CrossguardNakamaSaveBackend.h/.cpp` | `FTSTicker::FDelegateHandle`, `FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(...), Delay)`, `RemoveTicker`, `Handle.Reset()` / `IsValid()` | UE 5 ticker API (`Containers/Ticker.h`). | If `IsValid`/`Reset` are missing on the handle: keep a separate `bool bRetryScheduled`. |
| N7 | `CrossguardNakamaClient.cpp` | `FBase64::Encode(const FString&)`, `FBase64::Decode(const FString&, TArray<uint8>&)`, `FUTF8ToTCHAR(const ANSICHAR*, int32)`, `FString(int32, const TCHAR*)` | Assumed overloads; `Encode(FString)` encodes the string as ANSI/UTF-8 (keys are ASCII). | Convert by hand: `FTCHARToUTF8` to bytes, `FBase64::Encode(Bytes.GetData(), Len)`. |
| N8 | `CrossguardNakamaClient.cpp`, `...Subsystem.cpp`, `...SaveBackend.cpp` | `FJsonObject::TryGetStringField` / `TryGetNumberField(…, double&)` / `TryGetBoolField`, `HasTypedField<EJson::Object>`, `TJsonReaderFactory<>::Create(FString)` | 5.4+ take `FStringView` keys; `TEXT()` literals convert. | Usually compiles. If an overload is ambiguous, pass `FString(TEXT("key"))`. |
| N9 | `CrossguardNakamaSubsystem.cpp` | `FPlatformMisc::GetLoginId()`, `FPlatformMisc::GetEnvironmentVariable(TEXT(...))` | `GetLoginId` might be deprecated in 5.8 (C4996 only). | Use `FPlatformMisc::GetDeviceId()` or `FGuid` saved in `GameUserSettings` for the dev device id. |
| N10 | Runtime: main menu test `Crossguard.Scenario.Hub.MainMenuBinds` | Log in is a separate row, not a main button | The main-button list stays Play, Join, Settings, Quit. If the test counts all buttons: | Update the expectation to include the account row's button. |
| N11 | Runtime: dedicated server with `-NakamaHttpKey=` | `IsNakamaLoginRequired` refuses remote logins without a token unless `-AllowDevPlayerIds` | `Tools\run_local_cluster.ps1` starts servers with `-AllowDevPlayerIds` and no Nakama flags, so nothing changes there. With the flags and no Log in, clients get "Log in first". | Log in from the menu first (backend running), or start the servers with `-AllowDevPlayerIds`. |
| N12 | Runtime: PIE / tests | Saving is off in tests; the local backend stays the default; no test talks to Nakama | `Crossguard.Nakama.*` are pure (no network). | — |

### Nakama step 3: save leases, parties, matchmaking, tickets, server registry

New `Game/CrossguardNakamaMatchmaking` (`FCrossguardNakamaMatchmaking`, namespace
`CrossguardNakamaMatchmakingLocal`); rewritten `Game/CrossguardMatchmaking` (asynchronous interface:
`RequestMatch(..., FMatchCallback)`, `CancelMatch`, `IsSearching`, `FindHubServer(..., FHubCallback)`,
per-player `PlayerTickets`; `UCrossguardMatchmakingSubsystem::Initialize`); edits to
`CrossguardNakamaClient` (`CallClientRpc`), `CrossguardNakamaSubsystem` (backend switch, server id,
registry, tickets, `FindMyHub`, `Deinitialize`), `CrossguardNakamaSaveBackend` (leases),
`ICrossguardSaveBackend::ReleaseCharacter`, `CrossguardSaveSubsystem` (dependency, release on leave),
`CrossguardPartySubsystem` (backend mode, async launch, `PlanRaidTravelWith`), `CrossguardPartyComponent`
(new results, `PlayerArrived`), `CrossguardPlayerState::OnSetUniqueId`, `CrossguardGameMode`
(tickets at `PreLoginAsync` / `InitNewPlayer`, capacities, registration in `StartPlay`, async team
return `FinishTeamHubReturn`, `GetHubDestination(Player, BackendHub)`), `CrossguardRaidPortal` (texts),
`CrossguardMainMenuWidget::Play`, tests (`CrossguardNakamaTests.cpp`, `CrossguardScenarioPartyTests.cpp`),
rules `FPartyRegistry` mirror methods (built and tested here). The Go module and every RPC ran here
(`go test`, `Tools/backend_smoke.py` 72 checks against Nakama 3.41.0); the Unreal side is read-only
reviewed.

| # | Where | Call | Why it may fail | Suggested fix |
| --- | --- | --- | --- | --- |
| N13 | `CrossguardSaveSubsystem.cpp`, `CrossguardPartySubsystem.cpp`, `CrossguardMatchmaking.cpp` `Initialize` | `Collection.InitializeDependency<UCrossguardNakamaSubsystem>()` | Assumed the templated overload (UE 5.x `FSubsystemCollectionBase`). | `Collection.InitializeDependency(UCrossguardNakamaSubsystem::StaticClass());` |
| N14 | `CrossguardNakamaSubsystem.cpp`, `CrossguardNakamaSaveBackend.cpp`, `CrossguardPartySubsystem.cpp` | `FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UClass::Tick), Seconds)` with `bool Tick(float)` | `FTickerDelegate` is `TDelegate<bool(float)>`; `CreateUObject` should bind a UObject member. | `CreateWeakLambda(this, [this](float Dt) { return Tick(Dt); })`. |
| N15 | `CrossguardGameMode.cpp` `RegisterWithBackend` | `GetWorld()->URL.Port` as the listen port | Assumed `UWorld::URL` holds the `-port=` of a dedicated server at `StartPlay`. | Read `FParse::Value(FCommandLine::Get(), TEXT("Port="), Port)` (default 7777), or the net driver's local address. Wrong ports show in the log "registered as ... at 127.0.0.1:<port>"; `-PublicAddress=` overrides. |
| N16 | `CrossguardGameMode.cpp` `InitGame` | `IsRunningDedicatedServer()`, `GameSession->MaxPlayers = ...` (`#include "GameFramework/GameSession.h"`) | `MaxPlayers` is a public `int32` on `AGameSession` (globalconfig). | If private in 5.8: `[/Script/Engine.GameSession] MaxPlayers=64` in `DefaultGame.ini` and check the raid cap in `PreLogin`. |
| N17 | `CrossguardPartySubsystem.cpp` `ResultFromName` | `StaticEnum<ECrossguardPartyResult>()->GetValueByNameString(Name)` with a short name ("NotLeader"), then "ECrossguardPartyResult::NotLeader" | Unsure which form an `enum class` UENUM matches. | Both are tried. Test: `Crossguard.Nakama.PartyState`. |
| N18 | `CrossguardNakamaMatchmaking.cpp`, `CrossguardPartySubsystem.cpp` `ApplyBackendState` | `FJsonObject::TryGetObjectField(key, const TSharedPtr<FJsonObject>*&)`, `TryGetArrayField(key, const TArray<TSharedPtr<FJsonValue>>*&)`, `FJsonObject::Values` (a `TMap<FString, TSharedPtr<FJsonValue>>`), `FJsonValue::TryGetObject(const TSharedPtr<FJsonObject>*&)`, `TryGetString(FString&)` | 5.x signatures (keys as `FStringView`). | Usually compiles; if not, `GetObjectField` / `GetArrayField` after `HasTypedField<EJson::Object>`. |
| N19 | `CrossguardNakamaMatchmaking.h/.cpp` | `class ... : public ICrossguardMatchmaking, public TSharedFromThis<FCrossguardNakamaMatchmaking>`; `TWeakPtr<...> WeakThis = AsShared();`; `MakeShared<FCrossguardNakamaMatchmaking>(Nakama)` into `TSharedPtr<ICrossguardMatchmaking>` | Standard pattern; ESPMode defaults match. | — |
| N20 | `CrossguardNakamaTests.cpp` | `NewObject<UCrossguardPartySubsystem>(GetTransientPackage())`, `NewObject<UCrossguardNakamaSubsystem>(...)` (subsystems outside a game instance) | Assumed nothing asserts on a subsystem without a game instance (`GetGameInstance()` is null; `Initialize` is not called). | If it asserts: test the mirror in a scenario (the PIE game instance's subsystem) instead. |
| N21 | `CrossguardPartySubsystem.cpp` `CancelSearch` | `for (const FString& Key : {PartyKey, PlayerId})` | Initializer list of two `FString`. | Two `if` blocks. |
| N22 | `CrossguardGameMode.cpp` `PreLoginAsync` | Nested lambdas: the outer has a parameter `UserId`, the inner captures `UserId` by copy and has its own parameter `TicketError` | MSVC C4456/C4457 (shadowing, an error in Unreal) should not fire for a capture. | Rename the capture: `[..., VerifiedUserId = UserId]`. |
| N23 | Runtime: dedicated server with `-SaveBackend=Nakama` | Registration at `StartPlay`; heartbeat every 10 s | Log "Nakama: registered as hub server ..." (or "registration failed ... retrying"). | Check `-NakamaUrl=`, the key, Docker. |
| N24 | Runtime: hub → raid | A party member whose `ClientTravel` fails stays marked as "leaving for a raid": their later logout does not leave the party | Minor (they leave after 120 s on no server anyway). | — |
| N25 | Runtime: shutdown | Lease release and deregister tick the HTTP module by hand (like N5) | Same as N5. | Same as N5. |
| N26 | Runtime: `Crossguard.Scenario.Hub.PartyRaidLaunchPlan` | New step with a deferred matchmaking; `StartRaid` now ends with `FinishRaidLaunch` (the same travel code) | The earlier steps' expectations are unchanged (the dev fake answers at once). | If the deferred step fails on "Cancelled once": the party change did not reach `CancelSearch` (local `Leave` calls it). |
