package main

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"strings"
	"testing"
	"time"
)

const testKey = "test-encryption-key"

func b64(data string) string { return base64.RawURLEncoding.EncodeToString([]byte(data)) }

// makeToken signs a token the way Nakama does (HS256, claims tid/uid/usn/exp).
func makeToken(key, alg string, claims map[string]any) string {
	header := b64(`{"alg":"` + alg + `","typ":"JWT"}`)
	body, _ := json.Marshal(claims)
	unsigned := header + "." + b64(string(body))
	mac := hmac.New(sha256.New, []byte(key))
	mac.Write([]byte(unsigned))
	return unsigned + "." + base64.RawURLEncoding.EncodeToString(mac.Sum(nil))
}

func validClaims(userID string) map[string]any {
	return map[string]any{"tid": "token-1", "uid": userID, "usn": "alice", "exp": now().Add(time.Hour).Unix(), "iat": now().Unix()}
}

func verify(nk *fakeNakama, token string) (verifySessionResponse, error) {
	rpc := NewVerifySessionRpc(testKey)
	payload, _ := json.Marshal(map[string]string{"token": token})
	out, err := rpc(serverCtx(), &fakeLogger{}, nil, nk, string(payload))
	var response verifySessionResponse
	if err == nil {
		err = json.Unmarshal([]byte(out), &response)
	}
	return response, err
}

func TestVerifyValidSession(t *testing.T) {
	nk := newFakeNakama(alice)
	nk.users[alice].User.SteamId = "76561198000000001"
	response, err := verify(nk, makeToken(testKey, "HS256", validClaims(alice)))
	if err != nil {
		t.Fatalf("valid token: %v", err)
	}
	if response.UserID != alice || response.SteamID != "76561198000000001" || response.ExpiresAt == 0 {
		t.Fatalf("response: %+v", response)
	}
}

func TestVerifyRefusesBadTokens(t *testing.T) {
	nk := newFakeNakama(alice)
	expired := validClaims(alice)
	expired["exp"] = now().Add(-time.Minute).Unix()
	noExpiry := validClaims(alice)
	delete(noExpiry, "exp")
	badUser := validClaims("not-a-uuid")
	good := makeToken(testKey, "HS256", validClaims(alice))
	parts := strings.Split(good, ".")
	tamperedClaims := validClaims(bob)
	tamperedBody, _ := json.Marshal(tamperedClaims)
	cases := map[string]struct {
		token string
		code  int
	}{
		"empty":           {"", codeInvalidArgument},
		"garbage":         {"not.a.token", codeUnauthenticated},
		"two parts":       {parts[0] + "." + parts[1], codeUnauthenticated},
		"wrong key":       {makeToken("other-key", "HS256", validClaims(alice)), codeUnauthenticated},
		"alg none":        {makeToken(testKey, "none", validClaims(alice)), codeUnauthenticated},
		"swapped claims":  {parts[0] + "." + b64(string(tamperedBody)) + "." + parts[2], codeUnauthenticated},
		"expired":         {makeToken(testKey, "HS256", expired), codeUnauthenticated},
		"no expiry":       {makeToken(testKey, "HS256", noExpiry), codeUnauthenticated},
		"bad user id":     {makeToken(testKey, "HS256", badUser), codeUnauthenticated},
		"unknown account": {makeToken(testKey, "HS256", validClaims(bob)), codeUnauthenticated},
		"too long":        {strings.Repeat("a", maxTokenLength+1), codeUnauthenticated},
	}
	for name, test := range cases {
		if _, err := verify(nk, test.token); errorCode(err) != test.code {
			t.Errorf("%s: want code %d, got %v", name, test.code, err)
		}
	}
}

func TestVerifyRefusesBannedAccount(t *testing.T) {
	nk := newFakeNakama(alice)
	nk.ban(alice)
	if _, err := verify(nk, makeToken(testKey, "HS256", validClaims(alice))); errorCode(err) != codePermissionDenied {
		t.Fatalf("banned: %v", err)
	}
}

func TestVerifyIsServerOnly(t *testing.T) {
	nk := newFakeNakama(alice)
	rpc := NewVerifySessionRpc(testKey)
	payload, _ := json.Marshal(map[string]string{"token": makeToken(testKey, "HS256", validClaims(alice))})
	if _, err := rpc(clientCtx(alice), &fakeLogger{}, nil, nk, string(payload)); errorCode(err) != codePermissionDenied {
		t.Fatalf("client call: %v", err)
	}
}
