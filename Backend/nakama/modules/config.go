package main

import (
	"context"
	"strconv"
)

// Tunables read from Nakama's runtime environment (--runtime.env KEY=value; docker-compose.yml passes
// them from Backend/.env). Every default here is a placeholder until the owner rules on it
// (docs/design-decisions.md, "Online architecture").
const (
	envPartyOfflineGrace = "CROSSGUARD_PARTY_OFFLINE_GRACE_SEC" // members on no server this long leave (120)
	envMatchMinTeams     = "CROSSGUARD_MATCH_MIN_TEAMS"         // a match starts with this many teams queued (2)
	envMatchMaxWait      = "CROSSGUARD_MATCH_MAX_WAIT_SEC"      // ... or when the oldest has waited this long (30)
	envTicketTTL         = "CROSSGUARD_MATCH_TICKET_TTL_SEC"    // a match ticket must be used this soon (120)
	envHeartbeat         = "CROSSGUARD_SERVER_HEARTBEAT_SEC"    // game servers report this often (10)
)

// envInt: the runtime env value as an int in [minValue, maxValue], else fallback.
func envInt(ctx context.Context, key string, fallback, minValue, maxValue int) int {
	value, err := strconv.Atoi(envValue(ctx, key))
	if err != nil || value < minValue || value > maxValue {
		return fallback
	}
	return value
}

// clusterConfig: matchmaking and fleet settings.
type clusterConfig struct {
	MinTeams       int
	MaxWaitSec     int
	TicketTTLSec   int
	HeartbeatSec   int
	DefaultRaidCap int
	DefaultHubCap  int
	// A ticket once used stays valid for reconnects this long (the raid time limit, 3600 s, plus margin).
	TicketUseSec int
	// A queued request nobody asked about for this long is dropped (the hub stopped polling).
	AbandonSec int
	// An assigned request is kept this long for status reads.
	KeepAssignedSec int
	// A match server allocated but reporting no players this long after allocation is free again.
	AllocationTimeoutSec int
}

func defaultClusterConfig() clusterConfig {
	return clusterConfig{
		MinTeams:             2,
		MaxWaitSec:           30,
		TicketTTLSec:         120,
		HeartbeatSec:         10,
		DefaultRaidCap:       48,
		DefaultHubCap:        64,
		TicketUseSec:         4200,
		AbandonSec:           30,
		KeepAssignedSec:      300,
		AllocationTimeoutSec: 180,
	}
}

func clusterConfigFromEnv(ctx context.Context) clusterConfig {
	config := defaultClusterConfig()
	config.MinTeams = envInt(ctx, envMatchMinTeams, config.MinTeams, 1, 64)
	config.MaxWaitSec = envInt(ctx, envMatchMaxWait, config.MaxWaitSec, 0, 3600)
	config.TicketTTLSec = envInt(ctx, envTicketTTL, config.TicketTTLSec, 10, 3600)
	config.HeartbeatSec = envInt(ctx, envHeartbeat, config.HeartbeatSec, 1, 300)
	config.AllocationTimeoutSec = config.TicketTTLSec + 60
	return config
}
