package main

import (
	"context"
	"database/sql"

	"github.com/heroiclabs/nakama-common/api"
	"github.com/heroiclabs/nakama-common/runtime"
)

// gRPC status codes (runtime.NewError); Nakama maps them to HTTP statuses.
const (
	codeInvalidArgument    = 3  // 400
	codeNotFound           = 5  // 404
	codePermissionDenied   = 7  // 403
	codeFailedPrecondition = 9  // 400
	codeAborted            = 10 // 409
	codeInternal           = 13 // 500
	codeUnauthenticated    = 16 // 401
)

// The runtime env var (Nakama --runtime.env) that turns device-id login on. Development only: a device
// id is just a string the client picks, so anyone could log in as anyone who uses a known id.
const envDevAuth = "CROSSGUARD_DEV_AUTH"

// Storage collections only the server writes (game clients may read their own character).
var serverOnlyCollections = map[string]bool{
	collectionCharacters: true,
	collectionQuarantine: true,
	collectionConflicts:  true,
}

var (
	errLoginMethodOff  = runtime.NewError("this login method is not available", codePermissionDenied)
	errDevAuthOff      = runtime.NewError("device login is for development servers only", codePermissionDenied)
	errServerOnlyWrite = runtime.NewError("this collection is written by the game servers only", codePermissionDenied)
)

func envValue(ctx context.Context, key string) string {
	env, ok := ctx.Value(runtime.RUNTIME_CTX_ENV).(map[string]string)
	if !ok {
		return ""
	}
	return env[key]
}

func devAuthEnabled(ctx context.Context) bool {
	value := envValue(ctx, envDevAuth)
	return value == "1" || value == "true"
}

func beforeAuthenticateDevice(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, in *api.AuthenticateDeviceRequest) (*api.AuthenticateDeviceRequest, error) {
	if !devAuthEnabled(ctx) {
		return nil, errDevAuthOff
	}
	return in, nil
}

func beforeLinkDevice(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, in *api.AccountDevice) (*api.AccountDevice, error) {
	if !devAuthEnabled(ctx) {
		return nil, errDevAuthOff
	}
	return in, nil
}

// A client storage write or delete touching a server-only collection is refused whole. Without this a
// client could create its own "characters" object before the first server save (permission_write only
// protects objects that exist).
func beforeWriteStorage(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, in *api.WriteStorageObjectsRequest) (*api.WriteStorageObjectsRequest, error) {
	for _, object := range in.GetObjects() {
		if serverOnlyCollections[object.GetCollection()] {
			return nil, errServerOnlyWrite
		}
	}
	return in, nil
}

func beforeDeleteStorage(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, in *api.DeleteStorageObjectsRequest) (*api.DeleteStorageObjectsRequest, error) {
	for _, object := range in.GetObjectIds() {
		if serverOnlyCollections[object.GetCollection()] {
			return nil, errServerOnlyWrite
		}
	}
	return in, nil
}

// denyBefore builds a before-hook that refuses the request: login methods Crossguard does not offer.
func denyBefore[T any]() func(context.Context, runtime.Logger, *sql.DB, runtime.NakamaModule, T) (T, error) {
	return func(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, in T) (T, error) {
		var none T
		return none, errLoginMethodOff
	}
}

// Steam is the login (ticket checked by Nakama with social.steam.publisher_key / app_id); device ids in
// development only; everything else is off.
func registerAuthPolicy(initializer runtime.Initializer) error {
	steps := []func() error{
		func() error { return initializer.RegisterBeforeAuthenticateDevice(beforeAuthenticateDevice) },
		func() error { return initializer.RegisterBeforeLinkDevice(beforeLinkDevice) },
		func() error { return initializer.RegisterBeforeWriteStorageObjects(beforeWriteStorage) },
		func() error { return initializer.RegisterBeforeDeleteStorageObjects(beforeDeleteStorage) },
		func() error {
			return initializer.RegisterBeforeAuthenticateApple(denyBefore[*api.AuthenticateAppleRequest]())
		},
		func() error {
			return initializer.RegisterBeforeAuthenticateCustom(denyBefore[*api.AuthenticateCustomRequest]())
		},
		func() error {
			return initializer.RegisterBeforeAuthenticateEmail(denyBefore[*api.AuthenticateEmailRequest]())
		},
		func() error {
			return initializer.RegisterBeforeAuthenticateFacebook(denyBefore[*api.AuthenticateFacebookRequest]())
		},
		func() error {
			return initializer.RegisterBeforeAuthenticateFacebookInstantGame(denyBefore[*api.AuthenticateFacebookInstantGameRequest]())
		},
		func() error {
			return initializer.RegisterBeforeAuthenticateGameCenter(denyBefore[*api.AuthenticateGameCenterRequest]())
		},
		func() error {
			return initializer.RegisterBeforeAuthenticateGoogle(denyBefore[*api.AuthenticateGoogleRequest]())
		},
		func() error { return initializer.RegisterBeforeAuthenticate(denyBefore[*api.AuthenticateRequest]()) },
		func() error { return initializer.RegisterBeforeLinkCustom(denyBefore[*api.AccountCustom]()) },
		func() error { return initializer.RegisterBeforeLinkEmail(denyBefore[*api.AccountEmail]()) },
	}
	for _, step := range steps {
		if err := step(); err != nil {
			return err
		}
	}
	return nil
}
