package main

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"database/sql"
	"encoding/base64"
	"encoding/json"
	"strings"
	"time"

	"github.com/heroiclabs/nakama-common/runtime"
)

// crossguard_verify_session (docs/anti-cheat.md, "Player identity"): a dedicated server got a Nakama
// session token from a connecting client (?NakamaToken=) and asks who it belongs to. The answer is the
// player id the server uses for saves, parties and reconnects, so a client can no longer claim an id.
//
// Checks: HS256 signature with the session encryption key (the same check Nakama makes), not expired,
// a user id, the account exists and is not banned. Not checked: a session ended early by logout (Nakama
// keeps that in its in-memory session cache, which the runtime cannot read); tokens are short-lived
// (session.token_expiry_sec) and the game server only checks at login.

var (
	errNoToken      = runtime.NewError("token is required", codeInvalidArgument)
	errBadToken     = runtime.NewError("session token is not valid", codeUnauthenticated)
	errExpiredToken = runtime.NewError("session token has expired", codeUnauthenticated)
	errBannedUser   = runtime.NewError("account is disabled", codePermissionDenied)
)

// Longer than any real token; stops absurd payloads before any work.
const maxTokenLength = 8192

type verifySessionRequest struct {
	Token string `json:"token"`
}

type verifySessionResponse struct {
	UserID    string `json:"user_id"`
	Username  string `json:"username"`
	SteamID   string `json:"steam_id,omitempty"`
	ExpiresAt int64  `json:"expires_at"`
}

type sessionClaims struct {
	TokenID   string `json:"tid"`
	UserID    string `json:"uid"`
	Username  string `json:"usn"`
	ExpiresAt int64  `json:"exp"`
}

// now is replaced in tests.
var now = func() time.Time { return time.Now() }

// parseSessionToken checks a Nakama session JWT (HS256, signed with session.encryption_key).
func parseSessionToken(token string, key []byte) (sessionClaims, error) {
	var claims sessionClaims
	parts := strings.Split(token, ".")
	if len(parts) != 3 || len(key) == 0 {
		return claims, errBadToken
	}
	headerJSON, err := base64.RawURLEncoding.DecodeString(parts[0])
	if err != nil {
		return claims, errBadToken
	}
	var header struct {
		Alg string `json:"alg"`
	}
	if json.Unmarshal(headerJSON, &header) != nil || header.Alg != "HS256" {
		return claims, errBadToken
	}
	signature, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil {
		return claims, errBadToken
	}
	mac := hmac.New(sha256.New, key)
	mac.Write([]byte(parts[0] + "." + parts[1]))
	if !hmac.Equal(signature, mac.Sum(nil)) {
		return claims, errBadToken
	}
	claimsJSON, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil || json.Unmarshal(claimsJSON, &claims) != nil {
		return claims, errBadToken
	}
	if !uuidPattern.MatchString(claims.UserID) {
		return claims, errBadToken
	}
	if claims.ExpiresAt == 0 || now().Unix() >= claims.ExpiresAt {
		return claims, errExpiredToken
	}
	return claims, nil
}

// NewVerifySessionRpc: {"token"} -> {"user_id", "username", "steam_id", "expires_at"}. Server-to-server
// only. encryptionKey is session.encryption_key (Initializer.GetConfig at module load).
func NewVerifySessionRpc(encryptionKey string) func(context.Context, runtime.Logger, *sql.DB, runtime.NakamaModule, string) (string, error) {
	key := []byte(encryptionKey)
	return func(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
		return verifySession(ctx, logger, nk, key, payload)
	}
}

func verifySession(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, key []byte, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request verifySessionRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if request.Token == "" {
		return "", errNoToken
	}
	if len(request.Token) > maxTokenLength {
		return "", errBadToken
	}
	claims, err := parseSessionToken(request.Token, key)
	if err != nil {
		return "", err
	}
	account, err := nk.AccountGetId(ctx, claims.UserID)
	if err != nil && !strings.Contains(strings.ToLower(err.Error()), "not found") {
		// The lookup failed (database): a server error, so the game server may retry.
		logger.Error("verify session: account lookup for %s failed: %v", claims.UserID, err)
		return "", errStorage
	}
	if err != nil || account == nil || account.GetUser() == nil {
		// Deleted since the token was issued.
		logger.Warn("verify session: no account for %s", claims.UserID)
		return "", errBadToken
	}
	if account.GetDisableTime() != nil && account.GetDisableTime().GetSeconds() > 0 {
		return "", errBannedUser
	}
	out, _ := json.Marshal(verifySessionResponse{
		UserID:    claims.UserID,
		Username:  account.GetUser().GetUsername(),
		SteamID:   account.GetUser().GetSteamId(),
		ExpiresAt: claims.ExpiresAt,
	})
	return string(out), nil
}
