#pragma once

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Experience.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "Items/CrossguardItemTypes.h"

#include "CrossguardPlayerState.generated.h"

class UCrossguardSkillCatalog;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardTeamChanged, int32, NewTeamId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCrossguardProgressionChanged);

// Mirrors Crossguard::EXpSource.
UENUM(BlueprintType)
enum class ECrossguardXpSource : uint8
{
	BasicMonster,
	BossMonster,
	PlayerKill,
	// Amount set per quest (10-100).
	Quest,
	GatherNode,
	// Scaled by the item's rarity.
	Craft,
	Enchant,
	Extract,
};

UENUM(BlueprintType)
enum class ECrossguardProgressionError : uint8
{
	None,
	// Only allowed between raids.
	InRaid,
	// No respec token: respecs are bought with real money.
	NoRespecToken,
	InvalidAllocation,
	UnknownSkill,
	AlreadyLearned,
	// No skill catalog to check against.
	NotReady,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardProgressionRejected, ECrossguardProgressionError, Error);

// Per-player state that outlives the pawn: team, XP and level, attribute allocation, Skill Library
// and respec tokens.
//
// Not persisted yet: progression lasts for the session (and across seamless travel) until a save
// or backend exists.
UCLASS()
class CROSSGUARD_API ACrossguardPlayerState : public APlayerState, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -1 until assigned.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Team")
	int32 GetTeamId() const { return TeamId; }

	// Server only.
	void SetTeamId(int32 NewTeamId);

	// The ?Party= key the player joined with. Server only; kept across seamless travel so the next
	// map's game mode can re-register the team.
	FString PartyKey;

	// Seamless travel: carry the team, party key and progression to the new player state.
	virtual void CopyProperties(APlayerState* PlayerState) override;

	// IGenericTeamAgentInterface. Perception asks the pawn, which forwards here. Team ids above 254
	// report NoTeam.
	virtual FGenericTeamId GetGenericTeamId() const override;

	UPROPERTY(BlueprintAssignable, Category = "Crossguard|Team")
	FCrossguardTeamChanged OnTeamChanged;

	// --- Progression ---------------------------------------------------------------------------

	// Level from banked XP, capped at the season's max level.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	int32 GetCharacterLevel() const;

	// Banked XP. Owner only.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	int32 GetTotalXp() const { return TotalXp; }

	// XP earned this raid, banked on extraction and lost on a failed extraction. Owner only.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	int32 GetRaidXp() const { return RaidXp; }

	// For an XP bar: banked XP past the current level, and the XP the next level needs.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	void GetLevelProgress(int32& XpIntoLevel, int32& XpForNextLevel) const;

	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	bool IsInRaid() const { return bInRaid; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	const FCrossguardAttributeAllocation& GetAttributeAllocation() const { return AttributeAllocation; }

	// Respecs available. Each real-money respec purchase grants one. Owner only.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	int32 GetRespecTokens() const { return RespecTokens; }

	// Skills learned from Skill Gems; the loadout can only equip these. Owner only.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	const TArray<FName>& GetSkillLibrary() const { return SkillLibrary; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	bool HasLearnedSkill(FName SkillId) const;

	// Server. Awards XP for a source; QuestXp is read for Quest only, Rarity for Craft and Enchant
	// only. In a raid the XP is held until the raid ends; between raids it is banked at once.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Progression")
	void AwardXp(ECrossguardXpSource Source, int32 QuestXp = 0, ECrossguardRarity Rarity = ECrossguardRarity::Common);

	// Server. Marks the start of a raid.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Progression")
	void BeginRaid();

	// Server. Ends the raid: extracting banks the raid XP (plus the extraction reward), failing to
	// extract loses it. Returns the XP banked.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Progression")
	int32 EndRaid(bool bExtracted);

	// Server. Call only after the store purchase is verified server-side (never on a client's word).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Progression")
	void GrantRespecTokens(int32 Count);

	// Server. Learns a Skill Gem's skill. Only between raids; the caller consumes the gem on None.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Progression")
	ECrossguardProgressionError LearnSkill(FName SkillId, const UCrossguardSkillCatalog* Catalog);

	// Server. Testing only: learns a skill without a gem, in or out of a raid.
	void LearnSkillForTesting(FName SkillId);

	// Sets the attribute allocation. Only between raids. Spending unspent points is free; lowering
	// any attribute is a respec and uses one respec token. Clients forward the request; failures
	// reach the owning client through OnProgressionRejected.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Progression")
	void RequestAttributeAllocation(const FCrossguardAttributeAllocation& Allocation);

	// Fires on the server and the owning client when XP, level, allocation, library or respec
	// tokens change.
	UPROPERTY(BlueprintAssignable, Category = "Crossguard|Progression")
	FCrossguardProgressionChanged OnProgressionChanged;

	UPROPERTY(BlueprintAssignable, Category = "Crossguard|Progression")
	FCrossguardProgressionRejected OnProgressionRejected;

	const Crossguard::FProgressionConfig& GetProgressionConfig() const { return ProgressionConfig; }

protected:
	UFUNCTION()
	void OnRep_TeamId();

	UFUNCTION()
	void OnRep_Progression();

	UFUNCTION(Server, Reliable)
	void ServerRequestAttributeAllocation(const FCrossguardAttributeAllocation& Allocation);

	UFUNCTION(Client, Reliable)
	void ClientProgressionRejected(ECrossguardProgressionError Error);

private:
	ECrossguardProgressionError ApplyAttributeAllocation(const FCrossguardAttributeAllocation& Allocation);
	void AddXp(int32 Amount);
	// Server. Broadcasts and pushes level and allocation to the pawn.
	void ProgressionChanged();

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = -1;

	UPROPERTY(ReplicatedUsing = OnRep_Progression)
	int32 TotalXp = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progression)
	int32 RaidXp = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progression)
	bool bInRaid = false;

	UPROPERTY(ReplicatedUsing = OnRep_Progression)
	FCrossguardAttributeAllocation AttributeAllocation;

	UPROPERTY(ReplicatedUsing = OnRep_Progression)
	int32 RespecTokens = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progression)
	TArray<FName> SkillLibrary;

	Crossguard::FProgressionConfig ProgressionConfig;
	Crossguard::FXpConfig XpConfig;
};
