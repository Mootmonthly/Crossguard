package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"regexp"
	"strings"
	"time"

	"github.com/heroiclabs/nakama-common/runtime"
)

// Character saves (docs/persistence.md, "Nakama backend").
//
// One storage object per character: collection "characters", key = character id, owner = the player's
// Nakama user id; the value is the save document the game writes (CrossguardRules FCharacterSave JSON).
// Owner may read it (permission_read 1); only the server writes (permission_write 0, and client writes
// to the collection are refused in auth.go). The game server validates the document on load; this
// module only checks its rough shape so that garbage never lands in the database.
//
// Writes use storage object versions (optimistic concurrency): the caller passes the version it loaded
// ("*" when there was none), so two game servers can never silently overwrite each other's save. A
// write that loses the race is kept in "characters_conflicts" for support, never dropped.

const (
	collectionCharacters = "characters"
	collectionQuarantine = "characters_quarantine"
	collectionConflicts  = "characters_conflicts"

	// Storage permissions.
	permissionNoRead    = 0
	permissionOwnerRead = 1
	permissionNoWrite   = 0

	// Same cap as the game's loader (CrossguardRules FSaveLimits / UCrossguardLocalSaveBackend::MaxFileBytes).
	// Nakama's socket.max_request_size_bytes must be above it (Backend/nakama/local.yml).
	MaxRecordBytes = 8 * 1024 * 1024

	maxReasonLength = 512
)

var characterIDPattern = regexp.MustCompile(`^[A-Za-z0-9_-]{1,64}$`)
var uuidPattern = regexp.MustCompile(`^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$`)

var (
	errClientCall      = runtime.NewError("server-to-server only: call with the runtime HTTP key", codePermissionDenied)
	errBadPayload      = runtime.NewError("payload is not valid JSON", codeInvalidArgument)
	errBadUserID       = runtime.NewError("user_id must be a Nakama user id (UUID)", codeInvalidArgument)
	errBadCharacterID  = runtime.NewError("character_id must be 1-64 letters, digits, '-' or '_'", codeInvalidArgument)
	errUnknownUser     = runtime.NewError("no such user", codeNotFound)
	errNoVersion       = runtime.NewError("expected_version is required: the version loaded, or \"*\" for a new character", codeInvalidArgument)
	errRecordNotObject = runtime.NewError("record must be a JSON object", codeInvalidArgument)
	errRecordTooLarge  = runtime.NewError(fmt.Sprintf("record is larger than %d bytes", MaxRecordBytes), codeInvalidArgument)
	errStorage         = runtime.NewError("storage error", codeInternal)
)

// requireServerCall refuses RPCs made with a player's session: the save RPCs are for game servers
// calling with the runtime HTTP key, which carries no user id.
func requireServerCall(ctx context.Context) error {
	if userID, ok := ctx.Value(runtime.RUNTIME_CTX_USER_ID).(string); ok && userID != "" {
		return errClientCall
	}
	return nil
}

type characterRef struct {
	UserID      string `json:"user_id"`
	CharacterID string `json:"character_id"`
}

func (ref characterRef) validate() error {
	if !uuidPattern.MatchString(ref.UserID) {
		return errBadUserID
	}
	if !characterIDPattern.MatchString(ref.CharacterID) {
		return errBadCharacterID
	}
	return nil
}

// requireUser makes sure the owner exists (storage rows reference the users table).
func requireUser(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, userID string) error {
	users, err := nk.UsersGetId(ctx, []string{userID}, nil)
	if err != nil {
		logger.Error("users lookup failed for %s: %v", userID, err)
		return errStorage
	}
	if len(users) == 0 {
		return errUnknownUser
	}
	return nil
}

func decodePayload(payload string, into any) error {
	decoder := json.NewDecoder(strings.NewReader(payload))
	decoder.UseNumber()
	if err := decoder.Decode(into); err != nil {
		return errBadPayload
	}
	return nil
}

// --- crossguard_save_load ------------------------------------------------------------------------

type saveLoadResponse struct {
	Found bool `json:"found"`
	// Storage version to pass back as expected_version; "*" when there is no save yet.
	Version string          `json:"version"`
	Record  json.RawMessage `json:"record"`
}

// RpcSaveLoadFn: {"user_id", "character_id"} -> {"found", "version", "record"}.
func RpcSaveLoadFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var ref characterRef
	if err := decodePayload(payload, &ref); err != nil {
		return "", err
	}
	if err := ref.validate(); err != nil {
		return "", err
	}
	if err := requireUser(ctx, logger, nk, ref.UserID); err != nil {
		return "", err
	}
	objects, err := nk.StorageRead(ctx, []*runtime.StorageRead{{Collection: collectionCharacters, Key: ref.CharacterID, UserID: ref.UserID}})
	if err != nil {
		logger.Error("save load failed for %s/%s: %v", ref.UserID, ref.CharacterID, err)
		return "", errStorage
	}
	response := saveLoadResponse{Version: "*", Record: json.RawMessage("null")}
	if len(objects) > 0 {
		response.Found = true
		response.Version = objects[0].GetVersion()
		response.Record = json.RawMessage(objects[0].GetValue())
	}
	out, err := json.Marshal(response)
	if err != nil {
		// Only if the stored value is not JSON, which Nakama does not allow.
		logger.Error("save load for %s/%s: stored value is not JSON: %v", ref.UserID, ref.CharacterID, err)
		return "", errStorage
	}
	return string(out), nil
}

// --- crossguard_save_write -----------------------------------------------------------------------

type saveWriteRequest struct {
	characterRef
	// The document, as a JSON object (not a string: no escaping, and Nakama stores objects).
	Record json.RawMessage `json:"record"`
	// The version the caller loaded or last wrote; "*" = only if there is no save yet.
	ExpectedVersion string `json:"expected_version"`
}

type saveWriteResponse struct {
	Version string `json:"version"`
}

// Minimal shape check: the authoritative validation is the game server's (CrossguardRules ParseCharacterSave).
type recordShape struct {
	Version  *json.Number `json:"version"`
	PlayerID *string      `json:"playerId"`
}

func validateRecord(record json.RawMessage, userID string) error {
	if len(record) > MaxRecordBytes {
		return errRecordTooLarge
	}
	trimmed := strings.TrimSpace(string(record))
	if !strings.HasPrefix(trimmed, "{") {
		return errRecordNotObject
	}
	var shape recordShape
	decoder := json.NewDecoder(strings.NewReader(trimmed))
	decoder.UseNumber()
	if err := decoder.Decode(&shape); err != nil {
		return errRecordNotObject
	}
	if shape.Version == nil {
		return runtime.NewError("record has no \"version\"", codeInvalidArgument)
	}
	if _, err := shape.Version.Int64(); err != nil {
		return runtime.NewError("record \"version\" must be an integer", codeInvalidArgument)
	}
	if shape.PlayerID == nil || *shape.PlayerID != userID {
		return runtime.NewError("record \"playerId\" must be the user_id", codeInvalidArgument)
	}
	return nil
}

// RpcSaveWriteFn: {"user_id", "character_id", "record", "expected_version"} -> {"version"}.
// A stale expected_version answers ABORTED (HTTP 409) and the refused record is kept in
// characters_conflicts.
func RpcSaveWriteFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	if len(payload) > MaxRecordBytes+4096 {
		return "", errRecordTooLarge
	}
	var request saveWriteRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if err := request.validate(); err != nil {
		return "", err
	}
	if request.ExpectedVersion == "" {
		return "", errNoVersion
	}
	if err := validateRecord(request.Record, request.UserID); err != nil {
		return "", err
	}
	if err := requireUser(ctx, logger, nk, request.UserID); err != nil {
		return "", err
	}

	acks, err := nk.StorageWrite(ctx, []*runtime.StorageWrite{{
		Collection:      collectionCharacters,
		Key:             request.CharacterID,
		UserID:          request.UserID,
		Value:           string(request.Record),
		Version:         request.ExpectedVersion,
		PermissionRead:  permissionOwnerRead,
		PermissionWrite: permissionNoWrite,
	}})
	if err != nil {
		if isVersionConflict(err) {
			key := keepConflict(ctx, logger, nk, request)
			logger.Warn("save write for %s/%s refused: version %s is stale (kept as %s/%s)", request.UserID, request.CharacterID,
				request.ExpectedVersion, collectionConflicts, key)
			return "", runtime.NewError(fmt.Sprintf("version conflict: the save changed since version %s was loaded (record kept as %s)",
				request.ExpectedVersion, key), codeAborted)
		}
		logger.Error("save write failed for %s/%s: %v", request.UserID, request.CharacterID, err)
		return "", errStorage
	}
	if len(acks) == 0 {
		return "", errStorage
	}
	out, _ := json.Marshal(saveWriteResponse{Version: acks[0].GetVersion()})
	return string(out), nil
}

func isVersionConflict(err error) bool {
	// The runtime returns runtime.ErrStorageRejectedVersion; match the text too in case it is wrapped
	// or re-created.
	return errors.Is(err, runtime.ErrStorageRejectedVersion) || strings.Contains(err.Error(), "version check failed")
}

func asideKey(characterID string) string {
	return fmt.Sprintf("%s-%d", characterID, time.Now().UTC().UnixNano())
}

// keepConflict stores a refused write for support. Returns the key ("" if even that failed: logged).
func keepConflict(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, request saveWriteRequest) string {
	key := asideKey(request.CharacterID)
	value, _ := json.Marshal(map[string]any{
		"characterId":     request.CharacterID,
		"expectedVersion": request.ExpectedVersion,
		"refusedAt":       time.Now().UTC().Unix(),
		"record":          request.Record,
	})
	if _, err := nk.StorageWrite(ctx, []*runtime.StorageWrite{{
		Collection: collectionConflicts, Key: key, UserID: request.UserID, Value: string(value),
		PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite,
	}}); err != nil {
		logger.Error("could not keep the refused save of %s/%s: %v", request.UserID, request.CharacterID, err)
		return ""
	}
	return key
}

// --- crossguard_save_quarantine ------------------------------------------------------------------

type saveQuarantineRequest struct {
	characterRef
	Reason string `json:"reason"`
}

type saveQuarantineResponse struct {
	Quarantined bool   `json:"quarantined"`
	Key         string `json:"key,omitempty"`
}

// RpcSaveQuarantineFn: {"user_id", "character_id", "reason"} -> {"quarantined", "key"}. The game refused
// the stored save on load: a copy goes to characters_quarantine (server-only) for support. The original
// stays until the next write replaces it; nothing is deleted.
func RpcSaveQuarantineFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request saveQuarantineRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if err := request.validate(); err != nil {
		return "", err
	}
	objects, err := nk.StorageRead(ctx, []*runtime.StorageRead{{Collection: collectionCharacters, Key: request.CharacterID, UserID: request.UserID}})
	if err != nil {
		logger.Error("quarantine read failed for %s/%s: %v", request.UserID, request.CharacterID, err)
		return "", errStorage
	}
	if len(objects) == 0 {
		out, _ := json.Marshal(saveQuarantineResponse{})
		return string(out), nil
	}
	reason := request.Reason
	if len(reason) > maxReasonLength {
		reason = reason[:maxReasonLength]
	}
	key := asideKey(request.CharacterID)
	value, _ := json.Marshal(map[string]any{
		"characterId":   request.CharacterID,
		"reason":        reason,
		"version":       objects[0].GetVersion(),
		"quarantinedAt": time.Now().UTC().Unix(),
		"record":        json.RawMessage(objects[0].GetValue()),
	})
	if _, err := nk.StorageWrite(ctx, []*runtime.StorageWrite{{
		Collection: collectionQuarantine, Key: key, UserID: request.UserID, Value: string(value),
		PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite,
	}}); err != nil {
		logger.Error("quarantine write failed for %s/%s: %v", request.UserID, request.CharacterID, err)
		return "", errStorage
	}
	logger.Warn("save %s/%s quarantined as %s/%s: %s", request.UserID, request.CharacterID, collectionQuarantine, key, reason)
	out, _ := json.Marshal(saveQuarantineResponse{Quarantined: true, Key: key})
	return string(out), nil
}
