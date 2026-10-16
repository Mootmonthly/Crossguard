package main

import (
	"context"
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"strings"

	"github.com/heroiclabs/nakama-common/runtime"
)

// Save leases (docs/persistence.md, "Nakama backend"; docs/anti-cheat.md, "Save leases").
//
// One game server at a time owns a player's save: the one holding the player's lease. A lease is the
// storage object save_leases/<character id> owned by the player (server-only: read 0, write 0) with the
// holder's server id, a random token and an expiry. crossguard_save_write needs the current token, and
// the write and the lease check are one transaction (MultiUpdate), so a server whose lease ran out or was
// taken cannot write any more, even if its write was already on its way.
//
// Hand-off (hub -> raid -> hub): the server a player leaves writes the last save, then releases; the
// server they join acquires on login (retrying while the old one still holds it). An expired lease can be
// taken by anyone (a crashed server). A released lease stays stored with an empty token and the release
// time: "offline since", used by the party rules (parties.go) to drop members who are on no server.

const (
	collectionLeases = "save_leases"

	// TTL bounds and default (placeholders): the holder renews well before expiry (the game server renews
	// every third of the TTL).
	leaseDefaultTTLSec = 60
	leaseMinTTLSec     = 10
	leaseMaxTTLSec     = 600

	maxServerIDLength = 128
)

var serverIDPattern = characterIDPatternLike(maxServerIDLength)

var (
	errBadServerID  = runtime.NewError(fmt.Sprintf("server_id must be 1-%d letters, digits, '.', '-', '_' or ':'", maxServerIDLength), codeInvalidArgument)
	errNoLeaseToken = runtime.NewError("lease_token is required (crossguard_lease_acquire)", codeInvalidArgument)
	// The write's lease is not the player's current one: expired and taken, or released. FAILED_PRECONDITION
	// (HTTP 400): the server must stop writing this player (retrying cannot help).
	errLeaseLost = runtime.NewError("save lease not held: it expired, was released or another server took it", codeFailedPrecondition)
)

// leaseRecord is the stored value.
type leaseRecord struct {
	ServerID     string `json:"server_id"`
	Token        string `json:"token"`
	AcquiredAtMs int64  `json:"acquired_at_ms"`
	ExpiresAtMs  int64  `json:"expires_at_ms"`
	// Set when released (the token is empty then).
	ReleasedAtMs int64 `json:"released_at_ms,omitempty"`
}

// Held: a live lease (a token, not expired).
func (lease leaseRecord) heldAt(nowMs int64) bool {
	return lease.Token != "" && nowMs < lease.ExpiresAtMs
}

// offlineSinceMs: when the player stopped being on any server (0: they are on one now).
func (lease leaseRecord) offlineSinceMs(nowMs int64) int64 {
	if lease.heldAt(nowMs) {
		return 0
	}
	if lease.Token == "" && lease.ReleasedAtMs > 0 {
		return lease.ReleasedAtMs
	}
	return lease.ExpiresAtMs
}

func nowMs() int64 { return now().UnixMilli() }

func newToken() string {
	var bytes [16]byte
	if _, err := rand.Read(bytes[:]); err != nil {
		// crypto/rand does not fail on supported platforms.
		panic(err)
	}
	return hex.EncodeToString(bytes[:])
}

// characterIDPatternLike: server ids also allow '.' and ':' (host names, ports).
func characterIDPatternLike(maxLength int) func(string) bool {
	return func(value string) bool {
		if len(value) == 0 || len(value) > maxLength {
			return false
		}
		for _, char := range value {
			ok := (char >= 'a' && char <= 'z') || (char >= 'A' && char <= 'Z') || (char >= '0' && char <= '9') ||
				char == '-' || char == '_' || char == '.' || char == ':'
			if !ok {
				return false
			}
		}
		return true
	}
}

func clampTTL(ttl int) int {
	if ttl <= 0 {
		return leaseDefaultTTLSec
	}
	if ttl < leaseMinTTLSec {
		return leaseMinTTLSec
	}
	if ttl > leaseMaxTTLSec {
		return leaseMaxTTLSec
	}
	return ttl
}

// readLease returns the stored lease (zero value and version "" when none).
func readLease(ctx context.Context, nk runtime.NakamaModule, userID, characterID string) (leaseRecord, string, error) {
	objects, err := nk.StorageRead(ctx, []*runtime.StorageRead{{Collection: collectionLeases, Key: characterID, UserID: userID}})
	if err != nil {
		return leaseRecord{}, "", err
	}
	if len(objects) == 0 {
		return leaseRecord{}, "", nil
	}
	var lease leaseRecord
	if err := json.Unmarshal([]byte(objects[0].GetValue()), &lease); err != nil {
		// Unreadable: treat as free (it can only have been written by this module).
		return leaseRecord{}, objects[0].GetVersion(), nil
	}
	return lease, objects[0].GetVersion(), nil
}

func leaseWrite(userID, characterID string, lease leaseRecord, version string) *runtime.StorageWrite {
	value, _ := json.Marshal(lease)
	if version == "" {
		// None stored: only if there still is none.
		version = "*"
	}
	return &runtime.StorageWrite{
		Collection: collectionLeases, Key: characterID, UserID: userID, Value: string(value), Version: version,
		PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite,
	}
}

type leaseRequest struct {
	characterRef
	ServerID   string `json:"server_id"`
	LeaseToken string `json:"lease_token"`
	TTLSec     int    `json:"ttl_sec"`
}

func decodeLeaseRequest(payload string) (leaseRequest, error) {
	var request leaseRequest
	if err := decodePayload(payload, &request); err != nil {
		return request, err
	}
	if request.CharacterID == "" {
		request.CharacterID = defaultCharacterID
	}
	if err := request.validate(); err != nil {
		return request, err
	}
	if !serverIDPattern(request.ServerID) {
		return request, errBadServerID
	}
	return request, nil
}

// --- crossguard_lease_acquire --------------------------------------------------------------------

type leaseAcquireResponse struct {
	LeaseToken  string `json:"lease_token"`
	ExpiresAtMs int64  `json:"expires_at_ms"`
	// The save's storage version ("*": no save yet), for the load that follows.
	SaveVersion string `json:"save_version"`
	// An expired lease of another server was taken over (for logs).
	TookOverFrom string `json:"took_over_from,omitempty"`
}

// RpcLeaseAcquireFn: {"user_id", "character_id" ("main"), "server_id", "ttl_sec"} -> {"lease_token",
// "expires_at_ms", "save_version"}. Held by another server and not expired: ABORTED (HTTP 409), retry
// later. The same server asking again keeps its token (an in-flight write stays valid) with a new expiry.
func RpcLeaseAcquireFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	request, err := decodeLeaseRequest(payload)
	if err != nil {
		return "", err
	}
	if err := requireUser(ctx, logger, nk, request.UserID); err != nil {
		return "", err
	}
	ttlMs := int64(clampTTL(request.TTLSec)) * 1000
	objects, err := nk.StorageRead(ctx, []*runtime.StorageRead{
		{Collection: collectionLeases, Key: request.CharacterID, UserID: request.UserID},
		{Collection: collectionCharacters, Key: request.CharacterID, UserID: request.UserID},
	})
	if err != nil {
		logger.Error("lease acquire: read failed for %s: %v", request.UserID, err)
		return "", errStorage
	}
	var current leaseRecord
	leaseVersion := ""
	saveVersion := "*"
	for _, object := range objects {
		switch object.GetCollection() {
		case collectionLeases:
			leaseVersion = object.GetVersion()
			_ = json.Unmarshal([]byte(object.GetValue()), &current)
		case collectionCharacters:
			saveVersion = object.GetVersion()
		}
	}
	at := nowMs()
	response := leaseAcquireResponse{SaveVersion: saveVersion}
	next := leaseRecord{ServerID: request.ServerID, Token: newToken(), AcquiredAtMs: at, ExpiresAtMs: at + ttlMs}
	switch {
	case current.heldAt(at) && current.ServerID != request.ServerID:
		return "", runtime.NewError(fmt.Sprintf("save lease held by server %s for %d s more", current.ServerID, (current.ExpiresAtMs-at+999)/1000), codeAborted)
	case current.heldAt(at):
		// Ours already: same token, later expiry.
		next.Token = current.Token
		next.AcquiredAtMs = current.AcquiredAtMs
	case current.Token != "" && current.ServerID != request.ServerID:
		response.TookOverFrom = current.ServerID
	}
	if _, err := nk.StorageWrite(ctx, []*runtime.StorageWrite{leaseWrite(request.UserID, request.CharacterID, next, leaseVersion)}); err != nil {
		if isVersionConflict(err) {
			// Another server acquired between our read and write.
			return "", runtime.NewError("save lease changed while acquiring; try again", codeAborted)
		}
		logger.Error("lease acquire: write failed for %s: %v", request.UserID, err)
		return "", errStorage
	}
	if response.TookOverFrom != "" {
		logger.Warn("save lease of %s/%s taken over by %s (expired lease of %s)", request.UserID, request.CharacterID, request.ServerID, response.TookOverFrom)
	}
	response.LeaseToken = next.Token
	response.ExpiresAtMs = next.ExpiresAtMs
	out, _ := json.Marshal(response)
	return string(out), nil
}

// --- crossguard_lease_renew ----------------------------------------------------------------------

type leaseRenewResponse struct {
	ExpiresAtMs int64 `json:"expires_at_ms"`
}

// RpcLeaseRenewFn: {"user_id", "character_id", "server_id", "lease_token", "ttl_sec"} -> {"expires_at_ms"}.
// The lease must still carry the token (an expired lease nobody took can be renewed); otherwise
// FAILED_PRECONDITION (lost).
func RpcLeaseRenewFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	request, err := decodeLeaseRequest(payload)
	if err != nil {
		return "", err
	}
	if request.LeaseToken == "" {
		return "", errNoLeaseToken
	}
	current, version, err := readLease(ctx, nk, request.UserID, request.CharacterID)
	if err != nil {
		logger.Error("lease renew: read failed for %s: %v", request.UserID, err)
		return "", errStorage
	}
	if current.Token != request.LeaseToken || current.ServerID != request.ServerID {
		return "", errLeaseLost
	}
	current.ExpiresAtMs = nowMs() + int64(clampTTL(request.TTLSec))*1000
	if _, err := nk.StorageWrite(ctx, []*runtime.StorageWrite{leaseWrite(request.UserID, request.CharacterID, current, version)}); err != nil {
		if isVersionConflict(err) {
			return "", errLeaseLost
		}
		logger.Error("lease renew: write failed for %s: %v", request.UserID, err)
		return "", errStorage
	}
	out, _ := json.Marshal(leaseRenewResponse{ExpiresAtMs: current.ExpiresAtMs})
	return string(out), nil
}

// --- crossguard_lease_release --------------------------------------------------------------------

type leaseReleaseResponse struct {
	Released bool `json:"released"`
}

// RpcLeaseReleaseFn: {"user_id", "character_id", "server_id", "lease_token"} -> {"released"}. Not the
// holder any more: {"released": false} (nothing to do). The object stays with an empty token and the
// release time.
func RpcLeaseReleaseFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	request, err := decodeLeaseRequest(payload)
	if err != nil {
		return "", err
	}
	if request.LeaseToken == "" {
		return "", errNoLeaseToken
	}
	current, version, err := readLease(ctx, nk, request.UserID, request.CharacterID)
	if err != nil {
		logger.Error("lease release: read failed for %s: %v", request.UserID, err)
		return "", errStorage
	}
	released := false
	if current.Token == request.LeaseToken && current.ServerID == request.ServerID {
		at := nowMs()
		current.Token = ""
		current.ReleasedAtMs = at
		if current.ExpiresAtMs > at {
			current.ExpiresAtMs = at
		}
		if _, err := nk.StorageWrite(ctx, []*runtime.StorageWrite{leaseWrite(request.UserID, request.CharacterID, current, version)}); err != nil {
			if !isVersionConflict(err) {
				logger.Error("lease release: write failed for %s: %v", request.UserID, err)
				return "", errStorage
			}
			// Changed meanwhile (taken over): not ours to release.
		} else {
			released = true
		}
	}
	out, _ := json.Marshal(leaseReleaseResponse{Released: released})
	return string(out), nil
}

// --- Used by crossguard_save_write ---------------------------------------------------------------

// leaseGuard checks the token against the stored lease and returns the write that re-asserts it (same
// value and version) for the save's transaction: if another server changes the lease before the save
// lands, the whole write is refused.
func leaseGuard(ctx context.Context, nk runtime.NakamaModule, userID, characterID, token string) (*runtime.StorageWrite, error) {
	current, version, err := readLease(ctx, nk, userID, characterID)
	if err != nil {
		return nil, errStorage
	}
	if version == "" || current.Token == "" || current.Token != token || !current.heldAt(nowMs()) {
		return nil, errLeaseLost
	}
	return leaseWrite(userID, characterID, current, version), nil
}

// leaseStillHeld: after a refused transaction, whether the lease part was the cause.
func leaseStillHeld(ctx context.Context, nk runtime.NakamaModule, userID, characterID, token string, guard *runtime.StorageWrite) bool {
	current, version, err := readLease(ctx, nk, userID, characterID)
	return err == nil && version == guard.Version && current.Token == token
}

// readLeases: user id -> lease (for the party rules' "on a server" checks). Missing: zero value.
func readLeases(ctx context.Context, nk runtime.NakamaModule, userIDs []string) (map[string]leaseRecord, error) {
	leases := map[string]leaseRecord{}
	if len(userIDs) == 0 {
		return leases, nil
	}
	reads := make([]*runtime.StorageRead, 0, len(userIDs))
	for _, id := range userIDs {
		reads = append(reads, &runtime.StorageRead{Collection: collectionLeases, Key: defaultCharacterID, UserID: id})
	}
	objects, err := nk.StorageRead(ctx, reads)
	if err != nil {
		return nil, err
	}
	for _, object := range objects {
		var lease leaseRecord
		if json.Unmarshal([]byte(object.GetValue()), &lease) == nil {
			leases[object.GetUserId()] = lease
		}
	}
	return leases, nil
}

// isRejected: a version-checked write or delete in a transaction was refused.
func isRejected(err error) bool {
	if err == nil {
		return false
	}
	return isVersionConflict(err) || strings.Contains(strings.ToLower(err.Error()), "rejected")
}
