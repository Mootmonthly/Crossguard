#pragma once

#include "CoreMinimal.h"
#include "CrossguardRules/Teams.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "Items/CrossguardLoot.h"

#include "CrossguardGameMode.generated.h"

class ACrossguardCharacter;
class UCrossguardSkillCatalog;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossguardPlayerExtracted, ACrossguardCharacter*, Character);

// Match game mode. Assigns teams on login from the "Party" URL option: players joining with the
// same ?Party=<key> share a team (up to 8); players without one each get their own team.
// Matchmaking must put the party key in the travel URL. In the hub, in-game parties
// (UCrossguardPartySubsystem, docs/parties.md) move their members onto one team with SetPlayerParty
// and launch the raid with RaidMapName / RaidServerAddress.
UCLASS()
class CROSSGUARD_API ACrossguardGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACrossguardGameMode();

	// A "?Hub" option on the server's map URL turns bIsRaid off (a dev hub on any map).
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	// Refuses a login whose party's team is already full.
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;
	virtual void Logout(AController* Exiting) override;
	// Seamless travel skips InitNewPlayer: re-register the player with the party key their player
	// state carried over.
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;
	// Starts the raid for each player when this map is a raid.
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	// Server. Called by the attribute set when damage brings a character to 0 Health. Awards kill
	// XP to the killer's player: PlayerKill for a player-controlled victim, otherwise the victim's
	// KillXpSource. No XP for killing yourself or an ally.
	virtual void HandleKill(AActor* Victim, AActor* Killer);

	// Server. Called by an extraction zone: ends the raid for the character's player as a success.
	// Dead characters (State.Dead) cannot extract.
	virtual void HandleExtraction(ACrossguardCharacter* Character);

	// Server. The character's revive window ran out: its player's raid ends as a failure. The player
	// spectates a teammate still in the raid, or goes back to the menu if none is left (or solo).
	virtual void HandlePermanentDeath(ACrossguardCharacter* Character);

	// After a successful extraction. Hook the trip to the hub here once it exists.
	UPROPERTY(BlueprintAssignable, Category = "Crossguard")
	FCrossguardPlayerExtracted OnPlayerExtracted;

	virtual void StartPlay() override;

	// Seconds into the raid before extract portals can be summoned. Lower it to test.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard", meta = (ClampMin = "0"))
	float PortalUnlockSeconds = 600.0f;

	// Seconds since the raid started (0 outside raid maps).
	UFUNCTION(BlueprintPure, Category = "Crossguard")
	float GetRaidElapsedSeconds() const;

	// Raid maps hold XP until extraction and block between-raid actions (learning Skill Gems,
	// changing attributes). Turn off for a hub / test map where those should work.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard")
	bool bIsRaid = true;

	// Loot tables and the item data they draw from. Set every table here (the same assets the
	// character uses) for chests and monster drops to work.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Loot")
	FCrossguardLootSources Loot;

	// Imported from Data/Mobs/MobTable.json. Row struct: CrossguardMobRow. Monsters with a MobId
	// take their stats from it.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Mobs")
	TObjectPtr<UDataTable> MobTable;

	// The skill catalog (DA_SkillCatalog) mobs' skills are looked up in. Unset: native skills only
	// (data passives such as Sword Mastery do nothing).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Mobs")
	TObjectPtr<UCrossguardSkillCatalog> MobSkillCatalog;

	// Server. Rolls a loot table with a fresh random seed. Empty if the table or data is missing.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crossguard|Loot")
	TArray<FCrossguardInventoryItem> RollLoot(FName TableId);

	static const TCHAR* PartyOption;
	// Placeholder until a hub map exists: open any map with ?Hub to play it as the hub.
	static const TCHAR* HubOption;

	// Server. Moves the player onto the team of party key Party (empty: a team of their own), as if
	// they had logged in with ?Party=<Party>. No-op if that is their key already.
	void SetPlayerParty(AController* Controller, const FString& Party);

	// --- Raid launch from the hub (docs/parties.md). Placeholders until a matchmaker exists. ---

	// The raid map a party that is the whole server travels to (ServerTravel), e.g.
	// "/Game/Maps/L_Raid". The -RaidMap=<path> command line option overrides it. Empty: none.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Raid Launch")
	FString RaidMapName;

	// A separate raid server ("203.0.113.5:7777") the party's clients travel to while the rest of the
	// hub stays. The -RaidServer=<address> command line option overrides it. Empty: none.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crossguard|Raid Launch")
	FString RaidServerAddress;

	FString GetRaidMapName() const;
	FString GetRaidServerAddress() const;

private:
	static std::string PlayerKey(const FUniqueNetIdRepl& UniqueId, const AController* Controller);
	static std::string PartyKey(const FString& Options);
	// Registers the player and sets their team. Returns an error message, or empty on success.
	FString JoinTeam(AController* Controller, const std::string& Key, const FString& Party);

	// Re-targets every spectator whose teammate left the raid; sends them to the menu when none is
	// left.
	void UpdateSpectators();
	// A teammate of Player still in the raid; null if none.
	APawn* FindSpectateTarget(const APlayerController* Player) const;

	Crossguard::FTeamRegistry Teams;
	TArray<TWeakObjectPtr<APlayerController>> Spectators;
	float RaidStartSeconds = 0.0f;
	// Controller -> registry key, so Logout can find the player.
	TMap<TWeakObjectPtr<const AController>, FString> PlayerKeys;
};
