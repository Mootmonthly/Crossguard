package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"math/rand/v2"
	"sort"
	"time"

	"github.com/heroiclabs/nakama-common/runtime"
)

// Parties (docs/parties.md, "On Nakama").
//
// The hub servers are authoritative towards players (a player asks their hub server; the hub server
// checks hub-only rules and calls these RPCs with the HTTP key); Nakama holds the records, so a party
// survives hub -> raid -> hub and is the same on every hub shard. The rules mirror
// CrossguardRules FPartyRegistry (Source/CrossguardRules/Private/Party.cpp): same checks, same order,
// same error names (EPartyError). Change both together.
//
// Storage (server-only writes; clients may read their own membership and inbox):
//   parties/<party id>            system-owned: {"id", "leader_id", "members" (join order), "hub_server_id"}
//   party_members/current         owned by the player: {"party_id"}
//   party_invites/inbox           owned by the player: {"invites": [{"party_id", "invited_by", "expires_at_ms"}]}
// Each operation reads what it needs with versions and writes everything it changed in one transaction
// (MultiUpdate); a concurrent change elsewhere refuses it and it runs again after a short random pause
// (up to partyOpAttempts times; Nakama logs each refused transaction as an error, which is expected).
//
// Members who have been on no game server (their save lease: leases.go) for PartyOfflineGraceSec leave
// their party when it is next read (placeholder rule; the hub's "logout leaves the party" for everyone
// else).

const (
	collectionParties      = "parties"
	collectionPartyMembers = "party_members"
	collectionPartyInvites = "party_invites"
	keyPartyMembership     = "current"
	keyPartyInbox          = "inbox"

	// Ruled (owner): up to 8 (the match's team cap) and invites last 180 s (FPartyConfig).
	partyMaxMembers          = 8
	partyInviteExpirySec     = 180
	partyOpAttempts          = 8
	maxPartyStateUserIDs     = 256
	systemUserID             = ""
	partyResultNone          = "None"
	partyOfflineGraceDefault = 120
)

// EPartyError names (CrossguardRules/Party.h); the game maps them onto ECrossguardPartyResult.
const (
	partyInvalidPlayer    = "InvalidPlayer"
	partyUnknownParty     = "UnknownParty"
	partyNotInParty       = "NotInParty"
	partyAlreadyInParty   = "AlreadyInParty"
	partyTargetNotInParty = "TargetNotInParty"
	partyNotLeader        = "NotLeader"
	partyCannotTargetSelf = "CannotTargetSelf"
	partyFull             = "PartyFull"
	partyAlreadyInvited   = "AlreadyInvited"
	partyNoInvite         = "NoInvite"
	partyInviteExpired    = "InviteExpired"
)

var (
	errBadPartyOp     = runtime.NewError("op must be invite, accept, decline, decline_all, leave, kick, promote or logout", codeInvalidArgument)
	errTooManyUsers   = runtime.NewError("too many user_ids", codeInvalidArgument)
	errPartyContended = runtime.NewError("the party changed too often while updating; try again", codeAborted)
	errBadPartyID     = runtime.NewError("party_id must be 1-64 letters, digits, '-' or '_'", codeInvalidArgument)
)

type partyRecord struct {
	ID       string   `json:"id"`
	LeaderID string   `json:"leader_id"`
	Members  []string `json:"members"`
	// The hub server that last changed it or launched it (the hub picker prefers it).
	HubServerID string `json:"hub_server_id,omitempty"`
	UpdatedAtMs int64  `json:"updated_at_ms"`
}

func (party *partyRecord) isMember(userID string) bool {
	for _, member := range party.Members {
		if member == userID {
			return true
		}
	}
	return false
}

func (party *partyRecord) remove(userID string) {
	kept := party.Members[:0]
	for _, member := range party.Members {
		if member != userID {
			kept = append(kept, member)
		}
	}
	party.Members = kept
}

type membershipRecord struct {
	PartyID string `json:"party_id"`
}

type inviteRecord struct {
	PartyID     string `json:"party_id"`
	InvitedBy   string `json:"invited_by"`
	ExpiresAtMs int64  `json:"expires_at_ms"`
}

type inboxRecord struct {
	Invites []inviteRecord `json:"invites"`
}

func (inbox *inboxRecord) find(partyID string) *inviteRecord {
	for i := range inbox.Invites {
		if inbox.Invites[i].PartyID == partyID {
			return &inbox.Invites[i]
		}
	}
	return nil
}

func (inbox *inboxRecord) remove(partyID string) {
	kept := inbox.Invites[:0]
	for _, invite := range inbox.Invites {
		if invite.PartyID != partyID {
			kept = append(kept, invite)
		}
	}
	inbox.Invites = kept
}

// --- Transaction ---------------------------------------------------------------------------------

type loadedObject struct {
	version string // "" = not stored
	dirty   bool
}

type loadedParty struct {
	loadedObject
	record  *partyRecord // nil: does not exist (or disbanded in this transaction)
	deleted bool
}

type loadedMembership struct {
	loadedObject
	partyID string
}

type loadedInbox struct {
	loadedObject
	inbox inboxRecord
}

// partyTx: the objects one operation reads and changes, written together by commit.
type partyTx struct {
	ctx         context.Context
	nk          runtime.NakamaModule
	nowMs       int64
	graceMs     int64
	parties     map[string]*loadedParty
	memberships map[string]*loadedMembership
	inboxes     map[string]*loadedInbox
	// Parties already checked for members who left the game.
	pruned map[string]bool
}

func newPartyTx(ctx context.Context, nk runtime.NakamaModule, graceSec int) *partyTx {
	return &partyTx{ctx: ctx, nk: nk, nowMs: nowMs(), graceMs: int64(graceSec) * 1000,
		parties: map[string]*loadedParty{}, memberships: map[string]*loadedMembership{}, inboxes: map[string]*loadedInbox{}, pruned: map[string]bool{}}
}

func (tx *partyTx) read(collection, key, userID string) (string, string, error) {
	objects, err := tx.nk.StorageRead(tx.ctx, []*runtime.StorageRead{{Collection: collection, Key: key, UserID: userID}})
	if err != nil {
		return "", "", err
	}
	if len(objects) == 0 {
		return "", "", nil
	}
	return objects[0].GetValue(), objects[0].GetVersion(), nil
}

func (tx *partyTx) party(partyID string) (*loadedParty, error) {
	if loaded, ok := tx.parties[partyID]; ok {
		return loaded, nil
	}
	loaded := &loadedParty{}
	if partyID != "" {
		value, version, err := tx.read(collectionParties, partyID, systemUserID)
		if err != nil {
			return nil, err
		}
		loaded.version = version
		if version != "" {
			var record partyRecord
			if json.Unmarshal([]byte(value), &record) == nil && len(record.Members) > 0 {
				loaded.record = &record
			}
		}
	}
	tx.parties[partyID] = loaded
	return loaded, nil
}

func (tx *partyTx) membership(userID string) (*loadedMembership, error) {
	if loaded, ok := tx.memberships[userID]; ok {
		return loaded, nil
	}
	value, version, err := tx.read(collectionPartyMembers, keyPartyMembership, userID)
	if err != nil {
		return nil, err
	}
	loaded := &loadedMembership{loadedObject: loadedObject{version: version}}
	var record membershipRecord
	if version != "" && json.Unmarshal([]byte(value), &record) == nil {
		loaded.partyID = record.PartyID
	}
	tx.memberships[userID] = loaded
	return loaded, nil
}

func (tx *partyTx) inbox(userID string) (*loadedInbox, error) {
	if loaded, ok := tx.inboxes[userID]; ok {
		return loaded, nil
	}
	value, version, err := tx.read(collectionPartyInvites, keyPartyInbox, userID)
	if err != nil {
		return nil, err
	}
	loaded := &loadedInbox{loadedObject: loadedObject{version: version}}
	if version != "" {
		_ = json.Unmarshal([]byte(value), &loaded.inbox)
	}
	tx.inboxes[userID] = loaded
	return loaded, nil
}

// partyOf: the player's party, or nil. A membership pointing at a party that does not list them is
// stale (fixed on the next write) and counts as none.
func (tx *partyTx) partyOf(userID string) (*loadedParty, error) {
	membership, err := tx.membership(userID)
	if err != nil || membership.partyID == "" {
		return nil, err
	}
	loaded, err := tx.party(membership.partyID)
	if err != nil {
		return nil, err
	}
	if err := tx.prune(membership.partyID); err != nil {
		return nil, err
	}
	if loaded.record == nil || !loaded.record.isMember(userID) {
		return nil, nil
	}
	return loaded, nil
}

func (tx *partyTx) setMembership(userID, partyID string) error {
	membership, err := tx.membership(userID)
	if err != nil {
		return err
	}
	if membership.partyID != partyID {
		membership.partyID = partyID
		membership.dirty = true
	}
	return nil
}

// prune: members who have been on no game server for the grace period leave (placeholder rule). A
// member with no lease record at all is never pruned (unknown, not offline).
func (tx *partyTx) prune(partyID string) error {
	if tx.pruned[partyID] || tx.graceMs <= 0 {
		return nil
	}
	tx.pruned[partyID] = true
	loaded, err := tx.party(partyID)
	if err != nil || loaded.record == nil {
		return err
	}
	leases, err := readLeases(tx.ctx, tx.nk, loaded.record.Members)
	if err != nil {
		return err
	}
	var gone []string
	for _, member := range loaded.record.Members {
		lease, ok := leases[member]
		if !ok {
			continue
		}
		if since := lease.offlineSinceMs(tx.nowMs); since > 0 && tx.nowMs-since >= tx.graceMs {
			gone = append(gone, member)
		}
	}
	for _, member := range gone {
		if _, err := tx.leave(member, loaded); err != nil {
			return err
		}
	}
	return nil
}

// leave removes the member from the loaded party (leader hand-over, disband when empty).
func (tx *partyTx) leave(userID string, loaded *loadedParty) (bool, error) {
	party := loaded.record
	party.remove(userID)
	if err := tx.setMembership(userID, ""); err != nil {
		return false, err
	}
	loaded.dirty = true
	if len(party.Members) == 0 {
		loaded.record = nil
		loaded.deleted = true
		return true, nil
	}
	if party.LeaderID == userID {
		// Members are in join order: the longest-standing one leads now.
		party.LeaderID = party.Members[0]
	}
	return false, nil
}

// commit writes every changed object, all or nothing, each checked against the version it was read at.
func (tx *partyTx) commit() error {
	var writes []*runtime.StorageWrite
	var deletes []*runtime.StorageDelete
	versionOrAbsent := func(version string) string {
		if version == "" {
			return "*"
		}
		return version
	}
	for id, loaded := range tx.parties {
		if !loaded.dirty {
			continue
		}
		if loaded.record == nil {
			if loaded.version != "" {
				deletes = append(deletes, &runtime.StorageDelete{Collection: collectionParties, Key: id, UserID: systemUserID, Version: loaded.version})
			}
			continue
		}
		loaded.record.UpdatedAtMs = tx.nowMs
		value, _ := json.Marshal(loaded.record)
		writes = append(writes, &runtime.StorageWrite{Collection: collectionParties, Key: id, UserID: systemUserID, Value: string(value),
			Version: versionOrAbsent(loaded.version), PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite})
	}
	for userID, loaded := range tx.memberships {
		if !loaded.dirty {
			continue
		}
		value, _ := json.Marshal(membershipRecord{PartyID: loaded.partyID})
		writes = append(writes, &runtime.StorageWrite{Collection: collectionPartyMembers, Key: keyPartyMembership, UserID: userID, Value: string(value),
			Version: versionOrAbsent(loaded.version), PermissionRead: permissionOwnerRead, PermissionWrite: permissionNoWrite})
	}
	for userID, loaded := range tx.inboxes {
		if !loaded.dirty {
			continue
		}
		if loaded.inbox.Invites == nil {
			loaded.inbox.Invites = []inviteRecord{}
		}
		value, _ := json.Marshal(loaded.inbox)
		writes = append(writes, &runtime.StorageWrite{Collection: collectionPartyInvites, Key: keyPartyInbox, UserID: userID, Value: string(value),
			Version: versionOrAbsent(loaded.version), PermissionRead: permissionOwnerRead, PermissionWrite: permissionNoWrite})
	}
	if len(writes) == 0 && len(deletes) == 0 {
		return nil
	}
	_, _, err := tx.nk.MultiUpdate(tx.ctx, nil, writes, deletes, nil, false)
	return err
}

// --- Rules (FPartyRegistry) ----------------------------------------------------------------------

type partyOpRequest struct {
	Op       string `json:"op"`
	ActorID  string `json:"actor_id"`
	TargetID string `json:"target_id"`
	PartyID  string `json:"party_id"`
	ServerID string `json:"server_id"`
}

type partyOpOutcome struct {
	result    string
	partyID   string
	disbanded bool
	// Players whose view changed (besides the actor).
	touched []string
}

func (tx *partyTx) checkLeaderTarget(actorID, targetID string) (*loadedParty, string, error) {
	loaded, err := tx.partyOf(actorID)
	if err != nil {
		return nil, "", err
	}
	if loaded == nil {
		return nil, partyNotInParty, nil
	}
	if loaded.record.LeaderID != actorID {
		return nil, partyNotLeader, nil
	}
	if targetID == actorID {
		return nil, partyCannotTargetSelf, nil
	}
	if !loaded.record.isMember(targetID) {
		return nil, partyTargetNotInParty, nil
	}
	return loaded, partyResultNone, nil
}

// checkInvite: FPartyRegistry::CheckInvite (party nil: one about to be founded).
func (tx *partyTx) checkInvite(party *partyRecord, inviteeID string) (string, error) {
	if inviteeID == "" {
		return partyInvalidPlayer, nil
	}
	inviteeParty, err := tx.partyOf(inviteeID)
	if err != nil {
		return "", err
	}
	if inviteeParty != nil {
		return partyAlreadyInParty, nil
	}
	if party == nil {
		if 1 >= partyMaxMembers {
			return partyFull, nil
		}
		return partyResultNone, nil
	}
	if len(party.Members) >= partyMaxMembers {
		return partyFull, nil
	}
	inbox, err := tx.inbox(inviteeID)
	if err != nil {
		return "", err
	}
	if pending := inbox.inbox.find(party.ID); pending != nil && tx.nowMs < pending.ExpiresAtMs {
		return partyAlreadyInvited, nil
	}
	return partyResultNone, nil
}

func (tx *partyTx) apply(request partyOpRequest) (partyOpOutcome, error) {
	outcome := partyOpOutcome{result: partyResultNone}
	actor := request.ActorID
	switch request.Op {
	case "invite":
		invitee := request.TargetID
		if actor == invitee {
			outcome.result = partyCannotTargetSelf
			return outcome, nil
		}
		loaded, err := tx.partyOf(actor)
		if err != nil {
			return outcome, err
		}
		if loaded == nil {
			// Founding a party for this invite: every check first, so a refusal founds nothing.
			if result, err := tx.checkInvite(nil, invitee); err != nil || result != partyResultNone {
				outcome.result = result
				return outcome, err
			}
			id := newToken()
			loaded, err = tx.party(id)
			if err != nil {
				return outcome, err
			}
			if loaded.version != "" {
				return outcome, errPartyContended
			}
			loaded.record = &partyRecord{ID: id, LeaderID: actor, Members: []string{actor}}
			loaded.dirty = true
			if err := tx.setMembership(actor, id); err != nil {
				return outcome, err
			}
		} else {
			if loaded.record.LeaderID != actor {
				outcome.result = partyNotLeader
				return outcome, nil
			}
			if result, err := tx.checkInvite(loaded.record, invitee); err != nil || result != partyResultNone {
				outcome.result = result
				return outcome, err
			}
		}
		if request.ServerID != "" && loaded.record.HubServerID != request.ServerID {
			loaded.record.HubServerID = request.ServerID
			loaded.dirty = true
		}
		inbox, err := tx.inbox(invitee)
		if err != nil {
			return outcome, err
		}
		expires := tx.nowMs + partyInviteExpirySec*1000
		if existing := inbox.inbox.find(loaded.record.ID); existing != nil {
			// Expired (checkInvite refused a live one): a fresh invite in its place.
			*existing = inviteRecord{PartyID: loaded.record.ID, InvitedBy: actor, ExpiresAtMs: expires}
		} else {
			inbox.inbox.Invites = append(inbox.inbox.Invites, inviteRecord{PartyID: loaded.record.ID, InvitedBy: actor, ExpiresAtMs: expires})
		}
		inbox.dirty = true
		outcome.partyID = loaded.record.ID
		outcome.touched = append(outcome.touched, invitee)

	case "accept":
		loaded, err := tx.party(request.PartyID)
		if err != nil {
			return outcome, err
		}
		if err := tx.prune(request.PartyID); err != nil {
			return outcome, err
		}
		if loaded.record == nil {
			outcome.result = partyUnknownParty
			return outcome, nil
		}
		inbox, err := tx.inbox(actor)
		if err != nil {
			return outcome, err
		}
		pending := inbox.inbox.find(request.PartyID)
		if pending == nil {
			outcome.result = partyNoInvite
			return outcome, nil
		}
		if tx.nowMs >= pending.ExpiresAtMs {
			outcome.result = partyInviteExpired
			return outcome, nil
		}
		current, err := tx.partyOf(actor)
		if err != nil {
			return outcome, err
		}
		if current != nil {
			outcome.result = partyAlreadyInParty
			return outcome, nil
		}
		if len(loaded.record.Members) >= partyMaxMembers {
			outcome.result = partyFull
			return outcome, nil
		}
		// One party per player: every other invite to them is moot now.
		inbox.inbox.Invites = nil
		inbox.dirty = true
		loaded.record.Members = append(loaded.record.Members, actor)
		loaded.dirty = true
		if err := tx.setMembership(actor, request.PartyID); err != nil {
			return outcome, err
		}
		outcome.partyID = request.PartyID

	case "decline":
		loaded, err := tx.party(request.PartyID)
		if err != nil {
			return outcome, err
		}
		if loaded.record == nil {
			outcome.result = partyUnknownParty
			return outcome, nil
		}
		inbox, err := tx.inbox(actor)
		if err != nil {
			return outcome, err
		}
		if inbox.inbox.find(request.PartyID) == nil {
			outcome.result = partyNoInvite
			return outcome, nil
		}
		inbox.inbox.remove(request.PartyID)
		inbox.dirty = true

	case "decline_all":
		inbox, err := tx.inbox(actor)
		if err != nil {
			return outcome, err
		}
		if len(inbox.inbox.Invites) == 0 {
			outcome.result = partyNoInvite
			return outcome, nil
		}
		inbox.inbox.Invites = nil
		inbox.dirty = true

	case "leave", "logout":
		if request.Op == "logout" {
			// Leaving the hub server: their invites go too, and no refusal (in no party is fine).
			inbox, err := tx.inbox(actor)
			if err != nil {
				return outcome, err
			}
			if len(inbox.inbox.Invites) > 0 {
				inbox.inbox.Invites = nil
				inbox.dirty = true
			}
		}
		loaded, err := tx.partyOf(actor)
		if err != nil {
			return outcome, err
		}
		if loaded == nil {
			if request.Op == "leave" {
				outcome.result = partyNotInParty
			}
			return outcome, nil
		}
		outcome.partyID = loaded.record.ID
		outcome.touched = append(outcome.touched, loaded.record.Members...)
		disbanded, err := tx.leave(actor, loaded)
		if err != nil {
			return outcome, err
		}
		outcome.disbanded = disbanded

	case "kick":
		loaded, result, err := tx.checkLeaderTarget(actor, request.TargetID)
		if err != nil || result != partyResultNone {
			outcome.result = result
			return outcome, err
		}
		outcome.partyID = loaded.record.ID
		outcome.touched = append(outcome.touched, loaded.record.Members...)
		// The leader stays, so the party neither disbands nor changes leader.
		if _, err := tx.leave(request.TargetID, loaded); err != nil {
			return outcome, err
		}

	case "promote":
		loaded, result, err := tx.checkLeaderTarget(actor, request.TargetID)
		if err != nil || result != partyResultNone {
			outcome.result = result
			return outcome, err
		}
		loaded.record.LeaderID = request.TargetID
		loaded.dirty = true
		outcome.partyID = loaded.record.ID

	default:
		return outcome, errBadPartyOp
	}
	return outcome, nil
}

// --- State answers -------------------------------------------------------------------------------

type partyView struct {
	ID          string   `json:"id"`
	LeaderID    string   `json:"leader_id"`
	Members     []string `json:"members"`
	HubServerID string   `json:"hub_server_id,omitempty"`
}

type playerPartyState struct {
	PartyID string         `json:"party_id"`
	Invites []inviteRecord `json:"invites"`
}

type partyStateResponse struct {
	// The rules' answer (EPartyError name; "None" = done). Only for crossguard_party.
	Result    string `json:"result,omitempty"`
	PartyID   string `json:"party_id,omitempty"`
	Disbanded bool   `json:"disbanded,omitempty"`
	NowMs     int64  `json:"now_ms"`
	// Each asked-for / touched player: their party and their unexpired invites (from parties that exist).
	Players map[string]playerPartyState `json:"players"`
	// Every party named above.
	Parties map[string]partyView `json:"parties"`
}

// state builds the answer for the given players from the transaction's (committed) objects.
func (tx *partyTx) state(userIDs []string) (partyStateResponse, error) {
	response := partyStateResponse{NowMs: tx.nowMs, Players: map[string]playerPartyState{}, Parties: map[string]partyView{}}
	addParty := func(loaded *loadedParty) {
		if loaded != nil && loaded.record != nil {
			response.Parties[loaded.record.ID] = partyView{ID: loaded.record.ID, LeaderID: loaded.record.LeaderID,
				Members: append([]string{}, loaded.record.Members...), HubServerID: loaded.record.HubServerID}
		}
	}
	for _, userID := range userIDs {
		if _, done := response.Players[userID]; done {
			continue
		}
		state := playerPartyState{Invites: []inviteRecord{}}
		loaded, err := tx.partyOf(userID)
		if err != nil {
			return response, err
		}
		if loaded != nil {
			state.PartyID = loaded.record.ID
			addParty(loaded)
		}
		inbox, err := tx.inbox(userID)
		if err != nil {
			return response, err
		}
		for _, invite := range inbox.inbox.Invites {
			if tx.nowMs >= invite.ExpiresAtMs {
				continue
			}
			inviting, err := tx.party(invite.PartyID)
			if err != nil {
				return response, err
			}
			if inviting.record == nil {
				continue
			}
			state.Invites = append(state.Invites, invite)
			addParty(inviting)
		}
		sort.Slice(state.Invites, func(i, j int) bool { return state.Invites[i].ExpiresAtMs < state.Invites[j].ExpiresAtMs })
		response.Players[userID] = state
	}
	return response, nil
}

// backOff: a short random pause before running a refused transaction again (spreads out the retries
// of operations that collided).
func backOff(attempt int) {
	time.Sleep(time.Duration(rand.IntN(10*attempt)+1) * time.Millisecond)
}

func partyOfflineGraceSec(ctx context.Context) int {
	return envInt(ctx, envPartyOfflineGrace, partyOfflineGraceDefault, 0, 86400)
}

func validUserIDs(ids ...string) error {
	for _, id := range ids {
		if !uuidPattern.MatchString(id) {
			return errBadUserID
		}
	}
	return nil
}

// RpcPartyFn: crossguard_party. {"op", "actor_id", "target_id" (invite, kick, promote), "party_id"
// (accept, decline), "server_id" (the calling hub)} -> the rules' result and the new state of everyone
// involved (partyStateResponse). Rule refusals are answers (200, "result" != "None"), not errors.
func RpcPartyFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request partyOpRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if err := validUserIDs(request.ActorID); err != nil {
		return "", err
	}
	switch request.Op {
	case "invite", "kick", "promote":
		if request.TargetID != "" && !uuidPattern.MatchString(request.TargetID) {
			return "", errBadUserID
		}
	case "accept", "decline":
		if !characterIDPattern.MatchString(request.PartyID) {
			return "", errBadPartyID
		}
	}
	if request.ServerID != "" && !serverIDPattern(request.ServerID) {
		return "", errBadServerID
	}
	if request.Op == "invite" && request.TargetID != "" {
		// An unknown invitee is an invalid player (the rules' answer, not an error).
		if users, err := nk.UsersGetId(ctx, []string{request.TargetID}, nil); err == nil && len(users) == 0 {
			out, _ := json.Marshal(partyStateResponse{Result: partyInvalidPlayer, NowMs: nowMs(), Players: map[string]playerPartyState{}, Parties: map[string]partyView{}})
			return string(out), nil
		}
	}
	grace := partyOfflineGraceSec(ctx)
	for attempt := 1; attempt <= partyOpAttempts; attempt++ {
		tx := newPartyTx(ctx, nk, grace)
		outcome, err := tx.apply(request)
		var response partyStateResponse
		if err == nil {
			// The answer from the changed objects, then everything is written (a refusal by the rules
			// writes only what pruning changed).
			involved := append([]string{request.ActorID}, outcome.touched...)
			if request.TargetID != "" {
				involved = append(involved, request.TargetID)
			}
			if outcome.partyID != "" {
				if loaded, _ := tx.party(outcome.partyID); loaded != nil && loaded.record != nil {
					involved = append(involved, loaded.record.Members...)
				}
			}
			response, err = tx.state(involved)
		}
		if err == nil {
			err = tx.commit()
		}
		if err != nil {
			if isRejected(err) || errors.Is(err, errPartyContended) {
				backOff(attempt)
				continue
			}
			var runtimeErr *runtime.Error
			if errors.As(err, &runtimeErr) {
				return "", err
			}
			logger.Error("party %s by %s failed: %v", request.Op, request.ActorID, err)
			return "", errStorage
		}
		response.Result = outcome.result
		response.PartyID = outcome.partyID
		response.Disbanded = outcome.disbanded
		if outcome.result == partyResultNone {
			logger.Debug("party %s by %s on %s: party %s", request.Op, request.ActorID, request.ServerID, outcome.partyID)
		}
		out, _ := json.Marshal(response)
		return string(out), nil
	}
	return "", errPartyContended
}

type partyGetRequest struct {
	UserIDs []string `json:"user_ids"`
}

// RpcPartyGetFn: crossguard_party_get. {"user_ids": [...]} -> partyStateResponse for them (hub servers:
// on a player's arrival and every few seconds for everyone on the server). Drops members who left the
// game (prune) as a side effect.
func RpcPartyGetFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request partyGetRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if len(request.UserIDs) > maxPartyStateUserIDs {
		return "", errTooManyUsers
	}
	if err := validUserIDs(request.UserIDs...); err != nil {
		return "", err
	}
	grace := partyOfflineGraceSec(ctx)
	for attempt := 1; attempt <= partyOpAttempts; attempt++ {
		tx := newPartyTx(ctx, nk, grace)
		response, err := tx.state(request.UserIDs)
		if err == nil {
			err = tx.commit()
		}
		if err != nil {
			if isRejected(err) {
				backOff(attempt)
				continue
			}
			logger.Error("party get failed: %v", err)
			return "", errStorage
		}
		out, _ := json.Marshal(response)
		return string(out), nil
	}
	return "", errPartyContended
}

// readPartyRecord: the stored party (nil if none). For matchmaking and the hub picker.
func readPartyRecord(ctx context.Context, nk runtime.NakamaModule, partyID string) (*partyRecord, string, error) {
	tx := newPartyTx(ctx, nk, 0)
	loaded, err := tx.party(partyID)
	if err != nil {
		return nil, "", err
	}
	return loaded.record, loaded.version, nil
}
