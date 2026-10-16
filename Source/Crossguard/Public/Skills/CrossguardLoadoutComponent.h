#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"

#include "CrossguardLoadoutComponent.generated.h"

class UCrossguardAbilitySystemComponent;
class UCrossguardEquipmentComponent;
class UCrossguardSkillCatalog;
class UCrossguardSkillGrant;
struct FCrossguardSkillRequirements;

// Mirrors Crossguard::ELoadoutError.
UENUM(BlueprintType)
enum class ECrossguardLoadoutError : uint8
{
	None,
	UnknownSkill,
	// Not in the character's Skill Library.
	NotLearned,
	DuplicateSkill,
	// SkillId is already provided by OtherSkillId (bundle component or granted-while-active skill).
	SupersededSkill,
	OverCapacity,
	// No skill catalog or ability system to validate against.
	NotReady,
};

USTRUCT(BlueprintType)
struct CROSSGUARD_API FCrossguardLoadoutResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Loadout")
	ECrossguardLoadoutError Error = ECrossguardLoadoutError::None;

	UPROPERTY(BlueprintReadOnly, Category = "Loadout")
	FName SkillId;

	UPROPERTY(BlueprintReadOnly, Category = "Loadout")
	FName OtherSkillId;

	UPROPERTY(BlueprintReadOnly, Category = "Loadout")
	int32 TotalCost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Loadout")
	int32 Capacity = 0;

	bool IsValid() const { return Error == ECrossguardLoadoutError::None; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardLoadoutChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardLoadoutRejected, const FCrossguardLoadoutResult&, Result);

// The character's equipped skills. The server validates a requested loadout against the skill
// catalog and the character's Skill Capacity, then grants one ability per effective skill: equipped
// skills plus their bundle components, each once. A Passive skill with no ability class in the
// catalog is played from its data by UCrossguardDataPassiveAbility.
//
// A bundle's requirement also switches off the skills it includes ([The Mithril Knight] without Plate
// in every slot: no Multiscale, Immutable or Antimagic Garb either; [Spear of Liscor] without a
// Spear: no Antimagic Garb). The granted abilities ask IsSkillSwitchedOn and recheck on equipment
// changes.
//
// Players can only equip skills in their Skill Library (ACrossguardPlayerState).
// Not yet checked: whether equipping is allowed at the current moment (e.g. only outside a raid).
UCLASS(ClassGroup = (Crossguard), meta = (BlueprintSpawnableComponent))
class CROSSGUARD_API UCrossguardLoadoutComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrossguardLoadoutComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	TObjectPtr<UCrossguardSkillCatalog> SkillCatalog;

	// Replaces the loadout. Runs on the server; clients forward the request. On failure the
	// owning client receives OnLoadoutRejected and the current loadout stays.
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void RequestLoadout(const TArray<FName>& Skills);

	// Checks a loadout without applying it. Usable on clients for UI feedback.
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	FCrossguardLoadoutResult ValidateLoadout(const TArray<FName>& Skills) const;

	UFUNCTION(BlueprintPure, Category = "Loadout")
	const TArray<FName>& GetEquippedSkills() const { return EquippedSkills; }

	// Whether the loadout has this granted skill on: always when it is equipped itself; a skill that
	// comes only with bundles needs one of them (every bundle on the way) to have its requirement met.
	// True for skills the loadout did not grant and for AI characters (their skills have no
	// requirements). Server and clients (built from the replicated loadout).
	bool IsSkillSwitchedOn(FName SkillId) const;

	// A skill's own weapon and armor requirements (Skills.json) against the equipment. Without
	// equipment only a skill with no requirements passes.
	static bool AreSkillRequirementsMet(const FCrossguardSkillRequirements& Requirements, const UCrossguardEquipmentComponent* Equipment);

	// Skill Capacity at the owner's current level.
	UFUNCTION(BlueprintPure, Category = "Loadout")
	int32 GetCapacity() const;

	// Skills the player can cast (Active, Spell, Art) among those granted, for the skill wheel.
	// Includes skills granted while another is active (Dragonification's Flight). Owning client and
	// server.
	UFUNCTION(BlueprintPure, Category = "Loadout|Casting")
	TArray<FName> GetActivatableSkills() const;

	// The skill wheel (hold Q) sets this; E casts it. Local to the owning client.
	UFUNCTION(BlueprintCallable, Category = "Loadout|Casting")
	void SetSelectedSkill(FName SkillId) { SelectedSkill = SkillId; }

	UFUNCTION(BlueprintPure, Category = "Loadout|Casting")
	FName GetSelectedSkill() const { return SelectedSkill; }

	// E: casts the selected skill (predicted on the owning client). False if none is selected or it
	// cannot be cast now (cooldown, requirements, cost).
	UFUNCTION(BlueprintCallable, Category = "Loadout|Casting")
	bool TryActivateSelectedSkill();

	// Called by the character when Q is pressed / released; the skill wheel UI listens.
	void SetSkillWheelOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Loadout|Casting")
	bool IsSkillWheelOpen() const { return bSkillWheelOpen; }

	UPROPERTY(BlueprintAssignable, Category = "Loadout|Casting")
	FCrossguardLoadoutChanged OnSkillWheelOpened;

	UPROPERTY(BlueprintAssignable, Category = "Loadout|Casting")
	FCrossguardLoadoutChanged OnSkillWheelClosed;

	UPROPERTY(BlueprintAssignable, Category = "Loadout")
	FCrossguardLoadoutChanged OnLoadoutChanged;

	UPROPERTY(BlueprintAssignable, Category = "Loadout")
	FCrossguardLoadoutRejected OnLoadoutRejected;

protected:
	UFUNCTION(Server, Reliable)
	void ServerRequestLoadout(const TArray<FName>& Skills);

	UFUNCTION(Client, Reliable)
	void ClientLoadoutRejected(const FCrossguardLoadoutResult& Result);

	UFUNCTION()
	void OnRep_EquippedSkills();

private:
	void ApplyLoadout(const TArray<FName>& Skills);
	void ClearGrantedAbilities(UCrossguardAbilitySystemComponent& ASC);
	// Rebuilds SkillSources for this loadout.
	void RebuildSkillSources(const TArray<FName>& Skills);
	UCrossguardAbilitySystemComponent* GetAbilitySystem() const;

	UPROPERTY(ReplicatedUsing = OnRep_EquippedSkills)
	TArray<FName> EquippedSkills;

	// Server only.
	TArray<FGameplayAbilitySpecHandle> GrantedAbilities;

	// Each granted skill: the bundle chains it comes through (Crossguard::ResolveLoadoutSources); an
	// empty chain when equipped itself.
	TMap<FName, TArray<TArray<FName>>> SkillSources;

	FName SelectedSkill;
	bool bSkillWheelOpen = false;

	// Server only: tells each data-driven passive which skill it plays.
	UPROPERTY()
	TArray<TObjectPtr<UCrossguardSkillGrant>> SkillGrants;
};
