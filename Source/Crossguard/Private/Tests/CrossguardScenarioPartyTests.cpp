// Parties in the hub (docs/parties.md): invites, allies through the team id, what replicates to whom,
// leave / kick / leader handoff, and the raid launch plan. The travel itself is never started here:
// only the URL and the travel mode are checked (UCrossguardPartySubsystem::PlanRaidTravel).
// Framework: CrossguardScenario.h; docs/editor-tests.md ("Networked scenarios").

#include "Tests/CrossguardScenario.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/CrossguardCharacter.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/CrossguardGameMode.h"
#include "Game/CrossguardGuildSubsystem.h"
#include "Game/CrossguardPartyComponent.h"
#include "Game/CrossguardPartySubsystem.h"
#include "Game/CrossguardPlayerState.h"
#include "Game/CrossguardTeamLibrary.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"

namespace CrossguardScenarioParty
{
	// The scene row (cm): away from the Player Start at (-30 m, -30 m) and the other files' rows.
	constexpr float PartySceneY = 12000.0f;
	// Game seconds a replicated change may take to show on a client.
	constexpr float PartyReplicationWait = 5.0f;

	FString PartyEnumName(ECrossguardPartyResult Value)
	{
		return StaticEnum<ECrossguardPartyResult>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	FString TravelEnumName(ECrossguardRaidTravel Value)
	{
		return StaticEnum<ECrossguardRaidTravel>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	void ExpectParty(CrossguardScenario::FScenarioContext& C, const TCHAR* What, ECrossguardPartyResult Actual, ECrossguardPartyResult Expected)
	{
		C.Test.TestEqual(What, PartyEnumName(Actual), PartyEnumName(Expected));
	}

	UCrossguardPartySubsystem* PartiesOf(CrossguardScenario::FScenarioContext& C)
	{
		UGameInstance* GameInstance = C.World() != nullptr ? C.World()->GetGameInstance() : nullptr;
		UCrossguardPartySubsystem* Subsystem = GameInstance != nullptr ? GameInstance->GetSubsystem<UCrossguardPartySubsystem>() : nullptr;
		C.Require(Subsystem != nullptr, TEXT("the server game instance has a UCrossguardPartySubsystem"));
		return Subsystem;
	}

	UCrossguardPartyComponent* PartyOf(const ACrossguardPlayerState* PlayerState)
	{
		return PlayerState != nullptr ? PlayerState->GetParty() : nullptr;
	}

	FString PartyPlayerId(const ACrossguardPlayerState* PlayerState)
	{
		return UCrossguardGuildSubsystem::PlayerIdOf(PlayerState);
	}

	// Server: client Index's player state.
	ACrossguardPlayerState* ServerStateOf(const CrossguardScenario::FScenarioContext& C, int32 Index)
	{
		return C.PlayerState(C.ServerPawnOf(Index));
	}

	// Client Index's own player state, in its world.
	ACrossguardPlayerState* OwnStateOn(const CrossguardScenario::FScenarioContext& C, int32 Index)
	{
		const APlayerController* Controller = C.ClientController(Index);
		return Controller != nullptr ? Controller->GetPlayerState<ACrossguardPlayerState>() : nullptr;
	}

	// Server: the clients' player states (party components included) with distinct party ids.
	bool GetServerStates(CrossguardScenario::FScenarioContext& C, TArray<ACrossguardPlayerState*>& Out)
	{
		Out.Reset();
		for (int32 Index = 0; Index < C.ClientCount(); ++Index)
		{
			ACrossguardPlayerState* State = ServerStateOf(C, Index);
			if (!C.Require(State != nullptr && PartyOf(State) != nullptr, FString::Printf(TEXT("client %d has a player state with a party component"), Index)))
			{
				return false;
			}
			for (const ACrossguardPlayerState* Earlier : Out)
			{
				if (!C.Require(PartyPlayerId(Earlier) != PartyPlayerId(State), TEXT("the clients have different player ids")))
				{
					return false;
				}
			}
			Out.Add(State);
		}
		return true;
	}

	// Server: client pawns in a row, 3 m apart.
	bool PlacePartyClients(CrossguardScenario::FScenarioContext& C)
	{
		for (int32 Index = 0; Index < C.ClientCount(); ++Index)
		{
			ACrossguardCharacter* Pawn = C.ServerPawnOf(Index);
			if (!C.Require(Pawn != nullptr, FString::Printf(TEXT("client %d has a server pawn"), Index)))
			{
				return false;
			}
			C.Place(Pawn, FVector(300.0f * Index, PartySceneY, 0.0f), 0.0f);
		}
		return true;
	}

	// "Party request from <owner> refused: <ECrossguardPartyResult>".
	bool PartyRefusedWith(const CrossguardScenario::FScenarioContext& C, ECrossguardPartyResult Result)
	{
		const FString Refusal = FString::Printf(TEXT("refused: %s"), *PartyEnumName(Result));
		for (const FString& Line : C.LogLines())
		{
			if (Line.Contains(TEXT("Party request from ")) && Line.TrimEnd().EndsWith(Refusal))
			{
				return true;
			}
		}
		return false;
	}

	// The party component shows no party and no roster (what a non-member's copy must look like).
	bool ShowsNothing(const ACrossguardPlayerState* PlayerState)
	{
		const UCrossguardPartyComponent* Party = PartyOf(PlayerState);
		return Party != nullptr && !Party->IsInParty() && Party->GetMembers().IsEmpty() && Party->GetLeaderId().IsEmpty();
	}
}

// --- Invite, accept, allies, visibility ----------------------------------------------------------

// Listen server + 2 clients, in the hub. The host invites client 1 (founding a party), client 1
// accepts on its own client (RPC): on the server both are on the party's team at once, the outsider
// is not; client 1's own component shows the party (two members, the host leading) and the host as
// an ally; the outsider's client sees the two on one team but no party data anywhere, and client 1's
// copy of the host's component is empty too (owner only).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetPartyInviteAccept, "Crossguard.Net.PartyInviteAcceptMakesAllies", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetPartyInviteAccept::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioParty;

	FScenario Scenario(*this);
	Scenario.Clients(3);
	Scenario.Raid(false);

	Scenario.Step(TEXT("The host invites client 1: a party led by the host"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		if (!PlacePartyClients(C) || !GetServerStates(C, States))
		{
			return;
		}
		C.Test.TestFalse(TEXT("Before: the host and client 1 are not allies"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(1)));
		// The host's player is local on the listen server: the request runs at once.
		PartyOf(States[0])->RequestInvite(States[1]);
		const UCrossguardPartyComponent* HostParty = PartyOf(States[0]);
		if (!C.Require(HostParty->IsInParty() && HostParty->IsLeader(), TEXT("inviting founded a party the host leads")))
		{
			return;
		}
		C.Test.TestEqual(TEXT("The host's roster"), HostParty->GetMembers().Num(), 1);
		C.Test.TestEqual(TEXT("Server: client 1 holds one invite"), PartyOf(States[1])->GetInvites().Num(), 1);
		C.Test.TestFalse(TEXT("An invite alone makes no allies"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(1)));
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1 sees the invite"), PartyReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const UCrossguardPartyComponent* Own = PartyOf(OwnStateOn(C, 1));
		return Own != nullptr && Own->GetInvites().Num() == 1;
	});

	Scenario.Step(TEXT("Client 1 accepts on its own client"), [](FScenarioContext& C)
	{
		UCrossguardPartyComponent* Own = PartyOf(OwnStateOn(C, 1));
		if (C.Require(Own != nullptr && Own->GetInvites().Num() == 1, TEXT("client 1 still has the invite")))
		{
			C.Test.TestEqual(TEXT("The invite names the party"), Own->GetInvites()[0].PartyId, PartyOf(ServerStateOf(C, 0))->GetPartyId());
			Own->RequestAccept(Own->GetInvites()[0].PartyId);
		}
	});

	Scenario.WaitUntil(TEXT("server: client 1 joined the party"), PartyReplicationWait, [](FScenarioContext& C)
	{
		const UCrossguardPartyComponent* Host = PartyOf(ServerStateOf(C, 0));
		const UCrossguardPartyComponent* One = PartyOf(ServerStateOf(C, 1));
		return Host != nullptr && One != nullptr && One->IsInParty() && One->GetPartyId() == Host->GetPartyId();
	});

	Scenario.Step(TEXT("Server: the host and client 1 are allies on the party's team; the outsider is not"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		if (!GetServerStates(C, States))
		{
			return;
		}
		const FString PartyId = PartyOf(States[0])->GetPartyId();
		C.Test.TestTrue(TEXT("Host and client 1 are allies"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(1)));
		C.Test.TestFalse(TEXT("Host and the outsider are not"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(2)));
		C.Test.TestFalse(TEXT("Client 1 and the outsider are not"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(1), C.ServerPawnOf(2)));
		C.Test.TestEqual(TEXT("The host's party key is the party id"), States[0]->PartyKey, PartyId);
		C.Test.TestEqual(TEXT("Client 1's party key is the party id"), States[1]->PartyKey, PartyId);
		C.Test.TestTrue(TEXT("The outsider has no party key"), States[2]->PartyKey.IsEmpty());
		C.Test.TestEqual(TEXT("The host's roster"), PartyOf(States[0])->GetMembers().Num(), 2);
		C.Test.TestFalse(TEXT("Client 1 does not lead"), PartyOf(States[1])->IsLeader());
		C.Test.TestEqual(TEXT("Client 1's invites are used up"), PartyOf(States[1])->GetInvites().Num(), 0);
		C.Test.TestTrue(TEXT("The outsider's component is empty"), ShowsNothing(States[2]));
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1 sees the party (two members, the host leading) and the host as its ally"), PartyReplicationWait,
		[](FScenarioContext& C, UWorld&)
		{
			const ACrossguardPlayerState* ServerHost = ServerStateOf(C, 0);
			const ACrossguardPlayerState* Own = OwnStateOn(C, 1);
			const ACrossguardPlayerState* HostCopy = C.OnClient<ACrossguardPlayerState>(ServerHost, 1);
			const UCrossguardPartyComponent* Party = PartyOf(Own);
			return Party != nullptr && HostCopy != nullptr && Party->IsInParty() && !Party->IsLeader() && Party->GetMembers().Num() == 2
				&& Party->GetLeaderId() == PartyPlayerId(ServerHost) && Party->GetPartyId() == PartyOf(ServerHost)->GetPartyId()
				&& Own->GetTeamId() >= 0 && HostCopy->GetTeamId() == Own->GetTeamId();
		});

	Scenario.WaitUntilOnClient(2, TEXT("the outsider sees the host and client 1 on one team, not its own"), PartyReplicationWait,
		[](FScenarioContext& C, UWorld&)
		{
			const ACrossguardPlayerState* HostCopy = C.OnClient<ACrossguardPlayerState>(ServerStateOf(C, 0), 2);
			const ACrossguardPlayerState* OneCopy = C.OnClient<ACrossguardPlayerState>(ServerStateOf(C, 1), 2);
			const ACrossguardPlayerState* Own = OwnStateOn(C, 2);
			return HostCopy != nullptr && OneCopy != nullptr && Own != nullptr && HostCopy->GetTeamId() >= 0
				&& HostCopy->GetTeamId() == OneCopy->GetTeamId() && Own->GetTeamId() != HostCopy->GetTeamId();
		});

	Scenario.Step(TEXT("No party data reaches anyone but its owner"), [](FScenarioContext& C)
	{
		const ACrossguardPlayerState* HostOnOutsider = C.OnClient<ACrossguardPlayerState>(ServerStateOf(C, 0), 2);
		const ACrossguardPlayerState* OneOnOutsider = C.OnClient<ACrossguardPlayerState>(ServerStateOf(C, 1), 2);
		const ACrossguardPlayerState* HostOnOne = C.OnClient<ACrossguardPlayerState>(ServerStateOf(C, 0), 1);
		C.Test.TestTrue(TEXT("Outsider's client: its own component is empty"), ShowsNothing(OwnStateOn(C, 2)));
		C.Test.TestTrue(TEXT("Outsider's client: the host's component is empty"), ShowsNothing(HostOnOutsider));
		C.Test.TestTrue(TEXT("Outsider's client: client 1's component is empty"), ShowsNothing(OneOnOutsider));
		C.Test.TestTrue(TEXT("Client 1's client: the host's component is empty (owner only)"), ShowsNothing(HostOnOne));
	});

	Scenario.Run();
	return true;
}

// --- Leave, kick, leader handoff -----------------------------------------------------------------

// Listen server + 2 clients, all three in the host's party. The host hands the lead to client 1; the
// old leader's kick is refused (NotLeader), the new leader kicks client 2 (no longer an ally, its
// client sees no party); the leader leaving hands the lead back to the host; in a raid every
// membership change and the raid launch are refused (OnlyInHub); the last member leaving disbands.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetPartyLeaveKick, "Crossguard.Net.PartyKickPromoteLeave", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetPartyLeaveKick::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioParty;

	FScenario Scenario(*this);
	Scenario.Clients(3);
	Scenario.Raid(false);

	Scenario.Step(TEXT("All three in the host's party"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (!PlacePartyClients(C) || Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		C.Require(Parties->Invite(States[0], States[1]) == ECrossguardPartyResult::None, TEXT("the host invites client 1"));
		C.Require(Parties->Accept(States[1], PartyOf(States[0])->GetPartyId()) == ECrossguardPartyResult::None, TEXT("client 1 accepts"));
		C.Require(Parties->Invite(States[0], States[2]) == ECrossguardPartyResult::None, TEXT("the host invites client 2"));
		C.Require(Parties->Accept(States[2], PartyOf(States[0])->GetPartyId()) == ECrossguardPartyResult::None, TEXT("client 2 accepts"));
		C.Test.TestEqual(TEXT("Roster"), PartyOf(States[0])->GetMembers().Num(), 3);
		C.Test.TestTrue(TEXT("Host and client 2 are allies"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(2)));
	});

	Scenario.Step(TEXT("The host hands the lead to client 1"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		ExpectParty(C, TEXT("Client 1 cannot promote while not leading"), Parties->Promote(States[1], PartyPlayerId(States[2])), ECrossguardPartyResult::NotLeader);
		ExpectParty(C, TEXT("Promote client 1"), Parties->Promote(States[0], PartyPlayerId(States[1])), ECrossguardPartyResult::None);
		C.Test.TestTrue(TEXT("Client 1 leads"), PartyOf(States[1])->IsLeader());
		C.Test.TestFalse(TEXT("The host no longer leads"), PartyOf(States[0])->IsLeader());
		C.Test.TestEqual(TEXT("Everyone's view names client 1"), PartyOf(States[2])->GetLeaderId(), PartyPlayerId(States[1]));
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1 sees itself leading"), PartyReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const UCrossguardPartyComponent* Own = PartyOf(OwnStateOn(C, 1));
		return Own != nullptr && Own->IsLeader();
	});

	Scenario.Step(TEXT("The old leader's kick is refused; the new leader kicks client 2"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		C.ClearLog();
		PartyOf(States[0])->RequestKick(PartyPlayerId(States[2]));
		C.Test.TestTrue(TEXT("Log: the host's kick refused NotLeader"), PartyRefusedWith(C, ECrossguardPartyResult::NotLeader));
		C.Test.TestTrue(TEXT("Client 2 is still in"), PartyOf(States[2])->IsInParty());

		ExpectParty(C, TEXT("Kick oneself"), Parties->Kick(States[1], PartyPlayerId(States[1])), ECrossguardPartyResult::CannotTargetSelf);
		ExpectParty(C, TEXT("Client 1 kicks client 2"), Parties->Kick(States[1], PartyPlayerId(States[2])), ECrossguardPartyResult::None);
		C.Test.TestTrue(TEXT("Client 2 is out"), ShowsNothing(States[2]));
		C.Test.TestTrue(TEXT("Client 2 has no party key"), States[2]->PartyKey.IsEmpty());
		C.Test.TestFalse(TEXT("Client 2 is no longer the host's ally"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(2)));
		C.Test.TestTrue(TEXT("Host and client 1 are still allies"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(1)));
		C.Test.TestEqual(TEXT("Roster"), PartyOf(States[0])->GetMembers().Num(), 2);
		ExpectParty(C, TEXT("Kick someone not in the party"), Parties->Kick(States[1], PartyPlayerId(States[2])), ECrossguardPartyResult::TargetNotInParty);
	});

	Scenario.WaitUntilOnClient(2, TEXT("client 2 sees no party and the host on another team"), PartyReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const ACrossguardPlayerState* Own = OwnStateOn(C, 2);
		const ACrossguardPlayerState* HostCopy = C.OnClient<ACrossguardPlayerState>(ServerStateOf(C, 0), 2);
		return Own != nullptr && HostCopy != nullptr && ShowsNothing(Own) && Own->GetTeamId() >= 0 && Own->GetTeamId() != HostCopy->GetTeamId();
	});

	Scenario.Step(TEXT("The leader leaves: the lead goes back to the host"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		ExpectParty(C, TEXT("Client 1 leaves"), Parties->Leave(States[1]), ECrossguardPartyResult::None);
		C.Test.TestTrue(TEXT("The host leads"), PartyOf(States[0])->IsLeader());
		C.Test.TestEqual(TEXT("Roster"), PartyOf(States[0])->GetMembers().Num(), 1);
		C.Test.TestTrue(TEXT("Client 1 is out"), ShowsNothing(States[1]));
		C.Test.TestFalse(TEXT("Host and client 1 are no longer allies"), UCrossguardTeamLibrary::AreAllies(C.ServerPawnOf(0), C.ServerPawnOf(1)));
		ExpectParty(C, TEXT("Leave twice"), Parties->Leave(States[1]), ECrossguardPartyResult::NotInParty);
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1 sees no party"), PartyReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		return ShowsNothing(OwnStateOn(C, 1));
	});

	Scenario.Step(TEXT("In a raid, membership changes and the launch are refused"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		C.SetRaid(true);
		ExpectParty(C, TEXT("Invite in a raid"), Parties->Invite(States[0], States[1]), ECrossguardPartyResult::OnlyInHub);
		ExpectParty(C, TEXT("Leave in a raid"), Parties->Leave(States[0]), ECrossguardPartyResult::OnlyInHub);
		ExpectParty(C, TEXT("Start a raid from a raid"), Parties->StartRaid(States[0]), ECrossguardPartyResult::OnlyInHub);
		C.Test.TestTrue(TEXT("The host is still in its party"), PartyOf(States[0])->IsInParty());
		C.SetRaid(false);
	});

	Scenario.Step(TEXT("The last member leaving disbands the party"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		ExpectParty(C, TEXT("The host leaves"), Parties->Leave(States[0]), ECrossguardPartyResult::None);
		C.Test.TestTrue(TEXT("The host is out"), ShowsNothing(States[0]));
		C.Test.TestEqual(TEXT("No party left"), static_cast<int32>(Parties->GetRegistry().All().size()), 0);
	});

	Scenario.Run();
	return true;
}

// --- Invite inbox --------------------------------------------------------------------------------

// Listen server + 2 clients, in the hub. The host and client 2 both invite client 1 (two parties):
// client 1's own client lists both invites (owner only: the host's copy of client 1's component shows
// none). Client 1 deletes the host's invite from its console by the inviter's name (PartyDecline);
// client 2's stays and nobody joins anything. The host invites again; client 1 clears its inbox
// (PartyDeclineAll); clearing an empty inbox is refused (NoInvite). Declining works in a raid too.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardNetPartyInviteInbox, "Crossguard.Net.PartyInviteInboxDecline", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardNetPartyInviteInbox::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioParty;

	FScenario Scenario(*this);
	Scenario.Clients(3);
	Scenario.Raid(false);

	Scenario.Step(TEXT("The host and client 2 both invite client 1"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (!PlacePartyClients(C) || Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		ExpectParty(C, TEXT("The host invites client 1"), Parties->Invite(States[0], States[1]), ECrossguardPartyResult::None);
		ExpectParty(C, TEXT("Client 2 invites client 1"), Parties->Invite(States[2], States[1]), ECrossguardPartyResult::None);
		const TArray<FCrossguardPartyInviteView>& Invites = PartyOf(States[1])->GetInvites();
		C.Test.TestEqual(TEXT("Server: client 1's inbox holds two invites"), Invites.Num(), 2);
		for (const FCrossguardPartyInviteView& Invite : Invites)
		{
			const ACrossguardPlayerState* Inviter = Invite.PartyId == PartyOf(States[0])->GetPartyId() ? States[0] : States[2];
			C.Test.TestEqual(TEXT("The invite names its inviter"), Invite.InvitedBy, Inviter->GetPlayerName());
			C.Test.TestEqual(TEXT("The invite carries the inviter's id"), Invite.InvitedById, PartyPlayerId(Inviter));
		}
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1 sees both invites"), PartyReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const UCrossguardPartyComponent* Own = PartyOf(OwnStateOn(C, 1));
		return Own != nullptr && Own->GetInvites().Num() == 2;
	});

	Scenario.Step(TEXT("Client 1 deletes the host's invite from its console"), [](FScenarioContext& C)
	{
		const ACrossguardPlayerState* Host = ServerStateOf(C, 0);
		const UCrossguardPartyComponent* HostOnOne = PartyOf(C.OnClient<ACrossguardPlayerState>(Host, 1));
		C.Test.TestTrue(TEXT("Client 1's copy of the host's component lists no invites"), HostOnOne == nullptr || HostOnOne->GetInvites().IsEmpty());
		ACrossguardCharacter* Pawn = C.ClientPawn(1);
		if (C.Require(Pawn != nullptr && Host != nullptr, TEXT("client 1 has its own pawn")))
		{
			Pawn->PartyInvites();
			Pawn->PartyDecline(Host->GetPlayerName());
		}
	});

	Scenario.WaitUntil(TEXT("server: client 1 holds only client 2's invite"), PartyReplicationWait, [](FScenarioContext& C)
	{
		const UCrossguardPartyComponent* One = PartyOf(ServerStateOf(C, 1));
		const UCrossguardPartyComponent* Two = PartyOf(ServerStateOf(C, 2));
		return One != nullptr && Two != nullptr && One->GetInvites().Num() == 1 && One->GetInvites()[0].PartyId == Two->GetPartyId();
	});

	Scenario.Step(TEXT("Nobody joined anything; the host invites again"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		C.Test.TestFalse(TEXT("Client 1 is in no party"), PartyOf(States[1])->IsInParty());
		C.Test.TestEqual(TEXT("The host's party is still just the host"), PartyOf(States[0])->GetMembers().Num(), 1);
		ExpectParty(C, TEXT("Accepting the declined invite"), Parties->Accept(States[1], PartyOf(States[0])->GetPartyId()), ECrossguardPartyResult::NoInvite);
		ExpectParty(C, TEXT("The host invites client 1 again"), Parties->Invite(States[0], States[1]), ECrossguardPartyResult::None);
		C.Test.TestEqual(TEXT("Two invites again"), PartyOf(States[1])->GetInvites().Num(), 2);
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1 sees two invites again"), PartyReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const UCrossguardPartyComponent* Own = PartyOf(OwnStateOn(C, 1));
		return Own != nullptr && Own->GetInvites().Num() == 2;
	});

	Scenario.Step(TEXT("Client 1 clears its inbox from its console"), [](FScenarioContext& C)
	{
		if (ACrossguardCharacter* Pawn = C.ClientPawn(1); C.Require(Pawn != nullptr, TEXT("client 1 has its own pawn")))
		{
			Pawn->PartyDeclineAll();
		}
	});

	Scenario.WaitUntilOnClient(1, TEXT("client 1's inbox is empty"), PartyReplicationWait, [](FScenarioContext& C, UWorld&)
	{
		const UCrossguardPartyComponent* Own = PartyOf(OwnStateOn(C, 1));
		return Own != nullptr && Own->GetInvites().IsEmpty();
	});

	Scenario.Step(TEXT("Server: the inbox is empty; clearing it again is refused; declining works in a raid"), [](FScenarioContext& C)
	{
		TArray<ACrossguardPlayerState*> States;
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (Parties == nullptr || !GetServerStates(C, States))
		{
			return;
		}
		C.Test.TestTrue(TEXT("Server: client 1 has no invites"), PartyOf(States[1])->GetInvites().IsEmpty());
		C.Test.TestTrue(TEXT("Rules: no invite to client 1 left"),
			Parties->GetRegistry().InvitesFor(TCHAR_TO_UTF8(*PartyPlayerId(States[1])), 0.0).empty());
		ExpectParty(C, TEXT("Clearing an empty inbox"), Parties->DeclineAll(States[1]), ECrossguardPartyResult::NoInvite);
		ExpectParty(C, TEXT("The host invites client 1 once more"), Parties->Invite(States[0], States[1]), ECrossguardPartyResult::None);
		C.SetRaid(true);
		ExpectParty(C, TEXT("Declining in a raid"), Parties->Decline(States[1], PartyOf(States[0])->GetPartyId()), ECrossguardPartyResult::None);
		C.SetRaid(false);
		C.Test.TestTrue(TEXT("Client 1's inbox is empty again"), PartyOf(States[1])->GetInvites().IsEmpty());
	});

	Scenario.Run();
	return true;
}

// --- Raid launch plan ----------------------------------------------------------------------------

// Standalone hub. Who may launch (leader or solo, not a member, not in a raid) and what the launch
// would do, without travelling: no destination -> refused; a raid server set -> each client travels
// to "<address>?Party=<id>" (a solo player without the option); the whole server in the party and a
// raid map set -> the server travels to "<map>?Party=<id>". StartRaid is only called where it must
// refuse.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossguardScenarioPartyRaidLaunch, "Crossguard.Scenario.Hub.PartyRaidLaunchPlan", CROSSGUARD_SCENARIO_FLAGS)

bool FCrossguardScenarioPartyRaidLaunch::RunTest(const FString& Parameters)
{
	using namespace CrossguardScenario;
	using namespace CrossguardScenarioParty;

	FScenario Scenario(*this);
	Scenario.Raid(false);

	Scenario.Step(TEXT("A leads a party with B; Solo is on their own"), [](FScenarioContext& C)
	{
		ACrossguardCharacter* A = C.SpawnPlayer(TEXT("A"), FVector(0.0f, PartySceneY, 0.0f));
		ACrossguardCharacter* B = C.SpawnPlayer(TEXT("B"), FVector(200.0f, PartySceneY, 0.0f));
		ACrossguardCharacter* Solo = C.SpawnPlayer(TEXT("Solo"), FVector(400.0f, PartySceneY, 0.0f));
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		if (!C.Require(A != nullptr && B != nullptr && Solo != nullptr && Parties != nullptr && C.GameMode() != nullptr, TEXT("A, B and Solo spawned")))
		{
			return;
		}
		C.Require(Parties->Invite(C.PlayerState(A), C.PlayerState(B)) == ECrossguardPartyResult::None, TEXT("A invites B"));
		C.Require(Parties->Accept(C.PlayerState(B), PartyOf(C.PlayerState(A))->GetPartyId()) == ECrossguardPartyResult::None, TEXT("B accepts"));
		C.Test.TestTrue(TEXT("A and B are allies"), UCrossguardTeamLibrary::AreAllies(A, B));
		C.Test.TestFalse(TEXT("Solo is not their ally"), UCrossguardTeamLibrary::AreAllies(A, Solo));
		// No destination configured (whatever the Blueprint says).
		C.GameMode()->RaidMapName.Reset();
		C.GameMode()->RaidServerAddress.Reset();
	});

	Scenario.Step(TEXT("Who may launch; nothing to launch to"), [](FScenarioContext& C)
	{
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		ACrossguardPlayerState* A = C.PlayerState(C.Get<ACrossguardCharacter>(TEXT("A")));
		ACrossguardPlayerState* B = C.PlayerState(C.Get<ACrossguardCharacter>(TEXT("B")));
		if (Parties == nullptr || !C.Require(A != nullptr && B != nullptr, TEXT("A and B exist")))
		{
			return;
		}
		ECrossguardRaidTravel Mode = ECrossguardRaidTravel::ServerTravel;
		FString Url;
		ExpectParty(C, TEXT("A member cannot launch"), Parties->StartRaid(B), ECrossguardPartyResult::NotLeader);
		ExpectParty(C, TEXT("No destination"), Parties->PlanRaidTravel(A, Mode, Url), ECrossguardPartyResult::NoRaidDestination);
		C.Test.TestEqual(TEXT("Mode"), TravelEnumName(Mode), TravelEnumName(ECrossguardRaidTravel::NoDestination));
		C.Test.TestTrue(TEXT("No URL"), Url.IsEmpty());
	});

	Scenario.Step(TEXT("A raid server: each member's client would travel there with the party key"), [](FScenarioContext& C)
	{
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		ACrossguardPlayerState* A = C.PlayerState(C.Get<ACrossguardCharacter>(TEXT("A")));
		ACrossguardPlayerState* Solo = C.PlayerState(C.Get<ACrossguardCharacter>(TEXT("Solo")));
		if (Parties == nullptr || !C.Require(A != nullptr && Solo != nullptr, TEXT("A and Solo exist")))
		{
			return;
		}
		C.GameMode()->RaidServerAddress = TEXT("127.0.0.1:7777");
		C.GameMode()->RaidMapName = TEXT("/Game/L_Test");
		const FString PartyId = PartyOf(A)->GetPartyId();
		ECrossguardRaidTravel Mode = ECrossguardRaidTravel::NoDestination;
		FString Url;
		// Others (Solo, the PIE player) are on the server: no server travel, even with a raid map.
		ExpectParty(C, TEXT("Plan with a raid server"), Parties->PlanRaidTravel(A, Mode, Url), ECrossguardPartyResult::None);
		C.Test.TestEqual(TEXT("Mode"), TravelEnumName(Mode), TravelEnumName(ECrossguardRaidTravel::ClientTravel));
		C.Test.TestEqual(TEXT("URL"), Url, FString::Printf(TEXT("127.0.0.1:7777?%s=%s"), ACrossguardGameMode::PartyOption, *PartyId));
		// What the raid server's game mode reads at login.
		const FURL Parsed(nullptr, *Url, TRAVEL_Absolute);
		C.Test.TestEqual(TEXT("The URL's Party option"), FString(Parsed.GetOption(*FString::Printf(TEXT("%s="), ACrossguardGameMode::PartyOption), TEXT(""))), PartyId);

		ExpectParty(C, TEXT("Solo plans alone"), Parties->PlanRaidTravel(Solo, Mode, Url), ECrossguardPartyResult::None);
		C.Test.TestEqual(TEXT("Solo: client travel"), TravelEnumName(Mode), TravelEnumName(ECrossguardRaidTravel::ClientTravel));
		C.Test.TestEqual(TEXT("Solo: no party key"), Url, FString(TEXT("127.0.0.1:7777")));
		C.GameMode()->RaidServerAddress.Reset();
	});

	Scenario.Step(TEXT("Everyone on the server in the party and a raid map: the server would travel"), [](FScenarioContext& C)
	{
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		ACrossguardPlayerState* A = C.PlayerState(C.Get<ACrossguardCharacter>(TEXT("A")));
		ACrossguardPlayerState* Solo = C.PlayerState(C.Get<ACrossguardCharacter>(TEXT("Solo")));
		ACrossguardPlayerState* Local = C.PlayerState(C.LocalPlayer());
		const AGameStateBase* GameState = C.World() != nullptr ? C.World()->GetGameState() : nullptr;
		if (Parties == nullptr || !C.Require(A != nullptr && Solo != nullptr && Local != nullptr && GameState != nullptr, TEXT("A, Solo and the PIE player exist")))
		{
			return;
		}
		const FString PartyId = PartyOf(A)->GetPartyId();
		ECrossguardRaidTravel Mode = ECrossguardRaidTravel::ServerTravel;
		FString Url;
		ExpectParty(C, TEXT("Not the whole server, no raid server"), Parties->PlanRaidTravel(A, Mode, Url), ECrossguardPartyResult::NoRaidDestination);

		for (ACrossguardPlayerState* Joiner : {Solo, Local})
		{
			C.Require(Parties->Invite(A, Joiner) == ECrossguardPartyResult::None, TEXT("A invites"));
			C.Require(Parties->Accept(Joiner, PartyId) == ECrossguardPartyResult::None, TEXT("the invitee accepts"));
		}
		if (!C.Require(GameState->PlayerArray.Num() == PartyOf(A)->GetMembers().Num(), TEXT("every player on the server is in A's party")))
		{
			return;
		}
		ExpectParty(C, TEXT("Plan for the whole server"), Parties->PlanRaidTravel(A, Mode, Url), ECrossguardPartyResult::None);
		C.Test.TestEqual(TEXT("Mode"), TravelEnumName(Mode), TravelEnumName(ECrossguardRaidTravel::ServerTravel));
		const bool bListen = C.World()->GetNetMode() == NM_ListenServer;
		C.Test.TestEqual(TEXT("URL"), Url, UCrossguardPartySubsystem::RaidTravelUrl(TEXT("/Game/L_Test"), PartyId, bListen ? TEXT("listen") : TEXT("")));
		C.Test.TestTrue(TEXT("URL starts with the map and carries the key"), Url.StartsWith(FString::Printf(TEXT("/Game/L_Test?%s=%s"), ACrossguardGameMode::PartyOption, *PartyId)));
	});

	Scenario.Step(TEXT("In a raid the launch is refused"), [](FScenarioContext& C)
	{
		UCrossguardPartySubsystem* Parties = PartiesOf(C);
		ACrossguardPlayerState* A = C.PlayerState(C.Get<ACrossguardCharacter>(TEXT("A")));
		if (Parties == nullptr || A == nullptr)
		{
			return;
		}
		C.SetRaid(true);
		ECrossguardRaidTravel Mode = ECrossguardRaidTravel::ServerTravel;
		FString Url;
		ExpectParty(C, TEXT("Plan in a raid"), Parties->PlanRaidTravel(A, Mode, Url), ECrossguardPartyResult::OnlyInHub);
		ExpectParty(C, TEXT("Start in a raid"), Parties->StartRaid(A), ECrossguardPartyResult::OnlyInHub);
		// Leave nothing that could travel when PIE ends.
		C.GameMode()->RaidMapName.Reset();
	});

	Scenario.Run();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
