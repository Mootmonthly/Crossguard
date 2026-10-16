// Crossguard's Nakama runtime module (docs/backend-setup.md).
//
// Server-to-server RPCs for the dedicated game servers (called with the runtime HTTP key, never by
// game clients): character saves (saves.go) and session verification (session.go). Auth policy
// (auth.go): Steam login always, device-id login only with CROSSGUARD_DEV_AUTH=1, every other way off;
// clients cannot write or delete the save collections.
package main

import (
	"context"
	"database/sql"

	"github.com/heroiclabs/nakama-common/runtime"
)

// Registered RPC ids. The game server's Nakama client (Source/Crossguard/.../CrossguardNakamaClient)
// uses the same names.
const (
	RpcSaveLoad       = "crossguard_save_load"
	RpcSaveWrite      = "crossguard_save_write"
	RpcSaveQuarantine = "crossguard_save_quarantine"
	RpcVerifySession  = "crossguard_verify_session"
)

// InitModule is the plugin entry point Nakama looks up by name.
func InitModule(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, initializer runtime.Initializer) error {
	rpcs := map[string]func(context.Context, runtime.Logger, *sql.DB, runtime.NakamaModule, string) (string, error){
		RpcSaveLoad:       RpcSaveLoadFn,
		RpcSaveWrite:      RpcSaveWriteFn,
		RpcSaveQuarantine: RpcSaveQuarantineFn,
		RpcVerifySession:  RpcVerifySessionFn,
	}
	for id, fn := range rpcs {
		if err := initializer.RegisterRpc(id, fn); err != nil {
			return err
		}
	}
	if err := registerAuthPolicy(initializer); err != nil {
		return err
	}
	logger.Info("Crossguard module loaded (dev device auth: %v)", devAuthEnabled(ctx))
	return nil
}
