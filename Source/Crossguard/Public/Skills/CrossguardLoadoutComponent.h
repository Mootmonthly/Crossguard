#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"

#include "CrossguardLoadoutComponent.generated.h"

class UCrossguardAbilitySystemComponent;
class UCrossguardSkillCatalog;

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
// skills plus their bundle components, each once.
//
// Not yet checked: whether the player owns the Skill Gems, and whether equipping is allowed at
// the current moment (e.g. only outside a match).
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

	// Skill Capacity at the owner's current level.
	UFUNCTION(BlueprintPure, Category = "Loadout")
	int32 GetCapacity() const;

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
	UCrossguardAbilitySystemComponent* GetAbilitySystem() const;

	UPROPERTY(ReplicatedUsing = OnRep_EquippedSkills)
	TArray<FName> EquippedSkills;

	// Server only.
	TArray<FGameplayAbilitySpecHandle> GrantedAbilities;
};
