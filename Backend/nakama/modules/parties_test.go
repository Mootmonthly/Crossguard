package main

import (
	"encoding/json"
	"fmt"
	"testing"
	"time"
)

// uid: a test user id ("00000000-0000-4000-8000-0000000000NN").
func uid(n int) string { return fmt.Sprintf("00000000-0000-4000-8000-%012d", n) }

func partyOp(t *testing.T, nk *fakeNakama, op, actor, target, partyID string) partyStateResponse {
	t.Helper()
	payload, _ := json.Marshal(map[string]string{"op": op, "actor_id": actor, "target_id": target, "party_id": partyID, "server_id": "hub-1"})
	out, err := RpcPartyFn(serverCtx(), &fakeLogger{}, nil, nk, string(payload))
	if err != nil {
		t.Fatalf("%s by %s: %v", op, actor, err)
	}
	var response partyStateResponse
	if err := json.Unmarshal([]byte(out), &response); err != nil {
		t.Fatalf("%s answer %q: %v", op, out, err)
	}
	return response
}

func expectResult(t *testing.T, got partyStateResponse, want string) {
	t.Helper()
	if got.Result != want {
		t.Fatalf("result %q, want %q (%+v)", got.Result, want, got)
	}
}

func partyGet(t *testing.T, nk *fakeNakama, userIDs ...string) partyStateResponse {
	t.Helper()
	payload, _ := json.Marshal(map[string]any{"user_ids": userIDs})
	out, err := RpcPartyGetFn(serverCtx(), &fakeLogger{}, nil, nk, string(payload))
	if err != nil {
		t.Fatalf("party get: %v", err)
	}
	var response partyStateResponse
	if err := json.Unmarshal([]byte(out), &response); err != nil {
		t.Fatalf("party get answer %q: %v", out, err)
	}
	return response
}

func TestPartyInviteAcceptLeave(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	lead, b, c := uid(1), uid(2), uid(3)
	nk := newFakeNakama(lead, b, c)

	// A solo player's invite founds a party, with them as leader.
	invited := partyOp(t, nk, "invite", lead, b, "")
	expectResult(t, invited, partyResultNone)
	partyID := invited.PartyID
	if !characterIDPattern.MatchString(partyID) || invited.Players[lead].PartyID != partyID || invited.Parties[partyID].LeaderID != lead {
		t.Fatalf("founded party: %+v", invited)
	}
	if invites := invited.Players[b].Invites; len(invites) != 1 || invites[0].PartyID != partyID || invites[0].InvitedBy != lead ||
		invites[0].ExpiresAtMs != clock.Add(partyInviteExpirySec*time.Second).UnixMilli() {
		t.Fatalf("invitee's inbox: %+v", invited.Players[b])
	}
	if invited.Parties[partyID].HubServerID != "hub-1" {
		t.Fatalf("hub server not recorded: %+v", invited.Parties[partyID])
	}
	expectResult(t, partyOp(t, nk, "invite", lead, b, ""), partyAlreadyInvited)
	expectResult(t, partyOp(t, nk, "invite", lead, lead, ""), partyCannotTargetSelf)
	expectResult(t, partyOp(t, nk, "invite", lead, uid(99), ""), partyInvalidPlayer)

	accepted := partyOp(t, nk, "accept", b, "", partyID)
	expectResult(t, accepted, partyResultNone)
	if members := accepted.Parties[partyID].Members; len(members) != 2 || members[0] != lead || members[1] != b {
		t.Fatalf("members after accept: %+v", accepted.Parties[partyID])
	}
	if len(accepted.Players[b].Invites) != 0 || accepted.Players[b].PartyID != partyID {
		t.Fatalf("accepting player's state: %+v", accepted.Players[b])
	}
	expectResult(t, partyOp(t, nk, "accept", b, "", partyID), partyNoInvite)
	expectResult(t, partyOp(t, nk, "invite", lead, b, ""), partyAlreadyInParty)
	// Only the leader invites (a member does not found a second party).
	expectResult(t, partyOp(t, nk, "invite", b, c, ""), partyNotLeader)
	expectResult(t, partyOp(t, nk, "accept", c, "", "nope"), partyUnknownParty)

	// Expiry: an expired invite cannot be accepted and is replaced by a new one.
	expectResult(t, partyOp(t, nk, "invite", lead, c, ""), partyResultNone)
	*clock = clock.Add(partyInviteExpirySec * time.Second)
	expectResult(t, partyOp(t, nk, "accept", c, "", partyID), partyInviteExpired)
	if got := partyGet(t, nk, c); len(got.Players[c].Invites) != 0 {
		t.Fatalf("expired invite still listed: %+v", got.Players[c])
	}
	expectResult(t, partyOp(t, nk, "invite", lead, c, ""), partyResultNone)
	expectResult(t, partyOp(t, nk, "accept", c, "", partyID), partyResultNone)

	// The leader leaving hands the lead to the longest-standing member.
	left := partyOp(t, nk, "leave", lead, "", "")
	expectResult(t, left, partyResultNone)
	if left.Players[lead].PartyID != "" || left.Parties[partyID].LeaderID != b || len(left.Parties[partyID].Members) != 2 {
		t.Fatalf("after the leader left: %+v", left)
	}
	if _, ok := left.Players[c]; !ok {
		t.Fatal("other members' state missing from the answer")
	}
	expectResult(t, partyOp(t, nk, "leave", lead, "", ""), partyNotInParty)
	// The last members leaving disband it.
	expectResult(t, partyOp(t, nk, "leave", c, "", ""), partyResultNone)
	disbanded := partyOp(t, nk, "leave", b, "", "")
	if !disbanded.Disbanded || len(nk.objectsIn(collectionParties)) != 0 {
		t.Fatalf("disband: %+v, stored parties %d", disbanded, len(nk.objectsIn(collectionParties)))
	}
}

func TestPartyInboxDeclineAndAcceptDropsOthers(t *testing.T) {
	setClock(t, time.Unix(1_800_000_000, 0))
	l1, l2, x := uid(1), uid(2), uid(3)
	nk := newFakeNakama(l1, l2, x)
	p1 := partyOp(t, nk, "invite", l1, x, "").PartyID
	p2 := partyOp(t, nk, "invite", l2, x, "").PartyID
	if got := partyGet(t, nk, x); len(got.Players[x].Invites) != 2 || got.Parties[p1].ID != p1 || got.Parties[p2].ID != p2 {
		t.Fatalf("inbox of two: %+v", got)
	}
	expectResult(t, partyOp(t, nk, "decline", x, "", p1), partyResultNone)
	expectResult(t, partyOp(t, nk, "decline", x, "", p1), partyNoInvite)
	expectResult(t, partyOp(t, nk, "decline", x, "", "nope"), partyUnknownParty)
	expectResult(t, partyOp(t, nk, "invite", l1, x, ""), partyResultNone)
	expectResult(t, partyOp(t, nk, "decline_all", x, "", ""), partyResultNone)
	expectResult(t, partyOp(t, nk, "decline_all", x, "", ""), partyNoInvite)
	// Accepting one drops the other.
	partyOp(t, nk, "invite", l1, x, "")
	partyOp(t, nk, "invite", l2, x, "")
	expectResult(t, partyOp(t, nk, "accept", x, "", p2), partyResultNone)
	expectResult(t, partyOp(t, nk, "accept", x, "", p1), partyNoInvite)
	// An invite from a party that has since disbanded is not listed.
	partyOp(t, nk, "leave", x, "", "")
	partyOp(t, nk, "invite", l1, x, "")
	partyOp(t, nk, "leave", l1, "", "")
	if got := partyGet(t, nk, x); len(got.Players[x].Invites) != 0 {
		t.Fatalf("invite from a disbanded party listed: %+v", got.Players[x])
	}
	expectResult(t, partyOp(t, nk, "accept", x, "", p1), partyUnknownParty)
}

func TestPartyKickPromoteFullLogout(t *testing.T) {
	setClock(t, time.Unix(1_800_000_000, 0))
	var users []string
	for i := 1; i <= partyMaxMembers+1; i++ {
		users = append(users, uid(i))
	}
	nk := newFakeNakama(users...)
	lead := users[0]
	partyID := ""
	for _, member := range users[1:partyMaxMembers] {
		partyID = partyOp(t, nk, "invite", lead, member, "").PartyID
		expectResult(t, partyOp(t, nk, "accept", member, "", partyID), partyResultNone)
	}
	extra := users[partyMaxMembers]
	expectResult(t, partyOp(t, nk, "invite", lead, extra, ""), partyFull)

	b, c := users[1], users[2]
	expectResult(t, partyOp(t, nk, "kick", b, c, ""), partyNotLeader)
	expectResult(t, partyOp(t, nk, "kick", lead, lead, ""), partyCannotTargetSelf)
	expectResult(t, partyOp(t, nk, "kick", lead, extra, ""), partyTargetNotInParty)
	expectResult(t, partyOp(t, nk, "kick", extra, b, ""), partyNotInParty)
	kicked := partyOp(t, nk, "kick", lead, c, "")
	expectResult(t, kicked, partyResultNone)
	if kicked.Players[c].PartyID != "" || len(kicked.Parties[partyID].Members) != partyMaxMembers-1 {
		t.Fatalf("after kick: %+v", kicked)
	}
	// Room again: the invite founds nothing new and goes through.
	expectResult(t, partyOp(t, nk, "invite", lead, extra, ""), partyResultNone)

	expectResult(t, partyOp(t, nk, "promote", b, lead, ""), partyNotLeader)
	expectResult(t, partyOp(t, nk, "promote", lead, c, ""), partyTargetNotInParty)
	promoted := partyOp(t, nk, "promote", lead, b, "")
	if promoted.Parties[partyID].LeaderID != b {
		t.Fatalf("promote: %+v", promoted.Parties[partyID])
	}

	// Logout (leaving the hub server): out of the party, invites gone, never refused.
	partyOp(t, nk, "invite", b, c, "")
	loggedOut := partyOp(t, nk, "logout", c, "", "")
	expectResult(t, loggedOut, partyResultNone)
	if len(loggedOut.Players[c].Invites) != 0 {
		t.Fatalf("logout kept invites: %+v", loggedOut.Players[c])
	}
	expectResult(t, partyOp(t, nk, "logout", lead, "", ""), partyResultNone)
	if got := partyGet(t, nk, b); got.Parties[partyID].LeaderID != b || len(got.Parties[partyID].Members) != partyMaxMembers-2 {
		t.Fatalf("after the old leader logged out: %+v", got.Parties[partyID])
	}
}

// Members on no game server (save lease released or expired) for the grace period leave.
func TestPartyDropsMembersOffline(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	lead, b, c := uid(1), uid(2), uid(3)
	nk := newFakeNakama(lead, b, c)
	partyID := partyOp(t, nk, "invite", lead, b, "").PartyID
	partyOp(t, nk, "accept", b, "", partyID)
	partyOp(t, nk, "invite", lead, c, "")
	partyOp(t, nk, "accept", c, "", partyID)
	// The leader's client quit during a raid: lease released at T. c's lease lapsed (server crash).
	nk.seedLease(lead, leaseRecord{ServerID: "raid-1", ReleasedAtMs: clock.UnixMilli(), ExpiresAtMs: clock.UnixMilli()})
	nk.seedLease(c, leaseRecord{ServerID: "raid-1", Token: "x", ExpiresAtMs: clock.Add(30 * time.Second).UnixMilli()})
	*clock = clock.Add(partyOfflineGraceDefault*time.Second - time.Second)
	if got := partyGet(t, nk, b); len(got.Parties[partyID].Members) != 3 {
		t.Fatalf("dropped before the grace ran out: %+v", got.Parties[partyID])
	}
	*clock = clock.Add(time.Second)
	got := partyGet(t, nk, b)
	if members := got.Parties[partyID].Members; len(members) != 2 || got.Parties[partyID].LeaderID != b {
		t.Fatalf("offline leader not dropped: %+v", got.Parties[partyID])
	}
	var membership membershipRecord
	nk.object(collectionPartyMembers, lead, keyPartyMembership, &membership)
	if membership.PartyID != "" {
		t.Fatalf("dropped member's membership kept: %+v", membership)
	}
	*clock = clock.Add(30 * time.Second)
	if got := partyGet(t, nk, b); len(got.Parties[partyID].Members) != 1 {
		t.Fatalf("lapsed lease not dropped: %+v", got.Parties[partyID])
	}
}

// A concurrent change between read and write: the operation runs again on the new state.
func TestPartyRetriesOnConcurrentChange(t *testing.T) {
	setClock(t, time.Unix(1_800_000_000, 0))
	lead, b, c := uid(1), uid(2), uid(3)
	nk := newFakeNakama(lead, b, c)
	partyID := partyOp(t, nk, "invite", lead, b, "").PartyID
	partyOp(t, nk, "accept", b, "", partyID)
	partyOp(t, nk, "invite", lead, c, "")
	// While c accepts, another hub makes b leave: c's accept is retried and both changes land.
	nk.beforeMultiUpdate = func() {
		partyOp(t, nk, "leave", b, "", "")
	}
	accepted := partyOp(t, nk, "accept", c, "", partyID)
	expectResult(t, accepted, partyResultNone)
	if members := accepted.Parties[partyID].Members; len(members) != 2 || members[0] != lead || members[1] != c {
		t.Fatalf("retried accept: %+v", accepted.Parties[partyID])
	}
}

func TestPartyValidation(t *testing.T) {
	nk := newFakeNakama(uid(1))
	for name, payload := range map[string]string{
		"bad actor":   `{"op": "leave", "actor_id": "alice"}`,
		"bad target":  `{"op": "invite", "actor_id": "` + uid(1) + `", "target_id": "bob"}`,
		"bad party":   `{"op": "accept", "actor_id": "` + uid(1) + `", "party_id": "a b"}`,
		"unknown op":  `{"op": "dance", "actor_id": "` + uid(1) + `"}`,
		"bad server":  `{"op": "leave", "actor_id": "` + uid(1) + `", "server_id": "a/b"}`,
		"not json":    `{`,
		"no party id": `{"op": "decline", "actor_id": "` + uid(1) + `"}`,
	} {
		if _, err := RpcPartyFn(serverCtx(), &fakeLogger{}, nil, nk, payload); errorCode(err) != codeInvalidArgument {
			t.Errorf("%s: want INVALID_ARGUMENT, got %v", name, err)
		}
	}
	if _, err := RpcPartyFn(clientCtx(uid(1)), &fakeLogger{}, nil, nk, `{"op": "leave", "actor_id": "`+uid(1)+`"}`); errorCode(err) != codePermissionDenied {
		t.Errorf("client call: %v", err)
	}
	if _, err := RpcPartyGetFn(clientCtx(uid(1)), &fakeLogger{}, nil, nk, `{"user_ids": []}`); errorCode(err) != codePermissionDenied {
		t.Errorf("client get: %v", err)
	}
	many := make([]string, maxPartyStateUserIDs+1)
	for i := range many {
		many[i] = uid(i)
	}
	payload, _ := json.Marshal(map[string]any{"user_ids": many})
	if _, err := RpcPartyGetFn(serverCtx(), &fakeLogger{}, nil, nk, string(payload)); errorCode(err) != codeInvalidArgument {
		t.Errorf("too many users: %v", err)
	}
	if nk.writes != 0 {
		t.Fatal("a refused call wrote")
	}
}
