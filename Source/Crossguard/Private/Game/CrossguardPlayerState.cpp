#include "Game/CrossguardPlayerState.h"

#include "Character/CrossguardCharacter.h"
#include "CrossguardRules/Loadout.h"
#include "CrossguardRules/Progression.h"
#include "Net/UnrealNetwork.h"
#include "Skills/CrossguardSkillCatalog.h"

namespace
{
	Crossguard::EXpSource ToRulesXpSource(ECrossguardXpSource Source)
	{
		return static_cast<Crossguard::EXpSource>(static_cast<uint8>(Source));
	}

	Crossguard::FAttributes ToRulesAttributes(const FCrossguardAttributeAllocation& Allocation)
	{
		return {Allocation.Strength, Allocation.Agility, Allocation.Intelligence};
	}

	// Skill ids are lowercase (FName compares case-insensitively; the catalog does not).
	FName NormalizeSkillId(FName SkillId)
	{
		return FName(*SkillId.ToString().ToLower());
	}
}

static_assert(static_cast<int>(ECrossguardXpSource::BasicMonster) == static_cast<int>(Crossguard::EXpSource::BasicMonster), "XP source enums out of sync");
static_assert(static_cast<int>(ECrossguardXpSource::BossMonster) == static_cast<int>(Crossguard::EXpSource::BossMonster), "XP source enums out of sync");
static_assert(static_cast<int>(ECrossguardXpSource::PlayerKill) == static_cast<int>(Crossguard::EXpSource::PlayerKill), "XP source enums out of sync");
static_assert(static_cast<int>(ECrossguardXpSource::Quest) == static_cast<int>(Crossguard::EXpSource::Quest), "XP source enums out of sync");
static_assert(static_cast<int>(ECrossguardXpSource::GatherNode) == static_cast<int>(Crossguard::EXpSource::GatherNode), "XP source enums out of sync");
static_assert(static_cast<int>(ECrossguardXpSource::Craft) == static_cast<int>(Crossguard::EXpSource::Craft), "XP source enums out of sync");
static_assert(static_cast<int>(ECrossguardXpSource::Enchant) == static_cast<int>(Crossguard::EXpSource::Enchant), "XP source enums out of sync");
static_assert(static_cast<int>(ECrossguardXpSource::Extract) == static_cast<int>(Crossguard::EXpSource::Extract), "XP source enums out of sync");

void ACrossguardPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everyone knows everyone's team (nameplates, party skills).
	DOREPLIFETIME(ACrossguardPlayerState, TeamId);

	// Level is public through the character; the rest is the owner's business.
	DOREPLIFETIME_CONDITION(ACrossguardPlayerState, TotalXp, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACrossguardPlayerState, RaidXp, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACrossguardPlayerState, bInRaid, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACrossguardPlayerState, AttributeAllocation, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACrossguardPlayerState, RespecTokens, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACrossguardPlayerState, SkillLibrary, COND_OwnerOnly);
}

void ACrossguardPlayerState::SetTeamId(int32 NewTeamId)
{
	if (HasAuthority() && TeamId != NewTeamId)
	{
		TeamId = NewTeamId;
		OnTeamChanged.Broadcast(TeamId);
	}
}

void ACrossguardPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (ACrossguardPlayerState* Next = Cast<ACrossguardPlayerState>(PlayerState))
	{
		Next->PartyKey = PartyKey;
		Next->SetTeamId(TeamId);
		Next->TotalXp = TotalXp;
		Next->RaidXp = RaidXp;
		Next->bInRaid = bInRaid;
		Next->AttributeAllocation = AttributeAllocation;
		Next->RespecTokens = RespecTokens;
		Next->SkillLibrary = SkillLibrary;
	}
}

void ACrossguardPlayerState::OnRep_TeamId()
{
	OnTeamChanged.Broadcast(TeamId);
}

FGenericTeamId ACrossguardPlayerState::GetGenericTeamId() const
{
	return TeamId >= 0 && TeamId < FGenericTeamId::NoTeam.GetId() ? FGenericTeamId(static_cast<uint8>(TeamId)) : FGenericTeamId::NoTeam;
}

int32 ACrossguardPlayerState::GetCharacterLevel() const
{
	return Crossguard::LevelForTotalXp(ProgressionConfig, TotalXp);
}

void ACrossguardPlayerState::GetLevelProgress(int32& XpIntoLevel, int32& XpForNextLevel) const
{
	const int32 CurrentLevel = GetCharacterLevel();
	XpIntoLevel = TotalXp - Crossguard::TotalXpForLevel(ProgressionConfig, CurrentLevel);
	XpForNextLevel = Crossguard::XpToNextLevel(ProgressionConfig, CurrentLevel);
}

bool ACrossguardPlayerState::HasLearnedSkill(FName SkillId) const
{
	return SkillLibrary.Contains(NormalizeSkillId(SkillId));
}

void ACrossguardPlayerState::AwardXp(ECrossguardXpSource Source, int32 QuestXp, ECrossguardRarity Rarity)
{
	if (HasAuthority())
	{
		AddXp(Crossguard::XpForSource(XpConfig, ToRulesXpSource(Source), QuestXp, static_cast<Crossguard::ERarity>(static_cast<uint8>(Rarity))));
	}
}

void ACrossguardPlayerState::AddXp(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}
	if (bInRaid)
	{
		RaidXp += Amount;
	}
	else
	{
		TotalXp += Amount;
	}
	ProgressionChanged();
}

void ACrossguardPlayerState::BeginRaid()
{
	if (HasAuthority() && !bInRaid)
	{
		bInRaid = true;
		RaidXp = 0;
		ProgressionChanged();
	}
}

int32 ACrossguardPlayerState::EndRaid(bool bExtracted)
{
	if (!HasAuthority() || !bInRaid)
	{
		return 0;
	}
	Crossguard::FRaidXp Raid;
	Raid.Add(RaidXp);
	// A failed extraction loses the raid XP.
	const int32 Banked = bExtracted ? Raid.Extract(XpConfig) : 0;
	TotalXp += Banked;
	RaidXp = 0;
	bInRaid = false;
	ProgressionChanged();
	return Banked;
}

void ACrossguardPlayerState::GrantRespecTokens(int32 Count)
{
	if (HasAuthority() && Count > 0)
	{
		RespecTokens += Count;
		ProgressionChanged();
	}
}

ECrossguardProgressionError ACrossguardPlayerState::LearnSkill(FName SkillId, const UCrossguardSkillCatalog* Catalog)
{
	if (!HasAuthority() || Catalog == nullptr)
	{
		return ECrossguardProgressionError::NotReady;
	}

	const FName Id = NormalizeSkillId(SkillId);
	Crossguard::FSkillLibrary Library;
	for (const FName& Learned : SkillLibrary)
	{
		Library.Add(TCHAR_TO_UTF8(*Learned.ToString()));
	}
	switch (Crossguard::LearnSkillGem(Catalog->BuildRulesCatalog(), Library, TCHAR_TO_UTF8(*Id.ToString()), bInRaid))
	{
	case Crossguard::ELearnError::None:
		break;
	case Crossguard::ELearnError::InRaid:
		return ECrossguardProgressionError::InRaid;
	case Crossguard::ELearnError::UnknownSkill:
		return ECrossguardProgressionError::UnknownSkill;
	case Crossguard::ELearnError::AlreadyLearned:
		return ECrossguardProgressionError::AlreadyLearned;
	}

	SkillLibrary.Add(Id);
	ProgressionChanged();
	return ECrossguardProgressionError::None;
}

void ACrossguardPlayerState::LearnSkillForTesting(FName SkillId)
{
	const FName Id = NormalizeSkillId(SkillId);
	if (HasAuthority() && !SkillLibrary.Contains(Id))
	{
		SkillLibrary.Add(Id);
		ProgressionChanged();
	}
}

void ACrossguardPlayerState::RequestAttributeAllocation(const FCrossguardAttributeAllocation& Allocation)
{
	if (HasAuthority())
	{
		const ECrossguardProgressionError Error = ApplyAttributeAllocation(Allocation);
		if (Error != ECrossguardProgressionError::None)
		{
			OnProgressionRejected.Broadcast(Error);
		}
	}
	else
	{
		ServerRequestAttributeAllocation(Allocation);
	}
}

void ACrossguardPlayerState::ServerRequestAttributeAllocation_Implementation(const FCrossguardAttributeAllocation& Allocation)
{
	const ECrossguardProgressionError Error = ApplyAttributeAllocation(Allocation);
	if (Error != ECrossguardProgressionError::None)
	{
		ClientProgressionRejected(Error);
	}
}

void ACrossguardPlayerState::ClientProgressionRejected_Implementation(ECrossguardProgressionError Error)
{
	OnProgressionRejected.Broadcast(Error);
}

ECrossguardProgressionError ACrossguardPlayerState::ApplyAttributeAllocation(const FCrossguardAttributeAllocation& Allocation)
{
	if (bInRaid)
	{
		return ECrossguardProgressionError::InRaid;
	}
	const Crossguard::FAttributes Requested = ToRulesAttributes(Allocation);
	if (Crossguard::ValidateAllocation(ProgressionConfig, GetCharacterLevel(), Requested) != Crossguard::EAllocationError::None)
	{
		return ECrossguardProgressionError::InvalidAllocation;
	}
	if (Crossguard::RequiresRespec(ToRulesAttributes(AttributeAllocation), Requested))
	{
		switch (Crossguard::CanRespec(bInRaid, RespecTokens))
		{
		case Crossguard::ERespecError::None:
			break;
		case Crossguard::ERespecError::InRaid:
			return ECrossguardProgressionError::InRaid;
		case Crossguard::ERespecError::NoRespecToken:
			return ECrossguardProgressionError::NoRespecToken;
		}
		--RespecTokens;
	}

	AttributeAllocation = Allocation;
	ProgressionChanged();
	return ECrossguardProgressionError::None;
}

void ACrossguardPlayerState::ProgressionChanged()
{
	if (ACrossguardCharacter* Character = GetPawn<ACrossguardCharacter>())
	{
		Character->ApplyProgression();
	}
	OnProgressionChanged.Broadcast();
}

void ACrossguardPlayerState::OnRep_Progression()
{
	OnProgressionChanged.Broadcast();
}
