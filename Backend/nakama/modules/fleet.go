package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"sort"
	"sync"

	"github.com/heroiclabs/nakama-common/runtime"
)

// Game server registry and match server allocation (docs/backend-setup.md, "Step 3").
//
// Dedicated servers register on start (crossguard_server_register: id, kind "hub" or "match", address,
// capacity), report every HeartbeatSec (crossguard_server_heartbeat: player count) and deregister on exit;
// one that misses three heartbeats is dropped. The registry lives in this Nakama process's memory (Nakama
// open source is single-node); after a Nakama restart a heartbeat answers "unknown" and the server
// registers again.
//
// Match servers are handed out through Allocator. The local fleet allocator gives a registered, free
// match server to one match (one match per process); GameLift (Anywhere locally, managed fleets later)
// will be another Allocator with the same shape: reserve a server for a match, get its address.

const (
	serverKindHub   = "hub"
	serverKindMatch = "match"

	maxAddressLength = 255
	maxCapacity      = 1000
)

var (
	errBadKind       = runtime.NewError("kind must be \"hub\" or \"match\"", codeInvalidArgument)
	errBadAddress    = runtime.NewError("address must be host:port (letters, digits, '.', '-', ':', '[', ']')", codeInvalidArgument)
	errBadCapacity   = runtime.NewError(fmt.Sprintf("capacity must be 0 (default) to %d", maxCapacity), codeInvalidArgument)
	errNoHubServer   = runtime.NewError("no hub server with room", codeNotFound)
	errNoMatchServer = errors.New("no free match server")
)

type fleetServer struct {
	ID       string `json:"server_id"`
	Kind     string `json:"kind"`
	Address  string `json:"address"`
	Capacity int    `json:"capacity"`
	Players  int    `json:"players"`
	// Hub: players sent here since the last heartbeat (not counted in Players yet).
	Pending        int    `json:"pending,omitempty"`
	RegisteredAtMs int64  `json:"registered_at_ms"`
	LastSeenMs     int64  `json:"last_seen_ms"`
	MatchID        string `json:"match_id,omitempty"`
	AllocatedAtMs  int64  `json:"allocated_at_ms,omitempty"`
}

func (server *fleetServer) room() int { return server.Capacity - server.Players - server.Pending }

// fleet: every registered game server.
type fleet struct {
	mu      sync.Mutex
	config  clusterConfig
	servers map[string]*fleetServer
}

func newFleet(config clusterConfig) *fleet {
	return &fleet{config: config, servers: map[string]*fleetServer{}}
}

// expireLocked drops servers that missed three heartbeats and frees allocations nobody came to.
func (f *fleet) expireLocked(at int64) {
	for id, server := range f.servers {
		if at-server.LastSeenMs > int64(3*f.config.HeartbeatSec)*1000 {
			delete(f.servers, id)
		}
	}
}

func validAddress(address string) bool {
	if len(address) == 0 || len(address) > maxAddressLength {
		return false
	}
	for _, char := range address {
		ok := (char >= 'a' && char <= 'z') || (char >= 'A' && char <= 'Z') || (char >= '0' && char <= '9') ||
			char == '.' || char == '-' || char == ':' || char == '[' || char == ']'
		if !ok {
			return false
		}
	}
	return true
}

func (f *fleet) register(id, kind, address string, capacity int, at int64) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.expireLocked(at)
	if capacity == 0 {
		capacity = f.config.DefaultHubCap
		if kind == serverKindMatch {
			capacity = f.config.DefaultRaidCap
		}
	}
	server, ok := f.servers[id]
	if !ok || server.Kind != kind {
		server = &fleetServer{ID: id, Kind: kind, RegisteredAtMs: at}
		f.servers[id] = server
	}
	server.Address = address
	server.Capacity = capacity
	server.LastSeenMs = at
}

// heartbeat: false if the server is not registered (it registers again).
func (f *fleet) heartbeat(id string, players int, at int64) bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.expireLocked(at)
	server, ok := f.servers[id]
	if !ok {
		return false
	}
	server.LastSeenMs = at
	server.Players = players
	server.Pending = 0
	if server.Kind == serverKindMatch && server.MatchID != "" && players == 0 &&
		at-server.AllocatedAtMs > int64(f.config.AllocationTimeoutSec)*1000 {
		// Allocated, but nobody came (tickets expired): free again.
		server.MatchID = ""
		server.AllocatedAtMs = 0
	}
	return true
}

func (f *fleet) deregister(id string) bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	_, ok := f.servers[id]
	delete(f.servers, id)
	return ok
}

// matchOver frees a match server for the next match (a server kept running: -KeepMatchServer).
func (f *fleet) matchOver(id, matchID string) bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	server, ok := f.servers[id]
	if !ok || server.Kind != serverKindMatch || (matchID != "" && server.MatchID != matchID) {
		return false
	}
	server.MatchID = ""
	server.AllocatedAtMs = 0
	return true
}

func (f *fleet) get(id string) (fleetServer, bool) {
	f.mu.Lock()
	defer f.mu.Unlock()
	server, ok := f.servers[id]
	if !ok {
		return fleetServer{}, false
	}
	return *server, true
}

// pickHub: the preferred hub if it is registered and has room, else the hub with the most room (ties:
// the older registration). The pick counts as a pending player until the hub's next heartbeat.
func (f *fleet) pickHub(preferredID string, at int64) (fleetServer, bool) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.expireLocked(at)
	var best *fleetServer
	if preferred, ok := f.servers[preferredID]; ok && preferred.Kind == serverKindHub && preferred.room() > 0 {
		best = preferred
	}
	if best == nil {
		for _, server := range f.servers {
			if server.Kind != serverKindHub || server.room() <= 0 {
				continue
			}
			if best == nil || server.room() > best.room() ||
				(server.room() == best.room() && (server.RegisteredAtMs < best.RegisteredAtMs ||
					(server.RegisteredAtMs == best.RegisteredAtMs && server.ID < best.ID))) {
				best = server
			}
		}
	}
	if best == nil {
		return fleetServer{}, false
	}
	best.Pending++
	return *best, true
}

// --- Allocator -----------------------------------------------------------------------------------

// Allocation: a match server reserved for one match.
type Allocation struct {
	ServerID string
	Address  string
	// Players the match may hold (the server's registered capacity: 48 by default).
	Capacity int
}

// Allocator hands out match servers. Implementations: localFleetAllocator (registered dev servers);
// GameLift (Anywhere / managed fleets) later.
type Allocator interface {
	// Allocate reserves a server for matchID that holds at least minPlayers; errNoMatchServer if none.
	Allocate(matchID string, minPlayers int, at int64) (Allocation, error)
	// Release gives the server back (the match could not be set up).
	Release(serverID, matchID string)
	Describe() string
}

type localFleetAllocator struct {
	fleet *fleet
}

func (a *localFleetAllocator) Describe() string { return "local fleet (registered match servers)" }

func (a *localFleetAllocator) Allocate(matchID string, minPlayers int, at int64) (Allocation, error) {
	f := a.fleet
	f.mu.Lock()
	defer f.mu.Unlock()
	f.expireLocked(at)
	var free []*fleetServer
	for _, server := range f.servers {
		if server.Kind == serverKindMatch && server.MatchID == "" && server.Capacity >= minPlayers {
			free = append(free, server)
		}
	}
	if len(free) == 0 {
		return Allocation{}, errNoMatchServer
	}
	sort.Slice(free, func(i, j int) bool {
		if free[i].RegisteredAtMs != free[j].RegisteredAtMs {
			return free[i].RegisteredAtMs < free[j].RegisteredAtMs
		}
		return free[i].ID < free[j].ID
	})
	server := free[0]
	server.MatchID = matchID
	server.AllocatedAtMs = at
	return Allocation{ServerID: server.ID, Address: server.Address, Capacity: server.Capacity}, nil
}

func (a *localFleetAllocator) Release(serverID, matchID string) {
	a.fleet.matchOver(serverID, matchID)
}

// --- RPCs ----------------------------------------------------------------------------------------

type serverRequest struct {
	ServerID string `json:"server_id"`
	Kind     string `json:"kind"`
	Address  string `json:"address"`
	Capacity int    `json:"capacity"`
	Players  int    `json:"players"`
	MatchID  string `json:"match_id"`
}

func decodeServerRequest(ctx context.Context, payload string) (serverRequest, error) {
	var request serverRequest
	if err := requireServerCall(ctx); err != nil {
		return request, err
	}
	if err := decodePayload(payload, &request); err != nil {
		return request, err
	}
	if !serverIDPattern(request.ServerID) {
		return request, errBadServerID
	}
	return request, nil
}

type registerResponse struct {
	HeartbeatSec int `json:"heartbeat_sec"`
	Capacity     int `json:"capacity"`
	// For tools and logs (Tools/backend_smoke.py checks ticket expiry when it is short).
	TicketTTLSec int `json:"ticket_ttl_sec"`
}

type heartbeatResponse struct {
	Known bool `json:"known"`
}

type okResponse struct {
	Ok bool `json:"ok"`
}

// cluster: the in-memory matchmaking state of this Nakama process.
type cluster struct {
	config    clusterConfig
	fleet     *fleet
	allocator Allocator
	queue     *matchQueue
}

func newCluster(config clusterConfig) *cluster {
	f := newFleet(config)
	c := &cluster{config: config, fleet: f, allocator: &localFleetAllocator{fleet: f}}
	c.queue = newMatchQueue(config)
	return c
}

type rpcFn = func(context.Context, runtime.Logger, *sql.DB, runtime.NakamaModule, string) (string, error)

// RpcServerRegister: crossguard_server_register. {"server_id", "kind", "address", "capacity" (0: 48 for
// match servers, 64 for hubs)} -> {"heartbeat_sec", "capacity", "ticket_ttl_sec"}.
func (c *cluster) RpcServerRegister(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	request, err := decodeServerRequest(ctx, payload)
	if err != nil {
		return "", err
	}
	if request.Kind != serverKindHub && request.Kind != serverKindMatch {
		return "", errBadKind
	}
	if !validAddress(request.Address) {
		return "", errBadAddress
	}
	if request.Capacity < 0 || request.Capacity > maxCapacity {
		return "", errBadCapacity
	}
	c.fleet.register(request.ServerID, request.Kind, request.Address, request.Capacity, nowMs())
	server, _ := c.fleet.get(request.ServerID)
	logger.Info("game server registered: %s %s at %s (capacity %d)", request.Kind, request.ServerID, request.Address, server.Capacity)
	out, _ := json.Marshal(registerResponse{HeartbeatSec: c.config.HeartbeatSec, Capacity: server.Capacity, TicketTTLSec: c.config.TicketTTLSec})
	return string(out), nil
}

// RpcServerHeartbeat: crossguard_server_heartbeat. {"server_id", "players"} -> {"known"} (false: register again).
func (c *cluster) RpcServerHeartbeat(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	request, err := decodeServerRequest(ctx, payload)
	if err != nil {
		return "", err
	}
	if request.Players < 0 || request.Players > maxCapacity {
		return "", errBadCapacity
	}
	out, _ := json.Marshal(heartbeatResponse{Known: c.fleet.heartbeat(request.ServerID, request.Players, nowMs())})
	return string(out), nil
}

// RpcServerDeregister: crossguard_server_deregister. {"server_id"} -> {"ok"}.
func (c *cluster) RpcServerDeregister(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	request, err := decodeServerRequest(ctx, payload)
	if err != nil {
		return "", err
	}
	removed := c.fleet.deregister(request.ServerID)
	if removed {
		logger.Info("game server deregistered: %s", request.ServerID)
	}
	out, _ := json.Marshal(okResponse{Ok: removed})
	return string(out), nil
}

// RpcMatchOver: crossguard_match_over. {"server_id", "match_id"} -> {"ok"}: the match server is free for
// the next match (it keeps running: -KeepMatchServer; a server that exits deregisters instead).
func (c *cluster) RpcMatchOver(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	request, err := decodeServerRequest(ctx, payload)
	if err != nil {
		return "", err
	}
	out, _ := json.Marshal(okResponse{Ok: c.fleet.matchOver(request.ServerID, request.MatchID)})
	return string(out), nil
}

type hubFindRequest struct {
	UserID  string `json:"user_id"`
	PartyID string `json:"party_id"`
}

type hubFindResponse struct {
	ServerID string `json:"server_id"`
	Address  string `json:"address"`
}

func (c *cluster) findHub(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, userID, partyID string) (string, error) {
	// The party's hub first (the hub that launched it or last changed it), so a team comes back together.
	preferred := ""
	if partyID == "" && userID != "" {
		tx := newPartyTx(ctx, nk, 0)
		if membership, err := tx.membership(userID); err == nil {
			partyID = membership.partyID
		}
	}
	if partyID != "" {
		if party, _, err := readPartyRecord(ctx, nk, partyID); err == nil && party != nil {
			preferred = party.HubServerID
		}
	}
	hub, ok := c.fleet.pickHub(preferred, nowMs())
	if !ok {
		return "", errNoHubServer
	}
	out, _ := json.Marshal(hubFindResponse{ServerID: hub.ID, Address: hub.Address})
	return string(out), nil
}

// RpcHubFind: crossguard_hub_find (servers). {"user_id", "party_id"} -> {"server_id", "address"}; 404
// when no hub has room.
func (c *cluster) RpcHubFind(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request hubFindRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if request.UserID != "" && !uuidPattern.MatchString(request.UserID) {
		return "", errBadUserID
	}
	if request.PartyID != "" && !characterIDPattern.MatchString(request.PartyID) {
		return "", errBadPartyID
	}
	return c.findHub(ctx, logger, nk, request.UserID, request.PartyID)
}

// RpcHubFindMine: crossguard_hub_find_mine (game clients, with their session: the main menu's Play).
// {} -> {"server_id", "address"} for the caller (their party's hub first).
func (c *cluster) RpcHubFindMine(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	userID, _ := ctx.Value(runtime.RUNTIME_CTX_USER_ID).(string)
	if userID == "" {
		return "", runtime.NewError("log in first", codeUnauthenticated)
	}
	return c.findHub(ctx, logger, nk, userID, "")
}
