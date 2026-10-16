package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"sync"

	"github.com/heroiclabs/nakama-common/runtime"
)

// Matchmaking (docs/parties.md "Raid launch", docs/backend-setup.md "Step 3").
//
// A hub server queues a party (or a solo player) for a raid with crossguard_match_request and polls
// crossguard_match_status until it is assigned. A match forms when MinTeams teams are queued, or the
// oldest has waited MaxWaitSec (placeholders); the allocator reserves a match server and the queue fills
// it first come, first served, whole parties only, up to the server's capacity (48 players by default).
// No backfill: a match that formed takes no later teams (placeholder).
//
// Every player of an assigned request gets a match ticket: a random id stored in match_tickets
// (server-only), bound to the player, the party, the match server and the match. The player's client
// travels to the match server with ?MatchTicket=<ticket>; the match server checks it with
// crossguard_ticket_verify, which also says the player's party (their team): ?Party= is not trusted on
// match servers any more. A ticket must be used within TicketTTLSec; once used it stays valid for that
// player on that server for TicketUseSec (reconnects).
//
// Why not Nakama's built-in matchmaker: it matches realtime socket sessions (a client's
// MatchmakerAdd, or a Nakama party's), and the game talks to Nakama from its dedicated servers over HTTP
// with the server key. The queue below is small and runs in this Nakama process's memory (single-node);
// tickets are in storage.

const collectionMatchTickets = "match_tickets"

const (
	matchStateQueued    = "queued"
	matchStateAssigned  = "assigned"
	matchStateCancelled = "cancelled"
)

var (
	errBadMatchRequest = runtime.NewError("user_ids must hold 1-8 player ids (one without a party_id)", codeInvalidArgument)
	errNotInParty      = runtime.NewError("every user_id must be a member of party_id", codeInvalidArgument)
	errAlreadyQueued   = runtime.NewError("a player is already queued for a raid", codeAborted)
	errUnknownRequest  = runtime.NewError("no such match request (expired or never made)", codeNotFound)
	errNoTicket        = runtime.NewError("ticket is required", codeInvalidArgument)
	errBadTicket       = runtime.NewError("match ticket not valid", codeUnauthenticated)
	errExpiredTicket   = runtime.NewError("match ticket has expired", codeUnauthenticated)
	errWrongServer     = runtime.NewError("match ticket is for another server or player", codePermissionDenied)
)

type matchRequest struct {
	ID          string
	PartyID     string
	UserIDs     []string
	HubServerID string
	QueuedAtMs  int64
	PolledAtMs  int64
	State       string
	AssignedMs  int64
	MatchID     string
	ServerID    string
	Address     string
	Tickets     map[string]string
}

type matchQueue struct {
	mu       sync.Mutex
	config   clusterConfig
	requests map[string]*matchRequest
	// Queued requests in arrival order.
	order []string
	// User id -> the request they are queued or assigned in.
	byUser map[string]string
}

func newMatchQueue(config clusterConfig) *matchQueue {
	return &matchQueue{config: config, requests: map[string]*matchRequest{}, byUser: map[string]string{}}
}

func (q *matchQueue) forgetLocked(request *matchRequest) {
	delete(q.requests, request.ID)
	for _, user := range request.UserIDs {
		if q.byUser[user] == request.ID {
			delete(q.byUser, user)
		}
	}
	q.dequeueLocked(request.ID)
}

func (q *matchQueue) dequeueLocked(id string) {
	kept := q.order[:0]
	for _, queued := range q.order {
		if queued != id {
			kept = append(kept, queued)
		}
	}
	q.order = kept
}

// cleanLocked drops queued requests nobody polls and old assigned ones.
func (q *matchQueue) cleanLocked(at int64) {
	for _, request := range q.requests {
		switch request.State {
		case matchStateQueued:
			if at-request.PolledAtMs > int64(q.config.AbandonSec)*1000 {
				q.forgetLocked(request)
			}
		default:
			if at-request.AssignedMs > int64(q.config.KeepAssignedSec)*1000 {
				q.forgetLocked(request)
			}
		}
	}
}

// ticketRecord: the stored match ticket.
type ticketRecord struct {
	Ticket      string `json:"ticket"`
	UserID      string `json:"user_id"`
	PartyID     string `json:"party_id"`
	ServerID    string `json:"server_id"`
	MatchID     string `json:"match_id"`
	IssuedAtMs  int64  `json:"issued_at_ms"`
	ExpiresAtMs int64  `json:"expires_at_ms"`
	UsedAtMs    int64  `json:"used_at_ms,omitempty"`
}

// formMatchesLocked: while the queue rules allow, reserve a match server and fill it.
func (c *cluster) formMatchesLocked(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, at int64) {
	q := c.queue
	for len(q.order) > 0 {
		oldest := q.requests[q.order[0]]
		waited := at - oldest.QueuedAtMs
		if len(q.order) < q.config.MinTeams && waited < int64(q.config.MaxWaitSec)*1000 {
			return
		}
		matchID := newToken()
		allocation, err := c.allocator.Allocate(matchID, len(oldest.UserIDs), at)
		if err != nil {
			// No free match server: everyone waits (logged once in a while by the caller's status).
			return
		}
		// First come, first served, whole parties, up to the server's capacity.
		var chosen []*matchRequest
		players := 0
		for _, id := range q.order {
			request := q.requests[id]
			if players+len(request.UserIDs) <= allocation.Capacity {
				chosen = append(chosen, request)
				players += len(request.UserIDs)
			}
		}
		tickets := map[string]map[string]string{}
		var writes []*runtime.StorageWrite
		expires := at + int64(q.config.TicketTTLSec)*1000
		for _, request := range chosen {
			tickets[request.ID] = map[string]string{}
			for _, user := range request.UserIDs {
				ticket := newToken()
				tickets[request.ID][user] = ticket
				value, _ := json.Marshal(ticketRecord{Ticket: ticket, UserID: user, PartyID: request.PartyID, ServerID: allocation.ServerID,
					MatchID: matchID, IssuedAtMs: at, ExpiresAtMs: expires})
				writes = append(writes, &runtime.StorageWrite{Collection: collectionMatchTickets, Key: ticket, UserID: systemUserID,
					Value: string(value), Version: "*", PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite})
			}
		}
		if _, err := nk.StorageWrite(ctx, writes); err != nil {
			logger.Error("matchmaking: could not store tickets for match %s: %v", matchID, err)
			c.allocator.Release(allocation.ServerID, matchID)
			return
		}
		for _, request := range chosen {
			request.State = matchStateAssigned
			request.AssignedMs = at
			request.MatchID = matchID
			request.ServerID = allocation.ServerID
			request.Address = allocation.Address
			request.Tickets = tickets[request.ID]
			q.dequeueLocked(request.ID)
		}
		logger.Info("matchmaking: match %s on %s (%s): %d team(s), %d player(s)", matchID, allocation.ServerID, allocation.Address, len(chosen), players)
	}
}

type matchRequestPayload struct {
	PartyID  string   `json:"party_id"`
	UserIDs  []string `json:"user_ids"`
	ServerID string   `json:"server_id"`
}

type matchStatusResponse struct {
	RequestID string `json:"request_id"`
	State     string `json:"state"`
	WaitedSec int64  `json:"waited_sec"`
	MatchID   string `json:"match_id,omitempty"`
	ServerID  string `json:"server_id,omitempty"`
	Address   string `json:"address,omitempty"`
	// User id -> their ticket.
	Tickets map[string]string `json:"tickets,omitempty"`
}

func (request *matchRequest) status(at int64) matchStatusResponse {
	response := matchStatusResponse{RequestID: request.ID, State: request.State, WaitedSec: (at - request.QueuedAtMs) / 1000}
	if request.State == matchStateAssigned {
		response.MatchID = request.MatchID
		response.ServerID = request.ServerID
		response.Address = request.Address
		response.Tickets = request.Tickets
	}
	return response
}

// RpcMatchRequest: crossguard_match_request. {"party_id" ("" = solo), "user_ids", "server_id" (the
// hub)} -> {"request_id", "state", ...} (matchStatusResponse). A party's players must all be in it.
func (c *cluster) RpcMatchRequest(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request matchRequestPayload
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if len(request.UserIDs) == 0 || len(request.UserIDs) > partyMaxMembers || (request.PartyID == "" && len(request.UserIDs) != 1) {
		return "", errBadMatchRequest
	}
	if err := validUserIDs(request.UserIDs...); err != nil {
		return "", err
	}
	seen := map[string]bool{}
	for _, user := range request.UserIDs {
		if seen[user] {
			return "", errBadMatchRequest
		}
		seen[user] = true
	}
	if request.ServerID != "" && !serverIDPattern(request.ServerID) {
		return "", errBadServerID
	}
	if request.PartyID != "" {
		if !characterIDPattern.MatchString(request.PartyID) {
			return "", errBadPartyID
		}
		party, version, err := readPartyRecord(ctx, nk, request.PartyID)
		if err != nil {
			logger.Error("match request: party read failed: %v", err)
			return "", errStorage
		}
		if party == nil {
			return "", errNotInParty
		}
		for _, user := range request.UserIDs {
			if !party.isMember(user) {
				return "", errNotInParty
			}
		}
		if request.ServerID != "" && party.HubServerID != request.ServerID {
			// The team comes back to the hub it left from (crossguard_hub_find prefers it). Best effort.
			party.HubServerID = request.ServerID
			party.UpdatedAtMs = nowMs()
			value, _ := json.Marshal(party)
			_, _ = nk.StorageWrite(ctx, []*runtime.StorageWrite{{Collection: collectionParties, Key: party.ID, UserID: systemUserID, Value: string(value),
				Version: version, PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite}})
		}
	} else if users, err := nk.UsersGetId(ctx, request.UserIDs, nil); err != nil || len(users) != 1 {
		if err != nil {
			return "", errStorage
		}
		return "", errUnknownUser
	}

	q := c.queue
	q.mu.Lock()
	defer q.mu.Unlock()
	at := nowMs()
	q.cleanLocked(at)
	for _, user := range request.UserIDs {
		if existing, ok := q.byUser[user]; ok && q.requests[existing] != nil && q.requests[existing].State == matchStateQueued {
			return "", errAlreadyQueued
		}
	}
	queued := &matchRequest{ID: newToken(), PartyID: request.PartyID, UserIDs: request.UserIDs, HubServerID: request.ServerID,
		QueuedAtMs: at, PolledAtMs: at, State: matchStateQueued}
	q.requests[queued.ID] = queued
	q.order = append(q.order, queued.ID)
	for _, user := range request.UserIDs {
		q.byUser[user] = queued.ID
	}
	c.formMatchesLocked(ctx, logger, nk, at)
	out, _ := json.Marshal(queued.status(at))
	return string(out), nil
}

type matchStatusPayload struct {
	RequestID string `json:"request_id"`
}

// RpcMatchStatus: crossguard_match_status. {"request_id"} -> matchStatusResponse ("queued" or
// "assigned" with the server and every player's ticket). Polling keeps a queued request alive.
func (c *cluster) RpcMatchStatus(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request matchStatusPayload
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	q := c.queue
	q.mu.Lock()
	defer q.mu.Unlock()
	at := nowMs()
	q.cleanLocked(at)
	existing, ok := q.requests[request.RequestID]
	if !ok {
		return "", errUnknownRequest
	}
	existing.PolledAtMs = at
	c.formMatchesLocked(ctx, logger, nk, at)
	out, _ := json.Marshal(existing.status(at))
	return string(out), nil
}

type matchCancelResponse struct {
	Cancelled bool `json:"cancelled"`
}

// RpcMatchCancel: crossguard_match_cancel. {"request_id"} -> {"cancelled"} (false once assigned).
func (c *cluster) RpcMatchCancel(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request matchStatusPayload
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	q := c.queue
	q.mu.Lock()
	defer q.mu.Unlock()
	cancelled := false
	if existing, ok := q.requests[request.RequestID]; ok && existing.State == matchStateQueued {
		existing.State = matchStateCancelled
		existing.AssignedMs = nowMs()
		q.dequeueLocked(existing.ID)
		for _, user := range existing.UserIDs {
			delete(q.byUser, user)
		}
		cancelled = true
	}
	out, _ := json.Marshal(matchCancelResponse{Cancelled: cancelled})
	return string(out), nil
}

// queueSnapshot (tests and logs): queued request ids in order.
func (q *matchQueue) queueSnapshot() []string {
	q.mu.Lock()
	defer q.mu.Unlock()
	return append([]string{}, q.order...)
}

// --- crossguard_ticket_verify --------------------------------------------------------------------

type ticketVerifyRequest struct {
	Ticket   string `json:"ticket"`
	ServerID string `json:"server_id"`
	UserID   string `json:"user_id"`
}

type ticketVerifyResponse struct {
	UserID      string `json:"user_id"`
	PartyID     string `json:"party_id"`
	MatchID     string `json:"match_id"`
	ExpiresAtMs int64  `json:"expires_at_ms"`
}

// RpcTicketVerifyFn: crossguard_ticket_verify (match servers, at login). {"ticket", "server_id",
// "user_id" (the verified session's)} -> {"user_id", "party_id", "match_id", "expires_at_ms"}. Unknown or
// expired: 401; another server's or player's: 403. The first use starts its reconnect window.
func RpcTicketVerifyFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string, useSec int) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request ticketVerifyRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if request.Ticket == "" {
		return "", errNoTicket
	}
	if !characterIDPattern.MatchString(request.Ticket) {
		return "", errBadTicket
	}
	if !serverIDPattern(request.ServerID) {
		return "", errBadServerID
	}
	if err := validUserIDs(request.UserID); err != nil {
		return "", err
	}
	objects, err := nk.StorageRead(ctx, []*runtime.StorageRead{{Collection: collectionMatchTickets, Key: request.Ticket, UserID: systemUserID}})
	if err != nil {
		logger.Error("ticket verify: read failed: %v", err)
		return "", errStorage
	}
	if len(objects) == 0 {
		return "", errBadTicket
	}
	var ticket ticketRecord
	if json.Unmarshal([]byte(objects[0].GetValue()), &ticket) != nil {
		return "", errBadTicket
	}
	at := nowMs()
	if at >= ticket.ExpiresAtMs {
		return "", errExpiredTicket
	}
	if ticket.ServerID != request.ServerID || ticket.UserID != request.UserID {
		logger.Warn("ticket verify: ticket of %s for %s presented by %s on %s", ticket.UserID, ticket.ServerID, request.UserID, request.ServerID)
		return "", errWrongServer
	}
	if ticket.UsedAtMs == 0 {
		ticket.UsedAtMs = at
		ticket.ExpiresAtMs = at + int64(useSec)*1000
		value, _ := json.Marshal(ticket)
		if _, err := nk.StorageWrite(ctx, []*runtime.StorageWrite{{Collection: collectionMatchTickets, Key: ticket.Ticket, UserID: systemUserID,
			Value: string(value), Version: objects[0].GetVersion(), PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite}}); err != nil && !isRejected(err) {
			logger.Error("ticket verify: write failed: %v", err)
			return "", errStorage
		}
	}
	out, _ := json.Marshal(ticketVerifyResponse{UserID: ticket.UserID, PartyID: ticket.PartyID, MatchID: ticket.MatchID, ExpiresAtMs: ticket.ExpiresAtMs})
	return string(out), nil
}

func (c *cluster) RpcTicketVerify(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	return RpcTicketVerifyFn(ctx, logger, db, nk, payload, c.config.TicketUseSec)
}
