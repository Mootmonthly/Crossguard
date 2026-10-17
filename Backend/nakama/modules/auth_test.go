package main

import (
	"context"
	"testing"

	"github.com/heroiclabs/nakama-common/api"
	"github.com/heroiclabs/nakama-common/runtime"
)

func envCtx(env map[string]string) context.Context {
	return context.WithValue(context.Background(), runtime.RUNTIME_CTX_ENV, env)
}

func TestDeviceAuthOnlyInDev(t *testing.T) {
	request := &api.AuthenticateDeviceRequest{Account: &api.AccountDevice{Id: "dev-alice-device-0001"}}
	for _, ctx := range []context.Context{context.Background(), envCtx(map[string]string{}), envCtx(map[string]string{envDevAuth: "0"})} {
		if _, err := beforeAuthenticateDevice(ctx, &fakeLogger{}, nil, nil, request); errorCode(err) != codePermissionDenied {
			t.Fatalf("device auth without the dev flag: %v", err)
		}
	}
	out, err := beforeAuthenticateDevice(envCtx(map[string]string{envDevAuth: "1"}), &fakeLogger{}, nil, nil, request)
	if err != nil || out != request {
		t.Fatalf("device auth with the dev flag: %v", err)
	}
	if _, err := beforeLinkDevice(context.Background(), &fakeLogger{}, nil, nil, &api.AccountDevice{Id: "x"}); errorCode(err) != codePermissionDenied {
		t.Fatalf("device link without the dev flag: %v", err)
	}
}

func TestClientsCannotWriteServerCollections(t *testing.T) {
	for _, collection := range []string{collectionCharacters, collectionQuarantine, collectionConflicts} {
		write := &api.WriteStorageObjectsRequest{Objects: []*api.WriteStorageObject{{Collection: "settings", Key: "a"}, {Collection: collection, Key: "main"}}}
		if _, err := beforeWriteStorage(context.Background(), &fakeLogger{}, nil, nil, write); errorCode(err) != codePermissionDenied {
			t.Fatalf("client write to %s: %v", collection, err)
		}
		remove := &api.DeleteStorageObjectsRequest{ObjectIds: []*api.DeleteStorageObjectId{{Collection: collection, Key: "main"}}}
		if _, err := beforeDeleteStorage(context.Background(), &fakeLogger{}, nil, nil, remove); errorCode(err) != codePermissionDenied {
			t.Fatalf("client delete in %s: %v", collection, err)
		}
	}
	other := &api.WriteStorageObjectsRequest{Objects: []*api.WriteStorageObject{{Collection: "settings", Key: "a"}}}
	if out, err := beforeWriteStorage(context.Background(), &fakeLogger{}, nil, nil, other); err != nil || out != other {
		t.Fatalf("client write elsewhere: %v", err)
	}
}

func TestOtherLoginMethodsOff(t *testing.T) {
	deny := denyBefore[*api.AuthenticateEmailRequest]()
	if _, err := deny(context.Background(), &fakeLogger{}, nil, nil, &api.AuthenticateEmailRequest{}); errorCode(err) != codePermissionDenied {
		t.Fatalf("email login: %v", err)
	}
}

func TestDescribeSteamLogin(t *testing.T) {
	for _, c := range []struct {
		appID  int
		keySet bool
		want   string
	}{
		{0, false, "Steam login off (STEAM_APP_ID / STEAM_PUBLISHER_KEY empty)"},
		{480, false, "Steam login off (STEAM_APP_ID and STEAM_PUBLISHER_KEY are both needed)"},
		{0, true, "Steam login off (STEAM_APP_ID and STEAM_PUBLISHER_KEY are both needed)"},
		{480, true, "Steam login on, app 480 (Spacewar: testing only)"},
		{123456, true, "Steam login on, app 123456"},
	} {
		if got := describeSteamLogin(c.appID, c.keySet); got != c.want {
			t.Errorf("describeSteamLogin(%d, %v) = %q, want %q", c.appID, c.keySet, got, c.want)
		}
	}
}
