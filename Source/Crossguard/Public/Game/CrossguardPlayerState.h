#pragma once

#include "AbilitySystem/CrossguardAbilitySystemComponent.h"
#include "CoreMinimal.h"
#include "CrossguardRules/Experience.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "Items/CrossguardItemTypes.h"

#include "CrossguardPlayerState.generated.h"

class UCrossguardSkillCatalog;
class UCrossguardGuildComponent;
class UCrossguardPartyComponent;
class UCrossguardStashComponent;
class UCrossguardVendorComponent;

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
	// Paying a respec with a token, and none is left.
	NoRespecToken,
	InvalidAllocation,
	UnknownSkill,
	AlreadyLearned,
	// No skill catalog to check against.
	NotReady,
	// Paying a respec with gold, and the banked gold is under the price (1000).
	NotEnoughGold,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardProgressionRejected, ECrossguardProgressionError, Error);

// Per-player state that outlives the pawn: team, XP and level, attribute allocation, Skill Library,
// respec tokens and the stash.
//
// Saved by UCrossguardSaveSubsystem (docs/persistence.md): loaded when the unique id is set, saved
// when a raid starts and ends and after changes between raids.
UCLASS()
class CROSSGUARD_API ACrossguardPlayerState : public APlayerState, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ACrossguardPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -1 until assigned.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Team")
	int32 GetTeamId() const { return TeamId; }

	// Server only.
	void SetTeamId(int32 NewTeamId);

	// The ?Party= key the player joined with. Server only; kept across seamless travel so the next
	// map's game mode can re-register the team.
	FString PartyKey;

	// Development only (never set in Shipping): a stable stand-in player id for a player without an
	// online id, set by the server at login (ACrossguardGameMode::InitNewPlayer; docs/persistence.md,
	// "Player id"). Empty otherwise. UCrossguardGuildSubsystem::PlayerIdOf prefers it to "session-<n>".
	// Replicated so the owner's UI computes the same id.
	UPROPERTY(Replicated)
	FString DevPlayerId;

	// The player's Nakama user id, verified by the server at login (?NakamaToken=, ACrossguardGameMode::
	// PreLoginAsync; docs/anti-cheat.md "Player identity"). Empty without a Nakama login. The player id
	// (UCrossguardGuildSubsystem::PlayerIdOf) when set, ahead of Steam / development / session ids.
	// Replicated like DevPlayerId (a Nakama user id is not secret).
	UPROPERTY(Replicated)
	FString NakamaUserId;

	// Seamless travel: carry the team, party key and progression to the new player state.
	virtual void CopyProperties(APlayerState* PlayerState) override;
	// The online id arrives after BeginPlay for joining players: guilds know players by it.
	virtual void OnSetUniqueId() override;

	// IGenericTeamAgentInterface. Perception asks the pawn, which forwards here. Team ids above 254
	// report NoTeam.
	virtual FGenericTeamId GetGenericTeamId() const override;

	UPROPERTY(BlueprintAssignable, Category = "Crossguard|Team")
	FCrossguardTeamChanged OnTeamChanged;

	// True while the player is away mid-raid and their character waits for a reconnect
	// (ACrossguardGameMode::HoldForReconnect). Public, like the team: teammates' party panel and HUD
	// team list show "(disconnected)". Display only.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Team")
	bool IsDisconnected() const { return bDisconnected; }

	// Server only (the game mode's hold, reconnect and release).
	void SetDisconnected(bool bInDisconnected);

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
	// Server only: this player's raid on this map is over (extracted or dead for the raid).
	bool HasFinishedRaid() const { return bRaidFinished; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	const FCrossguardAttributeAllocation& GetAttributeAllocation() const { return AttributeAllocation; }

	// Respecs available. Each real-money respec purchase grants one. Owner only. A respec can also be
	// paid with banked gold instead (GetRespecGoldPrice).
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	int32 GetRespecTokens() const { return RespecTokens; }

	// Banked gold: safe, spent on stash tabs and the like. Gold coins carried in a raid are items
	// until the player extracts with them. Owner only.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	int32 GetGold() const { return Gold; }

	// Server. Adds banked gold (vendor sales, extraction).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Progression")
	void AddGold(int32 Amount);

	// Whether Amount more banked gold fits (it is an int32).
	bool CanReceiveGold(int64 Amount) const { return Amount >= 0 && static_cast<int64>(Gold) + Amount <= MAX_int32; }

	// Server. Takes Amount from the banked gold. False, changing nothing, if there is not enough.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Progression")
	bool SpendGold(int32 Amount);

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

	// One extract portal per player per raid. Server only.
	bool HasSummonedPortal() const { return bPortalSummoned; }
	void MarkPortalSummoned() { bPortalSummoned = true; }

	// Server. Ends the raid: extracting banks the raid XP (plus the extraction reward) and moves
	// the gold coins the character carries into the gold balance; failing to extract loses the raid
	// XP. Returns the XP banked.
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

	// Server. Loading a save (UCrossguardSaveSubsystem, which validated it): replaces the banked
	// progression. Raid state and raid XP are untouched.
	void RestoreSavedProgression(int32 InTotalXp, const FCrossguardAttributeAllocation& InAllocation, int32 InRespecTokens, int32 InGold,
		const TArray<FName>& InSkillLibrary);

	// Sets the attribute allocation. Only between raids. Spending unspent points is free; lowering
	// any attribute is a respec, paid with one respec token or, with bPayWithGold, with
	// GetRespecGoldPrice() banked gold (the player chooses). Clients forward the request; the server
	// checks and pays; failures reach the owning client through OnProgressionRejected.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Progression")
	void RequestAttributeAllocation(const FCrossguardAttributeAllocation& Allocation, bool bPayWithGold = false);

	// Banked gold a respec costs when not paid with a token (ruled: 1000).
	UFUNCTION(BlueprintPure, Category = "Crossguard|Progression")
	int32 GetRespecGoldPrice() const;

	// Fires on the server and the owning client when XP, level, allocation, library or respec
	// tokens change.
	UPROPERTY(BlueprintAssignable, Category = "Crossguard|Progression")
	FCrossguardProgressionChanged OnProgressionChanged;

	UPROPERTY(BlueprintAssignable, Category = "Crossguard|Progression")
	FCrossguardProgressionRejected OnProgressionRejected;

	const Crossguard::FProgressionConfig& GetProgressionConfig() const { return ProgressionConfig; }

	// The character's stash (between-raid storage).
	UFUNCTION(BlueprintPure, Category = "Crossguard|Stash")
	UCrossguardStashComponent* GetStash() const { return Stash; }

	// Selling to NPC vendors.
	UFUNCTION(BlueprintPure, Category = "Crossguard|Vendor")
	UCrossguardVendorComponent* GetVendor() const { return Vendor; }

	UFUNCTION(BlueprintPure, Category = "Crossguard|Guild")
	UCrossguardGuildComponent* GetGuild() const { return Guild; }

	// In-game party (docs/parties.md).
	UFUNCTION(BlueprintPure, Category = "Crossguard|Party")
	UCrossguardPartyComponent* GetParty() const { return Party; }

protected:
	UFUNCTION()
	void OnRep_TeamId();

	UFUNCTION()
	void OnRep_Progression();

	UFUNCTION(Server, Reliable)
	void ServerRequestAttributeAllocation(const FCrossguardAttributeAllocation& Allocation, bool bPayWithGold);

	UFUNCTION(Client, Reliable)
	void ClientProgressionRejected(ECrossguardProgressionError Error);

private:
	ECrossguardProgressionError ApplyAttributeAllocation(const FCrossguardAttributeAllocation& Allocation, bool bPayWithGold);
	void AddXp(int32 Amount);
	// Server. Broadcasts and pushes level and allocation to the pawn.
	void ProgressionChanged();

	UPROPERTY(VisibleAnywhere, Category = "Crossguard|Stash")
	TObjectPtr<UCrossguardStashComponent> Stash;

	UPROPERTY(VisibleAnywhere, Category = "Crossguard|Vendor")
	TObjectPtr<UCrossguardVendorComponent> Vendor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Guild", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCrossguardGuildComponent> Guild;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crossguard|Party", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCrossguardPartyComponent> Party;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = -1;

	UPROPERTY(Replicated)
	bool bDisconnected = false;

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
	int32 Gold = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progression)
	TArray<FName> SkillLibrary;

	// Server only; reset by BeginRaid.
	bool bPortalSummoned = false;
	bool bRaidFinished = false;

	Crossguard::FProgressionConfig ProgressionConfig;
	Crossguard::FXpConfig XpConfig;
};
