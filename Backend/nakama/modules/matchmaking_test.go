package main

import (
	"encoding/json"
	"strings"
	"testing"
	"time"
)

type testCluster struct {
	t  *testing.T
	c  *cluster
	nk *fakeNakama
}

func newTestCluster(t *testing.T, users ...string) *testCluster {
	return &testCluster{t: t, c: newCluster(defaultClusterConfig()), nk: newFakeNakama(users...)}
}

func (tc *testCluster) call(fn rpcFn, payload any) (string, error) {
	tc.t.Helper()
	body, _ := json.Marshal(payload)
	return fn(serverCtx(), &fakeLogger{}, nil, tc.nk, string(body))
}

func (tc *testCluster) mustCall(fn rpcFn, payload any, out any) {
	tc.t.Helper()
	text, err := tc.call(fn, payload)
	if err != nil {
		tc.t.Fatalf("call %v: %v", payload, err)
	}
	if out != nil {
		if err := json.Unmarshal([]byte(text), out); err != nil {
			tc.t.Fatalf("answer %q: %v", text, err)
		}
	}
}

func (tc *testCluster) register(id, kind, address string, capacity int) {
	tc.t.Helper()
	tc.mustCall(tc.c.RpcServerRegister, map[string]any{"server_id": id, "kind": kind, "address": address, "capacity": capacity}, nil)
}

func (tc *testCluster) request(partyID string, users ...string) matchStatusResponse {
	tc.t.Helper()
	var status matchStatusResponse
	tc.mustCall(tc.c.RpcMatchRequest, map[string]any{"party_id": partyID, "user_ids": users, "server_id": "hub-1"}, &status)
	return status
}

func (tc *testCluster) status(requestID string) matchStatusResponse {
	tc.t.Helper()
	var status matchStatusResponse
	tc.mustCall(tc.c.RpcMatchStatus, map[string]any{"request_id": requestID}, &status)
	return status
}

// party founds a party of the given members (leader first).
func (tc *testCluster) party(members ...string) string {
	tc.t.Helper()
	partyID := ""
	for _, member := range members[1:] {
		partyID = partyOp(tc.t, tc.nk, "invite", members[0], member, "").PartyID
		expectResult(tc.t, partyOp(tc.t, tc.nk, "accept", member, "", partyID), partyResultNone)
	}
	return partyID
}

func (tc *testCluster) verify(ticket, serverID, userID string) (ticketVerifyResponse, error) {
	var response ticketVerifyResponse
	text, err := tc.call(tc.c.RpcTicketVerify, map[string]any{"ticket": ticket, "server_id": serverID, "user_id": userID})
	if err == nil {
		_ = json.Unmarshal([]byte(text), &response)
	}
	return response, err
}

func TestTwoPartiesShareOneMatchServerWithTickets(t *testing.T) {
	setClock(t, time.Unix(1_800_000_000, 0))
	a1, a2, b1, b2, b3 := uid(1), uid(2), uid(3), uid(4), uid(5)
	tc := newTestCluster(t, a1, a2, b1, b2, b3)
	tc.register("match-1", serverKindMatch, "127.0.0.1:7778", 0)
	partyA := tc.party(a1, a2)
	partyB := tc.party(b1, b2, b3)

	first := tc.request(partyA, a1, a2)
	if first.State != matchStateQueued {
		t.Fatalf("one team (MinTeams 2): %+v", first)
	}
	second := tc.request(partyB, b1, b2, b3)
	if second.State != matchStateAssigned || second.Address != "127.0.0.1:7778" || second.ServerID != "match-1" || len(second.Tickets) != 3 {
		t.Fatalf("second team: %+v", second)
	}
	first = tc.status(first.RequestID)
	if first.State != matchStateAssigned || first.MatchID != second.MatchID || first.Address != second.Address || len(first.Tickets) != 2 {
		t.Fatalf("both teams in one match: %+v / %+v", first, second)
	}
	// Each ticket names its player and party; tickets are unique.
	seen := map[string]bool{}
	for user, ticket := range first.Tickets {
		got, err := tc.verify(ticket, "match-1", user)
		if err != nil || got.UserID != user || got.PartyID != partyA || got.MatchID != first.MatchID {
			t.Fatalf("verify %s: %+v %v", user, got, err)
		}
		seen[ticket] = true
	}
	for user, ticket := range second.Tickets {
		if got, err := tc.verify(ticket, "match-1", user); err != nil || got.PartyID != partyB {
			t.Fatalf("verify %s: %+v %v", user, got, err)
		}
		seen[ticket] = true
	}
	if len(seen) != 5 {
		t.Fatal("tickets are not unique")
	}
	// The match server is taken: a third team waits.
	c1, c2 := uid(6), uid(7)
	tc.nk.addUser(c1)
	tc.nk.addUser(c2)
	if got := tc.request("", c1); got.State != matchStateQueued {
		t.Fatalf("no free server: %+v", got)
	}
	// The party remembers the hub it left from (the hub picker prefers it).
	if party, _, _ := readPartyRecord(serverCtx(), tc.nk, partyA); party.HubServerID != "hub-1" {
		t.Fatalf("party hub: %+v", party)
	}
}

func TestTicketChecks(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	a, b := uid(1), uid(2)
	tc := newTestCluster(t, a, b)
	tc.register("match-1", serverKindMatch, "127.0.0.1:7778", 0)
	tc.register("match-2", serverKindMatch, "127.0.0.1:7779", 0)
	assignedA := tc.request("", a)
	assignedB := tc.request("", b)
	if assignedB.State != matchStateAssigned {
		t.Fatalf("two solos: %+v", assignedB)
	}
	assignedA = tc.status(assignedA.RequestID)
	ticketA, ticketB := assignedA.Tickets[a], assignedB.Tickets[b]

	if _, err := tc.verify(ticketA, "match-2", a); errorCode(err) != codePermissionDenied {
		t.Fatalf("another server: %v", err)
	}
	if _, err := tc.verify(ticketA, "match-1", b); errorCode(err) != codePermissionDenied {
		t.Fatalf("another player's ticket: %v", err)
	}
	if _, err := tc.verify(strings.Repeat("0", 32), "match-1", a); errorCode(err) != codeUnauthenticated {
		t.Fatalf("unknown ticket: %v", err)
	}
	if _, err := tc.verify("", "match-1", a); errorCode(err) != codeInvalidArgument {
		t.Fatalf("no ticket: %v", err)
	}
	if _, err := tc.verify("a?b", "match-1", a); errorCode(err) != codeUnauthenticated {
		t.Fatalf("malformed ticket: %v", err)
	}
	// A's ticket used in time stays valid for reconnects; B's, unused, expires.
	if _, err := tc.verify(ticketA, "match-1", a); err != nil {
		t.Fatalf("first use: %v", err)
	}
	*clock = clock.Add(time.Duration(defaultClusterConfig().TicketTTLSec) * time.Second)
	if _, err := tc.verify(ticketB, "match-1", b); errorCode(err) != codeUnauthenticated {
		t.Fatalf("expired ticket: %v", err)
	}
	if _, err := tc.verify(ticketA, "match-1", a); err != nil {
		t.Fatalf("reconnect with a used ticket: %v", err)
	}
	*clock = clock.Add(time.Duration(defaultClusterConfig().TicketUseSec) * time.Second)
	if _, err := tc.verify(ticketA, "match-1", a); errorCode(err) != codeUnauthenticated {
		t.Fatalf("used ticket past the raid: %v", err)
	}
	if _, err := RpcTicketVerifyFn(clientCtx(a), &fakeLogger{}, nil, tc.nk, `{"ticket": "`+ticketA+`"}`, 10); errorCode(err) != codePermissionDenied {
		t.Fatalf("client verify: %v", err)
	}
}

func TestQueueRulesWaitAndCapacity(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	var users []string
	for i := 1; i <= 12; i++ {
		users = append(users, uid(i))
	}
	tc := newTestCluster(t, users...)
	// A small server: 6 players.
	tc.register("match-1", serverKindMatch, "127.0.0.1:7778", 6)
	solo := tc.request("", users[0])
	if solo.State != matchStateQueued {
		t.Fatalf("alone: %+v", solo)
	}
	// Kept alive by polling; after MaxWaitSec it starts alone.
	*clock = clock.Add(time.Duration(defaultClusterConfig().MaxWaitSec-1) * time.Second)
	if got := tc.status(solo.RequestID); got.State != matchStateQueued {
		t.Fatalf("before the wait: %+v", got)
	}
	*clock = clock.Add(time.Second)
	if got := tc.status(solo.RequestID); got.State != matchStateAssigned || len(got.Tickets) != 1 {
		t.Fatalf("after the wait: %+v", got)
	}
	tc.mustCall(tc.c.RpcMatchOver, map[string]any{"server_id": "match-1"}, nil)

	// Fill by party size, first come first served: 4 + 3 do not fit in 6; 4 + 2 do.
	big := tc.party(users[1:5]...)
	mid := tc.party(users[5:8]...)
	small := tc.party(users[8:10]...)
	reqBig := tc.request(big, users[1:5]...)
	reqMid := tc.request(mid, users[5:8]...)
	if got := tc.status(reqMid.RequestID); got.State != matchStateQueued {
		t.Fatalf("the 3 do not fit with the 4: %+v", got)
	}
	if got := tc.status(reqBig.RequestID); got.State != matchStateAssigned {
		t.Fatalf("the 4 go: %+v", got)
	}
	tc.mustCall(tc.c.RpcMatchOver, map[string]any{"server_id": "match-1"}, nil)
	reqSmall := tc.request(small, users[8:10]...)
	if got := tc.status(reqSmall.RequestID); got.State != matchStateAssigned {
		t.Fatalf("3 + 2 fit in 6: %+v", got)
	}
	if got := tc.status(reqMid.RequestID); got.State != matchStateAssigned || got.MatchID != tc.status(reqSmall.RequestID).MatchID {
		t.Fatalf("3 + 2 share the match: %+v", got)
	}
}

func TestQueueRefusalsCancelAndAbandon(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	a, b, c := uid(1), uid(2), uid(3)
	tc := newTestCluster(t, a, b, c)
	queued := tc.request("", a)
	if _, err := tc.call(tc.c.RpcMatchRequest, map[string]any{"user_ids": []string{a}}); errorCode(err) != codeAborted {
		t.Fatalf("queued twice: %v", err)
	}
	if _, err := tc.call(tc.c.RpcMatchRequest, map[string]any{"user_ids": []string{a, b}}); errorCode(err) != codeInvalidArgument {
		t.Fatalf("two players without a party: %v", err)
	}
	partyID := tc.party(b, c)
	if _, err := tc.call(tc.c.RpcMatchRequest, map[string]any{"party_id": partyID, "user_ids": []string{b, a}}); errorCode(err) != codeInvalidArgument {
		t.Fatalf("a player outside the party: %v", err)
	}
	if _, err := tc.call(tc.c.RpcMatchRequest, map[string]any{"party_id": partyID, "user_ids": []string{b, b}}); errorCode(err) != codeInvalidArgument {
		t.Fatalf("a player twice: %v", err)
	}
	if _, err := tc.call(tc.c.RpcMatchRequest, map[string]any{"user_ids": []string{uid(50)}}); errorCode(err) != codeNotFound {
		t.Fatalf("unknown player: %v", err)
	}
	if _, err := RpcPartyGetFn(clientCtx(a), &fakeLogger{}, nil, tc.nk, `{}`); errorCode(err) != codePermissionDenied {
		t.Fatalf("client: %v", err)
	}
	if _, err := tc.c.RpcMatchRequest(clientCtx(a), &fakeLogger{}, nil, tc.nk, `{"user_ids": ["`+a+`"]}`); errorCode(err) != codePermissionDenied {
		t.Fatalf("client request: %v", err)
	}
	// Cancel: out of the queue, may queue again.
	var cancel matchCancelResponse
	tc.mustCall(tc.c.RpcMatchCancel, map[string]any{"request_id": queued.RequestID}, &cancel)
	if !cancel.Cancelled || len(tc.c.queue.queueSnapshot()) != 0 {
		t.Fatalf("cancel: %+v", cancel)
	}
	again := tc.request("", a)
	// Nobody polls: dropped after AbandonSec; status then answers 404.
	*clock = clock.Add(time.Duration(defaultClusterConfig().AbandonSec+1) * time.Second)
	if _, err := tc.call(tc.c.RpcMatchStatus, map[string]any{"request_id": again.RequestID}); errorCode(err) != codeNotFound {
		t.Fatalf("abandoned request: %v", err)
	}
}

func TestFleetRegistryAndHubPick(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	a, b := uid(1), uid(2)
	tc := newTestCluster(t, a, b)
	if _, err := tc.call(tc.c.RpcHubFind, map[string]any{"user_id": a}); errorCode(err) != codeNotFound {
		t.Fatalf("no hub: %v", err)
	}
	tc.register("hub-1", serverKindHub, "127.0.0.1:7777", 2)
	tc.register("hub-2", serverKindHub, "127.0.0.1:7787", 10)
	var pick hubFindResponse
	tc.mustCall(tc.c.RpcHubFind, map[string]any{"user_id": a}, &pick)
	if pick.ServerID != "hub-2" {
		t.Fatalf("most room first: %+v", pick)
	}
	// A party prefers its hub (where it launched from or last changed), while it has room.
	partyID := tc.party(a, b)
	tc.mustCall(tc.c.RpcHubFind, map[string]any{"user_id": a, "party_id": partyID}, &pick)
	if pick.ServerID != "hub-1" {
		t.Fatalf("party's hub: %+v", pick)
	}
	tc.mustCall(tc.c.RpcHubFind, map[string]any{"user_id": b}, &pick)
	if pick.ServerID != "hub-1" {
		t.Fatalf("party's hub found from membership: %+v", pick)
	}
	// hub-1 (2 places) now has 2 pending: full until its heartbeat.
	tc.mustCall(tc.c.RpcHubFind, map[string]any{"party_id": partyID}, &pick)
	if pick.ServerID != "hub-2" {
		t.Fatalf("party's hub full: %+v", pick)
	}
	var beat heartbeatResponse
	tc.mustCall(tc.c.RpcServerHeartbeat, map[string]any{"server_id": "hub-1", "players": 0}, &beat)
	if !beat.Known {
		t.Fatal("registered hub unknown")
	}
	tc.mustCall(tc.c.RpcHubFind, map[string]any{"party_id": partyID}, &pick)
	if pick.ServerID != "hub-1" {
		t.Fatalf("after the heartbeat: %+v", pick)
	}
	// Clients find their own hub with their session.
	out, err := tc.c.RpcHubFindMine(clientCtx(a), &fakeLogger{}, nil, tc.nk, `{}`)
	if err != nil || !strings.Contains(out, "127.0.0.1:") {
		t.Fatalf("client hub find: %s %v", out, err)
	}
	if _, err := tc.c.RpcHubFindMine(serverCtx(), &fakeLogger{}, nil, tc.nk, `{}`); errorCode(err) != codeUnauthenticated {
		t.Fatalf("hub find mine without a session: %v", err)
	}
	if _, err := tc.c.RpcHubFind(clientCtx(a), &fakeLogger{}, nil, tc.nk, `{}`); errorCode(err) != codePermissionDenied {
		t.Fatalf("client hub find (server RPC): %v", err)
	}
	// Silent servers drop out after three heartbeats; a heartbeat then says "unknown".
	*clock = clock.Add(time.Duration(3*defaultClusterConfig().HeartbeatSec+1) * time.Second)
	if _, err := tc.call(tc.c.RpcHubFind, map[string]any{}); errorCode(err) != codeNotFound {
		t.Fatalf("expired hubs: %v", err)
	}
	tc.mustCall(tc.c.RpcServerHeartbeat, map[string]any{"server_id": "hub-1", "players": 0}, &beat)
	if beat.Known {
		t.Fatal("expired hub still known")
	}
	// Deregister.
	tc.register("hub-3", serverKindHub, "127.0.0.1:7797", 0)
	var ok okResponse
	tc.mustCall(tc.c.RpcServerDeregister, map[string]any{"server_id": "hub-3"}, &ok)
	if !ok.Ok {
		t.Fatal("deregister")
	}
	if _, err := tc.call(tc.c.RpcHubFind, map[string]any{}); errorCode(err) != codeNotFound {
		t.Fatalf("deregistered hub: %v", err)
	}
}

func TestRegistrationValidationAndDefaults(t *testing.T) {
	tc := newTestCluster(t)
	for name, payload := range map[string]map[string]any{
		"bad kind":      {"server_id": "s1", "kind": "raid", "address": "1.2.3.4:7777"},
		"bad id":        {"server_id": "s 1", "kind": "hub", "address": "1.2.3.4:7777"},
		"bad address":   {"server_id": "s1", "kind": "hub", "address": "1.2.3.4:7777?x"},
		"no address":    {"server_id": "s1", "kind": "hub"},
		"capacity":      {"server_id": "s1", "kind": "hub", "address": "h:1", "capacity": maxCapacity + 1},
		"negative cap":  {"server_id": "s1", "kind": "match", "address": "h:1", "capacity": -1},
		"slash address": {"server_id": "s1", "kind": "hub", "address": "h/1"},
	} {
		if _, err := tc.call(tc.c.RpcServerRegister, payload); errorCode(err) != codeInvalidArgument {
			t.Errorf("%s: want INVALID_ARGUMENT, got %v", name, err)
		}
	}
	var registered registerResponse
	tc.mustCall(tc.c.RpcServerRegister, map[string]any{"server_id": "m1", "kind": "match", "address": "[::1]:7778"}, &registered)
	if registered.Capacity != 48 || registered.HeartbeatSec != defaultClusterConfig().HeartbeatSec {
		t.Fatalf("match server default (48 players, owner): %+v", registered)
	}
	tc.mustCall(tc.c.RpcServerRegister, map[string]any{"server_id": "h1", "kind": "hub", "address": "hub.example.com:7777"}, &registered)
	if registered.Capacity != defaultClusterConfig().DefaultHubCap {
		t.Fatalf("hub default: %+v", registered)
	}
	if _, err := tc.c.RpcServerRegister(clientCtx(uid(1)), &fakeLogger{}, nil, tc.nk, `{"server_id": "x", "kind": "hub", "address": "h:1"}`); errorCode(err) != codePermissionDenied {
		t.Fatalf("client register: %v", err)
	}
}

func TestAllocatorFreesUnusedAndOverMatches(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	config := defaultClusterConfig()
	f := newFleet(config)
	allocator := &localFleetAllocator{fleet: f}
	at := clock.UnixMilli()
	if _, err := allocator.Allocate("m0", 1, at); err != errNoMatchServer {
		t.Fatalf("empty fleet: %v", err)
	}
	f.register("match-1", serverKindMatch, "h:1", 4, at)
	if _, err := allocator.Allocate("m1", 5, at); err != errNoMatchServer {
		t.Fatalf("too small: %v", err)
	}
	got, err := allocator.Allocate("m1", 4, at)
	if err != nil || got.ServerID != "match-1" || got.Capacity != 4 {
		t.Fatalf("allocate: %+v %v", got, err)
	}
	if _, err := allocator.Allocate("m2", 1, at); err != errNoMatchServer {
		t.Fatal("one match per server")
	}
	// Nobody came: free again once the allocation times out (heartbeat with no players).
	for waited := 0; waited <= config.AllocationTimeoutSec; waited += config.HeartbeatSec {
		*clock = clock.Add(time.Duration(config.HeartbeatSec) * time.Second)
		if !f.heartbeat("match-1", 0, clock.UnixMilli()) {
			t.Fatal("heartbeat: server unknown")
		}
	}
	if _, err := allocator.Allocate("m2", 1, clock.UnixMilli()); err != nil {
		t.Fatalf("after the allocation timed out: %v", err)
	}
	// match_over for another match does not free it; for this one it does.
	if f.matchOver("match-1", "m1") {
		t.Fatal("stale match id freed the server")
	}
	if !f.matchOver("match-1", "m2") {
		t.Fatal("match over")
	}
	allocator.Release("match-1", "")
	if _, err := allocator.Allocate("m3", 1, clock.UnixMilli()); err != nil {
		t.Fatalf("after match over: %v", err)
	}
}

// Ticket storage failing gives the server back and keeps the teams queued.
func TestTicketStoreFailureKeepsQueue(t *testing.T) {
	setClock(t, time.Unix(1_800_000_000, 0))
	a, b := uid(1), uid(2)
	tc := newTestCluster(t, a, b)
	tc.register("match-1", serverKindMatch, "h:1", 0)
	tc.request("", a)
	tc.nk.failWrite = true
	second := tc.request("", b)
	if second.State != matchStateQueued {
		t.Fatalf("tickets not stored, still assigned: %+v", second)
	}
	tc.nk.failWrite = false
	if got := tc.status(second.RequestID); got.State != matchStateAssigned {
		t.Fatalf("after storage recovered: %+v", got)
	}
}
