package main

import (
	"encoding/json"
	"strings"
	"testing"
	"time"
)

// setClock pins now() for the test (restored after).
func setClock(t *testing.T, at time.Time) *time.Time {
	t.Helper()
	clock := at
	previous := now
	now = func() time.Time { return clock }
	t.Cleanup(func() { now = previous })
	return &clock
}

func noLeases(nk *fakeNakama, userIDs ...string) *fakeNakama {
	for _, id := range userIDs {
		delete(nk.storage, storageKey(collectionLeases, id, defaultCharacterID))
	}
	return nk
}

func leasePayload(userID, serverID, token string, ttl int) string {
	out, _ := json.Marshal(map[string]any{"user_id": userID, "server_id": serverID, "lease_token": token, "ttl_sec": ttl})
	return string(out)
}

func acquire(t *testing.T, nk *fakeNakama, userID, serverID string, ttl int) (leaseAcquireResponse, error) {
	t.Helper()
	out, err := RpcLeaseAcquireFn(serverCtx(), &fakeLogger{}, nil, nk, leasePayload(userID, serverID, "", ttl))
	var response leaseAcquireResponse
	if err == nil {
		if decodeErr := json.Unmarshal([]byte(out), &response); decodeErr != nil {
			t.Fatalf("acquire answer %q: %v", out, decodeErr)
		}
	}
	return response, err
}

func mustAcquire(t *testing.T, nk *fakeNakama, userID, serverID string, ttl int) leaseAcquireResponse {
	t.Helper()
	response, err := acquire(t, nk, userID, serverID, ttl)
	if err != nil || response.LeaseToken == "" {
		t.Fatalf("acquire by %s: %+v %v", serverID, response, err)
	}
	return response
}

func TestLeaseAcquireHeldAndReacquire(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	nk := noLeases(newFakeNakama(alice), alice)
	hub := mustAcquire(t, nk, alice, "hub-1", 60)
	if hub.SaveVersion != "*" || hub.ExpiresAtMs != clock.Add(60*time.Second).UnixMilli() {
		t.Fatalf("first acquire: %+v", hub)
	}
	// Another server must wait while the lease is live.
	if _, err := acquire(t, nk, alice, "raid-1", 60); errorCode(err) != codeAborted || !strings.Contains(err.Error(), "hub-1") {
		t.Fatalf("held lease: want ABORTED naming the holder, got %v", err)
	}
	// The holder asking again keeps its token (an in-flight write stays valid), with a later expiry.
	*clock = clock.Add(30 * time.Second)
	again := mustAcquire(t, nk, alice, "hub-1", 60)
	if again.LeaseToken != hub.LeaseToken || again.ExpiresAtMs != clock.Add(60*time.Second).UnixMilli() {
		t.Fatalf("re-acquire: %+v (first %+v)", again, hub)
	}
}

func TestLeaseExpiredCanBeTaken(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	nk := noLeases(newFakeNakama(alice), alice)
	hub := mustAcquire(t, nk, alice, "hub-1", 10)
	*clock = clock.Add(11 * time.Second)
	raid := mustAcquire(t, nk, alice, "raid-1", 60)
	if raid.TookOverFrom != "hub-1" || raid.LeaseToken == hub.LeaseToken {
		t.Fatalf("take-over: %+v", raid)
	}
	// The old holder's token no longer renews or writes.
	if _, err := RpcLeaseRenewFn(serverCtx(), &fakeLogger{}, nil, nk, leasePayload(alice, "hub-1", hub.LeaseToken, 60)); errorCode(err) != codeFailedPrecondition {
		t.Fatalf("renew by the old holder: %v", err)
	}
	if _, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, writePayloadWithLease(alice, "main", record(alice, 5), "*", hub.LeaseToken)); errorCode(err) != codeFailedPrecondition {
		t.Fatalf("write by the old holder: %v", err)
	}
	if len(nk.objectsIn(collectionCharacters)) != 0 || len(nk.objectsIn(collectionConflicts)) != 0 {
		t.Fatal("a refused lease write stored something")
	}
	// The new holder writes.
	if _, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, writePayloadWithLease(alice, "main", record(alice, 5), "*", raid.LeaseToken)); err != nil {
		t.Fatalf("write by the new holder: %v", err)
	}
}

func TestLeaseTTLClamped(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	nk := noLeases(newFakeNakama(alice, bob), alice, bob)
	if got := mustAcquire(t, nk, alice, "hub-1", 1); got.ExpiresAtMs != clock.Add(leaseMinTTLSec*time.Second).UnixMilli() {
		t.Fatalf("short TTL not clamped: %+v", got)
	}
	if got := mustAcquire(t, nk, bob, "hub-1", 0); got.ExpiresAtMs != clock.Add(leaseDefaultTTLSec*time.Second).UnixMilli() {
		t.Fatalf("default TTL: %+v", got)
	}
}

func TestLeaseRenewAndRelease(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	nk := noLeases(newFakeNakama(alice), alice)
	hub := mustAcquire(t, nk, alice, "hub-1", 30)
	*clock = clock.Add(20 * time.Second)
	out, err := RpcLeaseRenewFn(serverCtx(), &fakeLogger{}, nil, nk, leasePayload(alice, "hub-1", hub.LeaseToken, 30))
	if err != nil || !strings.Contains(out, itoa(int(clock.Add(30*time.Second).UnixMilli()))) {
		t.Fatalf("renew: %s %v", out, err)
	}
	if _, err := RpcLeaseRenewFn(serverCtx(), &fakeLogger{}, nil, nk, leasePayload(alice, "hub-1", "wrong", 30)); errorCode(err) != codeFailedPrecondition {
		t.Fatalf("renew with a wrong token: %v", err)
	}
	if _, err := RpcLeaseRenewFn(serverCtx(), &fakeLogger{}, nil, nk, leasePayload(alice, "hub-1", "", 30)); errorCode(err) != codeInvalidArgument {
		t.Fatalf("renew without a token: %v", err)
	}
	// A wrong token releases nothing.
	out, err = RpcLeaseReleaseFn(serverCtx(), &fakeLogger{}, nil, nk, leasePayload(alice, "hub-1", "wrong", 0))
	if err != nil || out != `{"released":false}` {
		t.Fatalf("release with a wrong token: %s %v", out, err)
	}
	out, err = RpcLeaseReleaseFn(serverCtx(), &fakeLogger{}, nil, nk, leasePayload(alice, "hub-1", hub.LeaseToken, 0))
	if err != nil || out != `{"released":true}` {
		t.Fatalf("release: %s %v", out, err)
	}
	var stored leaseRecord
	if !nk.object(collectionLeases, alice, defaultCharacterID, &stored) || stored.Token != "" || stored.ReleasedAtMs != clock.UnixMilli() {
		t.Fatalf("released lease kept as offline-since: %+v", stored)
	}
	if stored.offlineSinceMs(clock.UnixMilli()) != clock.UnixMilli() {
		t.Fatalf("offline since: %d", stored.offlineSinceMs(clock.UnixMilli()))
	}
	// Released: the next server acquires at once, and the old token cannot write.
	raid := mustAcquire(t, nk, alice, "raid-1", 60)
	if raid.TookOverFrom != "" {
		t.Fatalf("a released lease is not a take-over: %+v", raid)
	}
	if _, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, writePayloadWithLease(alice, "main", record(alice, 1), "*", hub.LeaseToken)); errorCode(err) != codeFailedPrecondition {
		t.Fatalf("write after release: %v", err)
	}
}

func TestLeaseAcquireReportsSaveVersion(t *testing.T) {
	nk := newFakeNakama(alice)
	version := write(t, nk, alice, record(alice, 3), "*")
	noLeases(nk, alice)
	if got := mustAcquire(t, nk, alice, "raid-1", 60); got.SaveVersion != version {
		t.Fatalf("save version: %+v want %s", got, version)
	}
}

// The lease changes between the write's check and its transaction: the save must not land.
func TestSaveWriteRefusedWhenLeaseChangesDuringWrite(t *testing.T) {
	setClock(t, time.Unix(1_800_000_000, 0))
	nk := noLeases(newFakeNakama(alice), alice)
	hub := mustAcquire(t, nk, alice, "hub-1", 60)
	nk.beforeMultiUpdate = func() {
		nk.seedLease(alice, leaseRecord{ServerID: "raid-1", Token: "other", ExpiresAtMs: now().Add(time.Minute).UnixMilli()})
	}
	_, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, writePayloadWithLease(alice, "main", record(alice, 9), "*", hub.LeaseToken))
	if errorCode(err) != codeFailedPrecondition {
		t.Fatalf("write racing a take-over: %v", err)
	}
	if len(nk.objectsIn(collectionCharacters)) != 0 || len(nk.objectsIn(collectionConflicts)) != 0 {
		t.Fatal("the save landed (or was kept as a version conflict)")
	}
}

func TestLeaseValidationAndClients(t *testing.T) {
	nk := noLeases(newFakeNakama(alice), alice)
	for name, payload := range map[string]string{
		"bad user":        leasePayload("alice", "hub-1", "", 60),
		"no server":       leasePayload(alice, "", "", 60),
		"bad server":      leasePayload(alice, "hub 1", "", 60),
		"bad character":   `{"user_id": "` + alice + `", "character_id": "../x", "server_id": "hub-1"}`,
		"not json":        `{`,
		"long server id":  leasePayload(alice, strings.Repeat("s", maxServerIDLength+1), "", 60),
		"server with uri": leasePayload(alice, "hub/1", "", 60),
	} {
		if _, err := RpcLeaseAcquireFn(serverCtx(), &fakeLogger{}, nil, nk, payload); errorCode(err) != codeInvalidArgument {
			t.Errorf("%s: want INVALID_ARGUMENT, got %v", name, err)
		}
	}
	if _, err := RpcLeaseAcquireFn(serverCtx(), &fakeLogger{}, nil, nk, leasePayload(bob, "hub-1", "", 60)); errorCode(err) != codeNotFound {
		t.Errorf("unknown user: %v", err)
	}
	if _, err := RpcLeaseAcquireFn(clientCtx(alice), &fakeLogger{}, nil, nk, leasePayload(alice, "hub-1", "", 60)); errorCode(err) != codePermissionDenied {
		t.Errorf("client acquire: %v", err)
	}
	if _, err := RpcLeaseReleaseFn(clientCtx(alice), &fakeLogger{}, nil, nk, leasePayload(alice, "hub-1", "x", 60)); errorCode(err) != codePermissionDenied {
		t.Errorf("client release: %v", err)
	}
	if nk.writes != 0 {
		t.Fatal("a refused call wrote")
	}
}
