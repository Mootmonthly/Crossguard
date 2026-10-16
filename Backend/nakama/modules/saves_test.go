package main

import (
	"encoding/json"
	"strings"
	"testing"
)

const (
	alice = "11111111-2222-3333-4444-555555555555"
	bob   = "66666666-7777-8888-9999-aaaaaaaaaaaa"
)

func record(playerID string, gold int) string {
	return `{"version": 1, "playerId": "` + playerID + `", "gold": ` + itoa(gold) + `, "stash": {"tabs": []}}`
}

func itoa(n int) string {
	out, _ := json.Marshal(n)
	return string(out)
}

func writePayload(userID, characterID, rec, expected string) string {
	return `{"user_id": "` + userID + `", "character_id": "` + characterID + `", "record": ` + rec + `, "expected_version": "` + expected + `"}`
}

func load(t *testing.T, nk *fakeNakama, userID, characterID string) saveLoadResponse {
	t.Helper()
	out, err := RpcSaveLoadFn(serverCtx(), &fakeLogger{}, nil, nk, `{"user_id": "`+userID+`", "character_id": "`+characterID+`"}`)
	if err != nil {
		t.Fatalf("load: %v", err)
	}
	var response saveLoadResponse
	if err := json.Unmarshal([]byte(out), &response); err != nil {
		t.Fatalf("load response %q: %v", out, err)
	}
	return response
}

func write(t *testing.T, nk *fakeNakama, userID, rec, expected string) string {
	t.Helper()
	out, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, writePayload(userID, "main", rec, expected))
	if err != nil {
		t.Fatalf("write: %v", err)
	}
	var response saveWriteResponse
	if err := json.Unmarshal([]byte(out), &response); err != nil || response.Version == "" {
		t.Fatalf("write response %q: %v", out, err)
	}
	return response.Version
}

func TestLoadNewCharacter(t *testing.T) {
	nk := newFakeNakama(alice)
	response := load(t, nk, alice, "main")
	if response.Found || response.Version != "*" || string(response.Record) != "null" {
		t.Fatalf("new character: %+v", response)
	}
}

func TestWriteThenLoad(t *testing.T) {
	nk := newFakeNakama(alice)
	version := write(t, nk, alice, record(alice, 10), "*")
	response := load(t, nk, alice, "main")
	if !response.Found || response.Version != version {
		t.Fatalf("after write: %+v (want version %s)", response, version)
	}
	var stored map[string]any
	if err := json.Unmarshal(response.Record, &stored); err != nil || stored["gold"].(float64) != 10 {
		t.Fatalf("record back: %s", response.Record)
	}
	object := nk.storage[storageKey(collectionCharacters, alice, "main")]
	if object.permissionRead != permissionOwnerRead || object.permissionWrite != permissionNoWrite {
		t.Fatalf("permissions: read %d write %d", object.permissionRead, object.permissionWrite)
	}
	// The next write with the version just returned succeeds and moves the version on.
	next := write(t, nk, alice, record(alice, 20), version)
	if next == version {
		t.Fatal("version did not change")
	}
}

func TestStaleVersionIsRefusedAndKept(t *testing.T) {
	nk := newFakeNakama(alice)
	first := write(t, nk, alice, record(alice, 10), "*")
	// Server A and server B both loaded "first"; A writes, then B's write must fail.
	write(t, nk, alice, record(alice, 20), first)
	logger := &fakeLogger{}
	_, err := RpcSaveWriteFn(serverCtx(), logger, nil, nk, writePayload(alice, "main", record(alice, 30), first))
	if errorCode(err) != codeAborted {
		t.Fatalf("stale write: want ABORTED, got %v", err)
	}
	if got := load(t, nk, alice, "main"); !strings.Contains(string(got.Record), "20") {
		t.Fatalf("stale write replaced the save: %s", got.Record)
	}
	conflicts := nk.objectsIn(collectionConflicts)
	if len(conflicts) != 1 || !strings.Contains(conflicts[0].value, `"gold":30`) || conflicts[0].permissionRead != permissionNoRead {
		t.Fatalf("refused record not kept: %+v", conflicts)
	}
}

func TestNewCharacterWriteRefusedWhenOneExists(t *testing.T) {
	nk := newFakeNakama(alice)
	write(t, nk, alice, record(alice, 10), "*")
	_, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, writePayload(alice, "main", record(alice, 0), "*"))
	if errorCode(err) != codeAborted {
		t.Fatalf("second fresh character: want ABORTED, got %v", err)
	}
}

func TestClientsCannotCallSaveRpcs(t *testing.T) {
	nk := newFakeNakama(alice)
	ctx := clientCtx(alice)
	if _, err := RpcSaveLoadFn(ctx, &fakeLogger{}, nil, nk, `{"user_id": "`+alice+`", "character_id": "main"}`); errorCode(err) != codePermissionDenied {
		t.Fatalf("client load: %v", err)
	}
	if _, err := RpcSaveWriteFn(ctx, &fakeLogger{}, nil, nk, writePayload(alice, "main", record(alice, 1), "*")); errorCode(err) != codePermissionDenied {
		t.Fatalf("client write: %v", err)
	}
	if _, err := RpcSaveQuarantineFn(ctx, &fakeLogger{}, nil, nk, `{"user_id": "`+alice+`", "character_id": "main"}`); errorCode(err) != codePermissionDenied {
		t.Fatalf("client quarantine: %v", err)
	}
	if nk.writes != 0 {
		t.Fatal("a client call wrote")
	}
}

func TestWriteValidation(t *testing.T) {
	nk := newFakeNakama(alice)
	cases := map[string]string{
		"not json":             `{"user_id": `,
		"bad user id":          writePayload("alice", "main", record("alice", 1), "*"),
		"bad character id":     writePayload(alice, "../main", record(alice, 1), "*"),
		"empty character id":   writePayload(alice, "", record(alice, 1), "*"),
		"no expected version":  writePayload(alice, "main", record(alice, 1), ""),
		"record is a string":   writePayload(alice, "main", `"{}"`, "*"),
		"record is an array":   writePayload(alice, "main", `[1, 2]`, "*"),
		"record no version":    writePayload(alice, "main", `{"playerId": "`+alice+`"}`, "*"),
		"record float version": writePayload(alice, "main", `{"version": 1.5, "playerId": "`+alice+`"}`, "*"),
		"record other player":  writePayload(alice, "main", record(bob, 1), "*"),
		"record no player":     writePayload(alice, "main", `{"version": 1}`, "*"),
	}
	for name, payload := range cases {
		if _, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, payload); errorCode(err) != codeInvalidArgument {
			t.Errorf("%s: want INVALID_ARGUMENT, got %v", name, err)
		}
	}
	// Unknown user: NOT_FOUND.
	if _, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, writePayload(bob, "main", record(bob, 1), "*")); errorCode(err) != codeNotFound {
		t.Errorf("unknown user: %v", err)
	}
	if nk.writes != 0 {
		t.Fatal("an invalid write was stored")
	}
}

func TestWriteSizeLimit(t *testing.T) {
	nk := newFakeNakama(alice)
	padding := strings.Repeat("x", MaxRecordBytes)
	huge := `{"version": 1, "playerId": "` + alice + `", "pad": "` + padding + `"}`
	if _, err := RpcSaveWriteFn(serverCtx(), &fakeLogger{}, nil, nk, writePayload(alice, "main", huge, "*")); errorCode(err) != codeInvalidArgument {
		t.Fatalf("oversized record: %v", err)
	}
	// Just under the cap is fine.
	fits := `{"version": 1, "playerId": "` + alice + `", "pad": "` + padding[:MaxRecordBytes-200] + `"}`
	write(t, nk, alice, fits, "*")
}

func TestStorageFailureIsInternal(t *testing.T) {
	nk := newFakeNakama(alice)
	nk.failRead = true
	if _, err := RpcSaveLoadFn(serverCtx(), &fakeLogger{}, nil, nk, `{"user_id": "`+alice+`", "character_id": "main"}`); errorCode(err) != codeInternal {
		t.Fatalf("load with the database down: %v", err)
	}
}

func TestQuarantineCopiesAndKeepsOriginal(t *testing.T) {
	nk := newFakeNakama(alice)
	// Nothing stored: nothing to quarantine.
	out, err := RpcSaveQuarantineFn(serverCtx(), &fakeLogger{}, nil, nk, `{"user_id": "`+alice+`", "character_id": "main", "reason": "x"}`)
	if err != nil || !strings.Contains(out, `"quarantined":false`) {
		t.Fatalf("empty quarantine: %s %v", out, err)
	}
	write(t, nk, alice, record(alice, 77), "*")
	out, err = RpcSaveQuarantineFn(serverCtx(), &fakeLogger{}, nil, nk,
		`{"user_id": "`+alice+`", "character_id": "main", "reason": "`+strings.Repeat("r", 2000)+`"}`)
	if err != nil || !strings.Contains(out, `"quarantined":true`) {
		t.Fatalf("quarantine: %s %v", out, err)
	}
	aside := nk.objectsIn(collectionQuarantine)
	if len(aside) != 1 || !strings.Contains(aside[0].value, `"gold":77`) || aside[0].permissionRead != permissionNoRead {
		t.Fatalf("quarantined copy: %+v", aside)
	}
	var kept struct {
		Reason string `json:"reason"`
	}
	_ = json.Unmarshal([]byte(aside[0].value), &kept)
	if len(kept.Reason) != maxReasonLength {
		t.Fatalf("reason not capped: %d", len(kept.Reason))
	}
	if !load(t, nk, alice, "main").Found {
		t.Fatal("quarantine removed the original")
	}
}
