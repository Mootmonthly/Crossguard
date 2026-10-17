// Crossguard's Nakama runtime module (docs/backend-setup.md).
//
// Server-to-server RPCs for the dedicated game servers (called with the runtime HTTP key, never by
// game clients): character saves (saves.go) with save leases (leases.go), session verification
// (session.go), parties (parties.go), the game server registry and match server allocation (fleet.go),
// matchmaking and match tickets (matchmaking.go), guilds with their bank (guilds.go, rules in
// guild_rules.go). One client RPC: crossguard_hub_find_mine. Auth policy
// (auth.go): Steam login always, device-id login only with CROSSGUARD_DEV_AUTH=1, every other way off;
// clients cannot write or delete the save collections.
package main

import (
	"context"
	"database/sql"
	"errors"
	"fmt"

	"github.com/heroiclabs/nakama-common/runtime"
)

// Registered RPC ids. The game server's Nakama client (Source/Crossguard/.../CrossguardNakamaClient)
// uses the same names.
const (
	RpcSaveLoad       = "crossguard_save_load"
	RpcSaveWrite      = "crossguard_save_write"
	RpcSaveQuarantine = "crossguard_save_quarantine"
	RpcVerifySession  = "crossguard_verify_session"
	RpcLeaseAcquire   = "crossguard_lease_acquire"
	RpcLeaseRenew     = "crossguard_lease_renew"
	RpcLeaseRelease   = "crossguard_lease_release"
	RpcParty          = "crossguard_party"
	RpcPartyGet       = "crossguard_party_get"
	RpcServerRegister = "crossguard_server_register"
	RpcServerBeat     = "crossguard_server_heartbeat"
	RpcServerLeave    = "crossguard_server_deregister"
	RpcMatchRequest   = "crossguard_match_request"
	RpcMatchStatus    = "crossguard_match_status"
	RpcMatchCancel    = "crossguard_match_cancel"
	RpcMatchOver      = "crossguard_match_over"
	RpcTicketVerify   = "crossguard_ticket_verify"
	RpcHubFind        = "crossguard_hub_find"
	RpcHubFindMine    = "crossguard_hub_find_mine"
	RpcGuild          = "crossguard_guild"
	RpcGuildGet       = "crossguard_guild_get"
	RpcGuildLog       = "crossguard_guild_log"
)

// InitModule is the plugin entry point Nakama looks up by name.
func InitModule(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, initializer runtime.Initializer) error {
	config, err := initializer.GetConfig()
	if err != nil {
		return err
	}
	encryptionKey := ""
	if config != nil && config.GetSession() != nil {
		encryptionKey = config.GetSession().GetEncryptionKey()
	}
	if encryptionKey == "" {
		return errors.New("crossguard: session.encryption_key is empty; crossguard_verify_session cannot work")
	}

	// Steam login (docs/steam-setup.md): Nakama checks tickets with these (STEAM_APP_ID / STEAM_PUBLISHER_KEY).
	steamAppID := 0
	steamKeySet := false
	if config != nil && config.GetSocial() != nil && config.GetSocial().GetSteam() != nil {
		steamAppID = config.GetSocial().GetSteam().GetAppID()
		steamKeySet = config.GetSocial().GetSteam().GetPublisherKey() != ""
	}

	clusterSettings := clusterConfigFromEnv(ctx)
	c := newCluster(clusterSettings)
	rpcs := map[string]rpcFn{
		RpcSaveLoad:       RpcSaveLoadFn,
		RpcSaveWrite:      RpcSaveWriteFn,
		RpcSaveQuarantine: RpcSaveQuarantineFn,
		RpcVerifySession:  NewVerifySessionRpc(encryptionKey),
		RpcLeaseAcquire:   RpcLeaseAcquireFn,
		RpcLeaseRenew:     RpcLeaseRenewFn,
		RpcLeaseRelease:   RpcLeaseReleaseFn,
		RpcParty:          RpcPartyFn,
		RpcPartyGet:       RpcPartyGetFn,
		RpcServerRegister: c.RpcServerRegister,
		RpcServerBeat:     c.RpcServerHeartbeat,
		RpcServerLeave:    c.RpcServerDeregister,
		RpcMatchRequest:   c.RpcMatchRequest,
		RpcMatchStatus:    c.RpcMatchStatus,
		RpcMatchCancel:    c.RpcMatchCancel,
		RpcMatchOver:      c.RpcMatchOver,
		RpcTicketVerify:   c.RpcTicketVerify,
		RpcHubFind:        c.RpcHubFind,
		RpcHubFindMine:    c.RpcHubFindMine,
		RpcGuild:          RpcGuildFn,
		RpcGuildGet:       RpcGuildGetFn,
		RpcGuildLog:       RpcGuildLogFn,
	}
	for id, fn := range rpcs {
		if err := initializer.RegisterRpc(id, fn); err != nil {
			return err
		}
	}
	if err := registerAuthPolicy(initializer); err != nil {
		return err
	}
	logger.Info("Crossguard module loaded (dev device auth: %v; %s; matchmaking: %d teams or %d s, tickets %d s, heartbeat %d s; %s)",
		devAuthEnabled(ctx), describeSteamLogin(steamAppID, steamKeySet), clusterSettings.MinTeams, clusterSettings.MaxWaitSec, clusterSettings.TicketTTLSec,
		clusterSettings.HeartbeatSec, c.allocator.Describe())
	return nil
}

// describeSteamLogin: the load log line's Steam part (never the key itself).
func describeSteamLogin(appID int, publisherKeySet bool) string {
	switch {
	case appID == 0 && !publisherKeySet:
		return "Steam login off (STEAM_APP_ID / STEAM_PUBLISHER_KEY empty)"
	case appID == 0 || !publisherKeySet:
		return "Steam login off (STEAM_APP_ID and STEAM_PUBLISHER_KEY are both needed)"
	case appID == 480:
		return "Steam login on, app 480 (Spacewar: testing only)"
	}
	return fmt.Sprintf("Steam login on, app %d", appID)
}
