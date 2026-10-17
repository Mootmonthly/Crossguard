package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"sort"
	"strings"

	"github.com/heroiclabs/nakama-common/api"
	"github.com/heroiclabs/nakama-common/runtime"
)

// Guilds (docs/guild-rules.md, "On Nakama").
//
// Like parties: the hub servers are authoritative towards players (a player asks their hub server; it
// checks the game's own rules, such as "the bank between raids only" and the player's inventory, and calls
// crossguard_guild with the HTTP key). Nakama holds the guilds with the rules of FGuildRegistry
// (guild_rules.go), so a guild is the same on every hub shard and survives travel and restarts.
//
// Storage (every collection server-only for writes, auth.go; players may read their own membership and
// inbox):
//   guilds/<guild id>            system-owned: the whole guild (guildRecord: roster and ranks, invites,
//                                bank gold and items, the newest bank log entries)
//   guild_names/<name key>       system-owned: {"guild_id"}; the case-insensitive unique name index
//   guild_members/current        owned by the player: {"guild_id"}; one guild per player
//   guild_invites/inbox          owned by the player: {"invites": [...]} (an index for the player's view;
//                                the guild's own invite list decides)
//   guild_bank_log/<id>.<seq>    system-owned, append-only: every bank deposit, withdrawal and
//                                redistribution, never rewritten or deleted (written with version "*")
//   guild_requests/<request id>  system-owned: bank operations already applied (a retried request is
//                                answered from here, never applied twice)
//
// Every operation reads what it needs with versions and writes everything it changed in ONE
// transaction (MultiUpdate); a concurrent change refuses it and it runs again after a short pause (up to
// guildOpAttempts times). Bank operations that move gold or items between the bank and a player carry
// that player's character save (the inventory and banked gold after the move, built by the hub server)
// with its expected version and save lease token: the guild, its log and the save are written together
// or not at all, so nothing is ever duplicated or lost between the bank and the player.
//
// The Nakama group: each guild is also a Nakama group with the same id (closed, max MaxMembers), its
// members and ranks mirrored after every change (Leader = superadmin, Officer = admin, Member = member).
// It is a mirror for the console, client reads and a later guild chat: the rules and the bank live in
// storage, because groups cannot do one-guild-per-player, exactly one Leader, invite expiry,
// case-insensitive names or a transaction with the bank and the save. Clients cannot change groups
// (before-hooks, auth.go). A failed mirror update is logged and repaired on the guild's next change.

const (
	collectionGuilds        = "guilds"
	collectionGuildNames    = "guild_names"
	collectionGuildMembers  = "guild_members"
	collectionGuildInvites  = "guild_invites"
	collectionGuildBankLog  = "guild_bank_log"
	collectionGuildRequests = "guild_requests"
	keyGuildMembership      = "current"
	keyGuildInbox           = "inbox"

	guildOpAttempts = 8
	// Transport limits (placeholders): the game's item state per bank stack, item ids, a stack's side.
	maxGuildPayloadBytes = 4096
	maxGuildItemIDLength = 128
	maxGuildItemSide     = 16
	maxGuildLogRead      = 100
	// A group found under a guild's name with no guild behind it is a leftover (a failed delete) once
	// it is this old; younger, it may be a create still on its way.
	staleGroupAgeSec = 60

	// The game's own answer (besides EGuildError's): the bank is not what the hub server expected (the
	// last member's leave) or the hub sent no save where one is needed. Nothing changed.
	guildBankChanged = "BankChanged"
)

var (
	errBadGuildOp      = runtime.NewError("op must be create, invite, accept, decline, kick, promote, demote, transfer, leave, deposit_gold, withdraw_gold, give_gold, deposit_item, withdraw_item, give_item or move_item", codeInvalidArgument)
	errBadGuildID      = runtime.NewError("guild_id must be a guild id (UUID)", codeInvalidArgument)
	errBadRequestID    = runtime.NewError("request_id must be 1-64 letters, digits, '-' or '_'", codeInvalidArgument)
	errNeedSave        = runtime.NewError("this bank operation needs the player's save (save: user_id, record, expected_version, lease_token)", codeInvalidArgument)
	errWrongSaveUser   = runtime.NewError("save.user_id must be the player whose gold or inventory changes", codeInvalidArgument)
	errBadGuildItem    = runtime.NewError(fmt.Sprintf("item: item_id 1-%d bytes, rarity 0-5, count >= 1, width and height 1-%d, payload at most %d bytes", maxGuildItemIDLength, maxGuildItemSide, maxGuildPayloadBytes), codeInvalidArgument)
	errGuildContended  = runtime.NewError("the guild changed too often while updating; try again", codeAborted)
	errGuildSaveStale  = runtime.NewError("version conflict: the player's save changed since expected_version (nothing was written)", codeAborted)
	errGuildNameLength = runtime.NewError("name is too long", codeInvalidArgument)
)

// goldText: an int64 sent as a JSON string (the game reads JSON numbers as doubles: 53 bits).
type goldText int64

func (g goldText) MarshalJSON() ([]byte, error) { return json.Marshal(fmt.Sprintf("%d", int64(g))) }

func (g *goldText) UnmarshalJSON(data []byte) error {
	var text string
	if err := json.Unmarshal(data, &text); err != nil {
		return err
	}
	var value int64
	if _, err := fmt.Sscan(text, &value); err != nil {
		return err
	}
	*g = goldText(value)
	return nil
}

// --- Transaction (the storage guildWorld) ----------------------------------------------------------

type loadedGuild struct {
	loadedObject
	record  *guildRecord // nil: none (or disbanded in this transaction)
	deleted bool
}

type loadedGuildName struct {
	loadedObject
	guildID string
	deleted bool
}

type loadedGuildMembership struct {
	loadedObject
	guildID string
}

type guildInboxEntry struct {
	GuildID     string `json:"guild_id"`
	InvitedBy   string `json:"invited_by"`
	ExpiresAtMs int64  `json:"expires_at_ms"`
}

type guildInboxRecord struct {
	Invites []guildInboxEntry `json:"invites"`
}

type loadedGuildInbox struct {
	loadedObject
	inbox guildInboxRecord
}

type guildLogWrite struct {
	guildID string
	entry   guildLogEntry
}

// guildTx: the objects one operation reads and changes, written together by commit.
type guildTx struct {
	ctx         context.Context
	nk          runtime.NakamaModule
	nowMs       int64
	guilds      map[string]*loadedGuild
	names       map[string]*loadedGuildName
	memberships map[string]*loadedGuildMembership
	inboxes     map[string]*loadedGuildInbox
	logs        []guildLogWrite
	// Written with the rest (the player's save and lease guard, the applied-request marker).
	extra []*runtime.StorageWrite
}

func newGuildTx(ctx context.Context, nk runtime.NakamaModule) *guildTx {
	return &guildTx{ctx: ctx, nk: nk, nowMs: nowMs(), guilds: map[string]*loadedGuild{}, names: map[string]*loadedGuildName{},
		memberships: map[string]*loadedGuildMembership{}, inboxes: map[string]*loadedGuildInbox{}}
}

func (tx *guildTx) read(collection, key, userID string) (string, string, error) {
	objects, err := tx.nk.StorageRead(tx.ctx, []*runtime.StorageRead{{Collection: collection, Key: key, UserID: userID}})
	if err != nil {
		return "", "", err
	}
	if len(objects) == 0 {
		return "", "", nil
	}
	return objects[0].GetValue(), objects[0].GetVersion(), nil
}

func (tx *guildTx) loadGuild(id string) (*loadedGuild, error) {
	if loaded, ok := tx.guilds[id]; ok {
		return loaded, nil
	}
	loaded := &loadedGuild{}
	if id != "" {
		value, version, err := tx.read(collectionGuilds, id, systemUserID)
		if err != nil {
			return nil, err
		}
		loaded.version = version
		if version != "" {
			var record guildRecord
			if json.Unmarshal([]byte(value), &record) == nil && record.ID == id && len(record.Members) > 0 {
				loaded.record = &record
			}
		}
	}
	tx.guilds[id] = loaded
	return loaded, nil
}

func (tx *guildTx) guild(id string) (*guildRecord, error) {
	loaded, err := tx.loadGuild(id)
	if err != nil {
		return nil, err
	}
	return loaded.record, nil
}

func (tx *guildTx) loadMembership(playerID string) (*loadedGuildMembership, error) {
	if loaded, ok := tx.memberships[playerID]; ok {
		return loaded, nil
	}
	value, version, err := tx.read(collectionGuildMembers, keyGuildMembership, playerID)
	if err != nil {
		return nil, err
	}
	loaded := &loadedGuildMembership{loadedObject: loadedObject{version: version}}
	var record struct {
		GuildID string `json:"guild_id"`
	}
	if version != "" && json.Unmarshal([]byte(value), &record) == nil {
		loaded.guildID = record.GuildID
	}
	tx.memberships[playerID] = loaded
	return loaded, nil
}

func (tx *guildTx) membershipOf(playerID string) (string, error) {
	loaded, err := tx.loadMembership(playerID)
	if err != nil {
		return "", err
	}
	return loaded.guildID, nil
}

func (tx *guildTx) loadName(key string) (*loadedGuildName, error) {
	if loaded, ok := tx.names[key]; ok {
		return loaded, nil
	}
	value, version, err := tx.read(collectionGuildNames, key, systemUserID)
	if err != nil {
		return nil, err
	}
	loaded := &loadedGuildName{loadedObject: loadedObject{version: version}}
	var record struct {
		GuildID string `json:"guild_id"`
	}
	if version != "" && json.Unmarshal([]byte(value), &record) == nil {
		loaded.guildID = record.GuildID
	}
	tx.names[key] = loaded
	return loaded, nil
}

// nameHolder: the guild holding the name; a name object pointing at a guild that is gone (or renamed)
// is stale and counts as free (overwritten, version-checked, when claimed).
func (tx *guildTx) nameHolder(key string) (string, error) {
	loaded, err := tx.loadName(key)
	if err != nil || loaded.guildID == "" || loaded.deleted {
		return "", err
	}
	holder, err := tx.guild(loaded.guildID)
	if err != nil {
		return "", err
	}
	if holder == nil || guildNameKey(holder.Name) != key {
		return "", nil
	}
	return loaded.guildID, nil
}

func (tx *guildTx) changed(g *guildRecord) {
	if loaded, ok := tx.guilds[g.ID]; ok {
		loaded.dirty = true
	}
}

func (tx *guildTx) created(g *guildRecord) {
	loaded := tx.guilds[g.ID]
	if loaded == nil {
		loaded = &loadedGuild{}
		tx.guilds[g.ID] = loaded
	}
	loaded.record = g
	loaded.dirty = true
	// Claimed with the version nameHolder read (a concurrent create of the same name refuses the commit).
	if name, err := tx.loadName(guildNameKey(g.Name)); err == nil {
		name.guildID = g.ID
		name.deleted = false
		name.dirty = true
	}
}

func (tx *guildTx) disbanded(g *guildRecord) {
	if loaded, ok := tx.guilds[g.ID]; ok {
		loaded.record = nil
		loaded.deleted = true
		loaded.dirty = true
	}
	// The name is freed if it is still this guild's.
	if name, err := tx.loadName(guildNameKey(g.Name)); err == nil && name.guildID == g.ID {
		name.deleted = true
		name.dirty = true
	}
}

func (tx *guildTx) setMembership(playerID, guildID string) error {
	loaded, err := tx.loadMembership(playerID)
	if err != nil {
		return err
	}
	if loaded.guildID != guildID || loaded.version == "" {
		loaded.guildID = guildID
		loaded.dirty = true
	}
	return nil
}

func (tx *guildTx) loadInbox(playerID string) (*loadedGuildInbox, error) {
	if loaded, ok := tx.inboxes[playerID]; ok {
		return loaded, nil
	}
	value, version, err := tx.read(collectionGuildInvites, keyGuildInbox, playerID)
	if err != nil {
		return nil, err
	}
	loaded := &loadedGuildInbox{loadedObject: loadedObject{version: version}}
	if version != "" {
		_ = json.Unmarshal([]byte(value), &loaded.inbox)
	}
	tx.inboxes[playerID] = loaded
	return loaded, nil
}

func (tx *guildTx) inviteSent(playerID string, g *guildRecord, invite guildInvite) error {
	loaded, err := tx.loadInbox(playerID)
	if err != nil {
		return err
	}
	entry := guildInboxEntry{GuildID: g.ID, InvitedBy: invite.InvitedBy, ExpiresAtMs: invite.ExpiresAtMs}
	for i := range loaded.inbox.Invites {
		if loaded.inbox.Invites[i].GuildID == g.ID {
			loaded.inbox.Invites[i] = entry
			loaded.dirty = true
			return nil
		}
	}
	loaded.inbox.Invites = append(loaded.inbox.Invites, entry)
	loaded.dirty = true
	return nil
}

func (tx *guildTx) inviteGone(playerID, guildID string) error {
	loaded, err := tx.loadInbox(playerID)
	if err != nil {
		return err
	}
	kept := loaded.inbox.Invites[:0]
	for _, entry := range loaded.inbox.Invites {
		if entry.GuildID != guildID {
			kept = append(kept, entry)
		}
	}
	if len(kept) != len(loaded.inbox.Invites) {
		loaded.dirty = true
	}
	loaded.inbox.Invites = kept
	return nil
}

func (tx *guildTx) logged(g *guildRecord, entry guildLogEntry) {
	tx.logs = append(tx.logs, guildLogWrite{guildID: g.ID, entry: entry})
}

func guildLogKey(guildID string, seq int64) string {
	return fmt.Sprintf("%s.%012d", guildID, seq)
}

func versionOrAbsent(version string) string {
	if version == "" {
		return "*"
	}
	return version
}

// commit writes every changed object (and tx.extra), all or nothing, each checked against the version it
// was read at. Expired invites of a guild being written are dropped first.
func (tx *guildTx) commit() ([]*api.StorageObjectAck, error) {
	var writes []*runtime.StorageWrite
	var deletes []*runtime.StorageDelete
	for id, loaded := range tx.guilds {
		if !loaded.dirty {
			continue
		}
		if loaded.record == nil {
			if loaded.version != "" {
				deletes = append(deletes, &runtime.StorageDelete{Collection: collectionGuilds, Key: id, UserID: systemUserID, Version: loaded.version})
			}
			continue
		}
		loaded.record.UpdatedAtMs = tx.nowMs
		value, _ := json.Marshal(loaded.record)
		writes = append(writes, &runtime.StorageWrite{Collection: collectionGuilds, Key: id, UserID: systemUserID, Value: string(value),
			Version: versionOrAbsent(loaded.version), PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite})
	}
	for key, loaded := range tx.names {
		if !loaded.dirty {
			continue
		}
		if loaded.deleted {
			if loaded.version != "" {
				deletes = append(deletes, &runtime.StorageDelete{Collection: collectionGuildNames, Key: key, UserID: systemUserID, Version: loaded.version})
			}
			continue
		}
		value, _ := json.Marshal(map[string]string{"guild_id": loaded.guildID})
		writes = append(writes, &runtime.StorageWrite{Collection: collectionGuildNames, Key: key, UserID: systemUserID, Value: string(value),
			Version: versionOrAbsent(loaded.version), PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite})
	}
	for playerID, loaded := range tx.memberships {
		if !loaded.dirty {
			continue
		}
		value, _ := json.Marshal(map[string]string{"guild_id": loaded.guildID})
		writes = append(writes, &runtime.StorageWrite{Collection: collectionGuildMembers, Key: keyGuildMembership, UserID: playerID, Value: string(value),
			Version: versionOrAbsent(loaded.version), PermissionRead: permissionOwnerRead, PermissionWrite: permissionNoWrite})
	}
	for playerID, loaded := range tx.inboxes {
		if !loaded.dirty {
			continue
		}
		if loaded.inbox.Invites == nil {
			loaded.inbox.Invites = []guildInboxEntry{}
		}
		value, _ := json.Marshal(loaded.inbox)
		writes = append(writes, &runtime.StorageWrite{Collection: collectionGuildInvites, Key: keyGuildInbox, UserID: playerID, Value: string(value),
			Version: versionOrAbsent(loaded.version), PermissionRead: permissionOwnerRead, PermissionWrite: permissionNoWrite})
	}
	for _, log := range tx.logs {
		value, _ := json.Marshal(struct {
			GuildID string `json:"guild_id"`
			guildLogEntry
		}{log.guildID, log.entry})
		// "*": an entry is only ever created, never replaced.
		writes = append(writes, &runtime.StorageWrite{Collection: collectionGuildBankLog, Key: guildLogKey(log.guildID, log.entry.Seq), UserID: systemUserID,
			Value: string(value), Version: "*", PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite})
	}
	writes = append(writes, tx.extra...)
	if len(writes) == 0 && len(deletes) == 0 {
		return nil, nil
	}
	acks, _, err := tx.nk.MultiUpdate(tx.ctx, nil, writes, deletes, nil, false)
	return acks, err
}

// --- Requests ----------------------------------------------------------------------------------------

// guildSave: the character save of the player whose gold or inventory a bank operation changes (as in
// crossguard_save_write).
type guildSave struct {
	UserID          string          `json:"user_id"`
	CharacterID     string          `json:"character_id"`
	Record          json.RawMessage `json:"record"`
	ExpectedVersion string          `json:"expected_version"`
	LeaseToken      string          `json:"lease_token"`
}

// expectedBank: the bank the last member's hub server put into their inventory (leave).
type expectedBank struct {
	Gold    int64 `json:"gold"`
	SlotIDs []int `json:"slot_ids"`
}

type guildOpRequest struct {
	Op       string `json:"op"`
	ActorID  string `json:"actor_id"`
	TargetID string `json:"target_id"`
	GuildID  string `json:"guild_id"`
	Name     string `json:"name"`
	Amount   int64  `json:"amount"`
	// deposit_item: the stack (slot id and placement are the bank's).
	Item *bankItem `json:"item"`
	// deposit_item (optional), move_item.
	At           *gridPlacement `json:"at"`
	SlotID       int            `json:"slot_id"`
	ExpectedBank *expectedBank  `json:"expected_bank"`
	RequestID    string         `json:"request_id"`
	ServerID     string         `json:"server_id"`
	Save         *guildSave     `json:"save"`
}

// saveUserFor: whose save a bank operation must carry ("" = none).
func (request guildOpRequest) saveUser() string {
	switch request.Op {
	case "deposit_gold", "withdraw_gold", "deposit_item", "withdraw_item":
		return request.ActorID
	case "give_gold", "give_item":
		return request.TargetID
	case "leave":
		if request.Save != nil {
			return request.ActorID
		}
	}
	return ""
}

func validBankItem(item *bankItem) bool {
	return item != nil && len(item.ItemID) <= maxGuildItemIDLength && item.Rarity >= 0 && item.Rarity <= 5 &&
		item.Width <= maxGuildItemSide && item.Height <= maxGuildItemSide && len(item.Payload) <= maxGuildPayloadBytes
}

func (request *guildOpRequest) validate() error {
	if err := validUserIDs(request.ActorID); err != nil {
		return err
	}
	switch request.Op {
	case "create":
		if len(request.Name) > 256 {
			return errGuildNameLength
		}
	case "invite", "kick", "promote", "demote", "transfer", "give_gold", "give_item":
		// An empty target is the rules' answer (InvalidPlayer / TargetNotInGuild), not an error.
		if request.TargetID != "" && !uuidPattern.MatchString(request.TargetID) {
			return errBadUserID
		}
	case "accept", "decline":
		if !uuidPattern.MatchString(request.GuildID) {
			return errBadGuildID
		}
	case "deposit_item":
		if !validBankItem(request.Item) {
			return errBadGuildItem
		}
		// The bank chooses these.
		request.Item.SlotID = 0
	case "leave", "deposit_gold", "withdraw_gold", "withdraw_item", "move_item":
	default:
		return errBadGuildOp
	}
	if request.ServerID != "" && !serverIDPattern(request.ServerID) {
		return errBadServerID
	}
	if user := request.saveUser(); user != "" {
		if request.Save == nil {
			return errNeedSave
		}
		if !characterIDPattern.MatchString(request.RequestID) {
			return errBadRequestID
		}
		if request.Save.CharacterID == "" {
			request.Save.CharacterID = defaultCharacterID
		}
		if request.Save.UserID != user {
			return errWrongSaveUser
		}
		if !characterIDPattern.MatchString(request.Save.CharacterID) {
			return errBadCharacterID
		}
		if request.Save.ExpectedVersion == "" {
			return errNoVersion
		}
		if request.Save.LeaseToken == "" {
			return errNoLeaseToken
		}
		if err := validateRecord(request.Save.Record, user); err != nil {
			return err
		}
	}
	return nil
}

type guildOpOutcome struct {
	result    string
	guildID   string
	disbanded bool
	item      *bankItem
	payout    *guildPayout
	// Write the carried save (the bank part happened).
	writeSave bool
	// Players whose view changed (besides the actor).
	touched []string
}

// apply runs the rules for one request on the transaction.
func (tx *guildTx) apply(request guildOpRequest, config guildConfig) (guildOpOutcome, error) {
	rules := &guildRules{config: config, world: tx, nowMs: tx.nowMs}
	outcome := guildOpOutcome{result: guildOK}
	actor := request.ActorID
	var err error
	actorGuildID := func() string {
		g, _, _ := rules.guildOf(actor)
		if g != nil {
			return g.ID
		}
		return ""
	}
	before := actorGuildID()
	switch request.Op {
	case "create":
		outcome.result, err = rules.create(request.GuildID, request.Name, actor)
	case "invite":
		outcome.result, err = rules.invite(actor, request.TargetID)
	case "accept":
		outcome.result, err = rules.accept(request.GuildID, actor)
	case "decline":
		outcome.result, err = rules.decline(request.GuildID, actor)
	case "kick":
		outcome.result, err = rules.kick(actor, request.TargetID)
	case "promote":
		outcome.result, err = rules.promote(actor, request.TargetID)
	case "demote":
		outcome.result, err = rules.demote(actor, request.TargetID)
	case "transfer":
		outcome.result, err = rules.transferLeadership(actor, request.TargetID)
	case "leave":
		var payout guildPayout
		var g *guildRecord
		if before != "" {
			g, _ = tx.guild(before)
		}
		if g != nil && len(g.Members) == 1 && (g.Bank.Gold > 0 || len(g.Bank.Items) > 0) && !bankMatches(g.Bank, request.ExpectedBank, request.Save != nil) {
			// The last member takes the bank: the hub must have put exactly this bank into their save.
			outcome.result = guildBankChanged
			break
		}
		outcome.result, outcome.disbanded, payout, err = rules.leave(actor)
		if outcome.disbanded {
			outcome.payout = &payout
			outcome.writeSave = request.Save != nil
		}
	case "deposit_gold":
		outcome.result, err = rules.depositGold(actor, request.Amount)
		outcome.writeSave = true
	case "withdraw_gold":
		outcome.result, err = rules.withdrawGold(actor, request.Amount)
		outcome.writeSave = true
	case "give_gold":
		outcome.result, err = rules.redistributeGold(actor, request.TargetID, request.Amount)
		outcome.writeSave = true
	case "deposit_item":
		var stored bankItem
		outcome.result, stored, err = rules.depositItem(actor, *request.Item, request.At)
		outcome.item = &stored
		outcome.writeSave = true
	case "withdraw_item":
		var taken bankItem
		outcome.result, taken, err = rules.withdrawItem(actor, request.SlotID)
		outcome.item = &taken
		outcome.writeSave = true
	case "give_item":
		var taken bankItem
		outcome.result, taken, err = rules.redistributeItem(actor, request.TargetID, request.SlotID)
		outcome.item = &taken
		outcome.writeSave = true
	case "move_item":
		at := gridPlacement{}
		if request.At != nil {
			at = *request.At
		}
		outcome.result, err = rules.moveBankItem(actor, request.SlotID, at)
	default:
		return outcome, errBadGuildOp
	}
	if err != nil {
		return outcome, err
	}
	if outcome.result != guildOK {
		outcome.item = nil
		outcome.writeSave = false
		return outcome, nil
	}
	outcome.guildID = actorGuildID()
	if outcome.guildID == "" {
		outcome.guildID = before
	}
	if request.Op == "accept" || request.Op == "decline" || request.Op == "create" {
		outcome.guildID = request.GuildID
	}
	if g, _ := tx.guild(outcome.guildID); g != nil {
		if _, err := rules.pruneExpiredInvites(g); err != nil {
			return outcome, err
		}
	}
	if request.TargetID != "" {
		outcome.touched = append(outcome.touched, request.TargetID)
	}
	return outcome, nil
}

// bankMatches: the bank is what the hub expected (gold and the exact set of stacks).
func bankMatches(bank guildBank, expected *expectedBank, hasSave bool) bool {
	if expected == nil || !hasSave || expected.Gold != bank.Gold || len(expected.SlotIDs) != len(bank.Items) {
		return false
	}
	want := map[int]bool{}
	for _, slot := range expected.SlotIDs {
		want[slot] = true
	}
	for _, item := range bank.Items {
		if !want[item.SlotID] {
			return false
		}
	}
	return len(want) == len(bank.Items)
}

// --- State answers -------------------------------------------------------------------------------

type guildInviteView struct {
	GuildID     string `json:"guild_id"`
	GuildName   string `json:"guild_name"`
	InvitedBy   string `json:"invited_by"`
	ExpiresAtMs int64  `json:"expires_at_ms"`
}

type guildPlayerState struct {
	GuildID string            `json:"guild_id"`
	Invites []guildInviteView `json:"invites"`
}

type guildBankView struct {
	Gold       goldText   `json:"gold"`
	Width      int        `json:"width"`
	Height     int        `json:"height"`
	NextSlotID int        `json:"next_slot_id"`
	Items      []bankItem `json:"items"`
}

type guildView struct {
	ID      string          `json:"id"`
	Name    string          `json:"name"`
	Members []guildMember   `json:"members"`
	Invites []guildInvite   `json:"invites"`
	Bank    guildBankView   `json:"bank"`
	BankLog []guildLogEntry `json:"bank_log"`
	LogSeq  int64           `json:"log_seq"`
}

type guildPayoutView struct {
	Gold  goldText   `json:"gold"`
	Items []bankItem `json:"items"`
}

type guildStateResponse struct {
	// The rules' answer (EGuildError name, or BankChanged; "None" = done). Only for crossguard_guild.
	Result    string `json:"result,omitempty"`
	GuildID   string `json:"guild_id,omitempty"`
	Disbanded bool   `json:"disbanded,omitempty"`
	// A bank operation already applied (a retry): nothing changed now.
	Replayed bool `json:"replayed,omitempty"`
	// The player's save version after the operation (bank operations with a save).
	SaveVersion string `json:"save_version,omitempty"`
	// Deposited (as stored), withdrawn or given.
	Item *bankItem `json:"item,omitempty"`
	// The bank the last member took (disbanded).
	Payout *guildPayoutView `json:"payout,omitempty"`
	NowMs  int64            `json:"now_ms"`
	// Each asked-for / involved player: their guild and their live invites.
	Players map[string]guildPlayerState `json:"players"`
	// Every guild named above.
	Guilds map[string]guildView `json:"guilds"`
}

func makeGuildView(g *guildRecord, nowMs int64) guildView {
	view := guildView{ID: g.ID, Name: g.Name, Members: append([]guildMember{}, g.Members...), Invites: []guildInvite{},
		Bank:    guildBankView{Gold: goldText(g.Bank.Gold), Width: g.Bank.Width, Height: g.Bank.Height, NextSlotID: g.Bank.NextSlotID, Items: append([]bankItem{}, g.Bank.Items...)},
		BankLog: append([]guildLogEntry{}, g.BankLog...), LogSeq: g.LogSeq}
	for _, invite := range g.Invites {
		if nowMs < invite.ExpiresAtMs {
			view.Invites = append(view.Invites, invite)
		}
	}
	return view
}

// state builds the answer for the given players from the transaction's objects (after the operation).
func (tx *guildTx) state(userIDs []string, config guildConfig) (guildStateResponse, error) {
	rules := &guildRules{config: config, world: tx, nowMs: tx.nowMs}
	response := guildStateResponse{NowMs: tx.nowMs, Players: map[string]guildPlayerState{}, Guilds: map[string]guildView{}}
	for _, userID := range userIDs {
		if _, done := response.Players[userID]; done || userID == "" {
			continue
		}
		state := guildPlayerState{Invites: []guildInviteView{}}
		g, _, err := rules.guildOf(userID)
		if err != nil {
			return response, err
		}
		if g != nil {
			state.GuildID = g.ID
			if _, seen := response.Guilds[g.ID]; !seen {
				response.Guilds[g.ID] = makeGuildView(g, tx.nowMs)
			}
		}
		inbox, err := tx.loadInbox(userID)
		if err != nil {
			return response, err
		}
		for _, entry := range inbox.inbox.Invites {
			inviting, err := tx.guild(entry.GuildID)
			if err != nil {
				return response, err
			}
			// The guild's own list decides (the inbox is only an index).
			if inviting == nil {
				continue
			}
			invite := inviting.invite(userID)
			if invite == nil || tx.nowMs >= invite.ExpiresAtMs {
				continue
			}
			state.Invites = append(state.Invites, guildInviteView{GuildID: inviting.ID, GuildName: inviting.Name, InvitedBy: invite.InvitedBy, ExpiresAtMs: invite.ExpiresAtMs})
		}
		sort.Slice(state.Invites, func(i, j int) bool { return state.Invites[i].ExpiresAtMs < state.Invites[j].ExpiresAtMs })
		response.Players[userID] = state
	}
	return response, nil
}

// --- The carried save ------------------------------------------------------------------------------

// addSave puts the player's save and its lease guard into the transaction (crossguard_save_write's
// checks: the lease is held, the save is at the expected version).
func (tx *guildTx) addSave(save *guildSave) error {
	guard, err := leaseGuard(tx.ctx, tx.nk, save.UserID, save.CharacterID, save.LeaseToken)
	if err != nil {
		return err
	}
	tx.extra = append(tx.extra, &runtime.StorageWrite{Collection: collectionCharacters, Key: save.CharacterID, UserID: save.UserID,
		Value: string(save.Record), Version: save.ExpectedVersion, PermissionRead: permissionOwnerRead, PermissionWrite: permissionNoWrite}, guard)
	return nil
}

// appliedRequest: the marker of a bank operation already applied ("" version: not applied).
func appliedRequest(ctx context.Context, nk runtime.NakamaModule, requestID string) (bool, error) {
	objects, err := nk.StorageRead(ctx, []*runtime.StorageRead{{Collection: collectionGuildRequests, Key: requestID, UserID: systemUserID}})
	if err != nil {
		return false, err
	}
	return len(objects) > 0, nil
}

func characterVersion(ctx context.Context, nk runtime.NakamaModule, userID, characterID string) (string, error) {
	objects, err := nk.StorageRead(ctx, []*runtime.StorageRead{{Collection: collectionCharacters, Key: characterID, UserID: userID}})
	if err != nil {
		return "", err
	}
	if len(objects) == 0 {
		return "*", nil
	}
	return objects[0].GetVersion(), nil
}

// --- RPCs ----------------------------------------------------------------------------------------

var guildSettings = defaultGuildConfig()

// RpcGuildFn: crossguard_guild. {"op", "actor_id", "target_id", "guild_id", "name", "amount", "item", "at",
// "slot_id", "expected_bank", "request_id", "server_id", "save"} -> guildStateResponse. Rule refusals are
// answers (200, "result" != "None"), not errors. See docs/backend-setup.md for each op's fields.
func RpcGuildFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	if len(payload) > MaxRecordBytes+65536 {
		return "", errRecordTooLarge
	}
	var request guildOpRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if err := request.validate(); err != nil {
		return "", err
	}
	if request.TargetID != "" && request.Op == "invite" {
		// An unknown player is an invalid player (the rules' answer, not an error).
		if users, err := nk.UsersGetId(ctx, []string{request.TargetID}, nil); err == nil && len(users) == 0 {
			return marshalGuildAnswer(guildStateResponse{Result: guildInvalidPlayer, NowMs: nowMs(), Players: map[string]guildPlayerState{}, Guilds: map[string]guildView{}})
		}
	}
	if request.Save != nil && request.saveUser() != "" {
		if err := requireUser(ctx, logger, nk, request.Save.UserID); err != nil {
			return "", err
		}
		applied, err := appliedRequest(ctx, nk, request.RequestID)
		if err != nil {
			logger.Error("guild %s: request lookup failed: %v", request.Op, err)
			return "", errStorage
		}
		if applied {
			return guildReplay(ctx, logger, nk, request)
		}
	}
	if request.Op == "create" {
		return createGuild(ctx, logger, nk, request)
	}
	response, err := runGuildOp(ctx, logger, nk, request)
	if err != nil {
		return "", err
	}
	return marshalGuildAnswer(response)
}

func marshalGuildAnswer(response guildStateResponse) (string, error) {
	out, err := json.Marshal(response)
	if err != nil {
		return "", errStorage
	}
	return string(out), nil
}

// guildReplay: the request was applied before (its answer was lost): the current state and save version.
func guildReplay(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, request guildOpRequest) (string, error) {
	response, err := guildReplayResponse(ctx, nk, request)
	if err != nil {
		logger.Error("guild %s replay: %v", request.Op, err)
		return "", errStorage
	}
	logger.Info("guild %s by %s: request %s was already applied (replayed)", request.Op, request.ActorID, request.RequestID)
	return marshalGuildAnswer(response)
}

// runGuildOp: the transaction loop (rules, answer, commit; again on a concurrent change).
func runGuildOp(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, request guildOpRequest) (guildStateResponse, error) {
	for attempt := 1; attempt <= guildOpAttempts; attempt++ {
		tx := newGuildTx(ctx, nk)
		outcome, err := tx.apply(request, guildSettings)
		var response guildStateResponse
		if err == nil && outcome.result == guildOK && outcome.writeSave {
			err = tx.addSave(request.Save)
			if err == nil {
				marker, _ := json.Marshal(map[string]any{"guild_id": outcome.guildID, "op": request.Op, "actor_id": request.ActorID, "at_ms": tx.nowMs})
				tx.extra = append(tx.extra, &runtime.StorageWrite{Collection: collectionGuildRequests, Key: request.RequestID, UserID: systemUserID,
					Value: string(marker), Version: "*", PermissionRead: permissionNoRead, PermissionWrite: permissionNoWrite})
			}
		}
		if err == nil {
			involved := append([]string{request.ActorID}, outcome.touched...)
			if g, _ := tx.guild(outcome.guildID); g != nil {
				for _, member := range g.Members {
					involved = append(involved, member.UserID)
				}
			}
			response, err = tx.state(involved, guildSettings)
		}
		var acks []*api.StorageObjectAck
		if err == nil && outcome.result == guildOK {
			// A refusal by the rules writes nothing (not even the carried save).
			acks, err = tx.commit()
		}
		if err != nil {
			if isRejected(err) {
				if outcome.writeSave {
					// The save or its lease may be what changed: then retrying cannot help.
					if stop := guildSaveStillValid(ctx, nk, request); stop != nil {
						if errors.Is(stop, errGuildReplayed) {
							replay, replayErr := guildReplayResponse(ctx, nk, request)
							if replayErr != nil {
								return response, errStorage
							}
							return replay, nil
						}
						logger.Warn("guild %s by %s: %v", request.Op, request.ActorID, stop)
						return response, stop
					}
				}
				backOff(attempt)
				continue
			}
			var runtimeErr *runtime.Error
			if errors.As(err, &runtimeErr) {
				return response, err
			}
			logger.Error("guild %s by %s failed: %v", request.Op, request.ActorID, err)
			return response, errStorage
		}
		response.Result = outcome.result
		response.GuildID = outcome.guildID
		response.Disbanded = outcome.disbanded
		response.Item = outcome.item
		if outcome.payout != nil {
			response.Payout = &guildPayoutView{Gold: goldText(outcome.payout.Gold), Items: outcome.payout.Items}
		}
		if outcome.result == guildOK && outcome.writeSave {
			for _, ack := range acks {
				if ack.GetCollection() == collectionCharacters && ack.GetUserId() == request.Save.UserID {
					response.SaveVersion = ack.GetVersion()
				}
			}
		}
		if outcome.result == guildOK {
			logger.Debug("guild %s by %s on %s: guild %s", request.Op, request.ActorID, request.ServerID, outcome.guildID)
			mirrorGuildGroup(ctx, logger, nk, tx, outcome.guildID)
		}
		return response, nil
	}
	return guildStateResponse{}, errGuildContended
}

var errGuildReplayed = errors.New("request already applied")

// guildSaveStillValid: nil if the carried save could still be written (a guild change refused the
// transaction: retry); else why not.
func guildSaveStillValid(ctx context.Context, nk runtime.NakamaModule, request guildOpRequest) error {
	if applied, err := appliedRequest(ctx, nk, request.RequestID); err == nil && applied {
		return errGuildReplayed
	}
	if _, err := leaseGuard(ctx, nk, request.Save.UserID, request.Save.CharacterID, request.Save.LeaseToken); err != nil {
		return err
	}
	version, err := characterVersion(ctx, nk, request.Save.UserID, request.Save.CharacterID)
	if err != nil {
		return errStorage
	}
	if version != request.Save.ExpectedVersion {
		return errGuildSaveStale
	}
	return nil
}

func guildReplayResponse(ctx context.Context, nk runtime.NakamaModule, request guildOpRequest) (guildStateResponse, error) {
	tx := newGuildTx(ctx, nk)
	response, err := tx.state([]string{request.ActorID, request.TargetID}, guildSettings)
	if err != nil {
		return response, err
	}
	response.SaveVersion, err = characterVersion(ctx, nk, request.Save.UserID, request.Save.CharacterID)
	response.Result = guildOK
	response.Replayed = true
	return response, err
}

// createGuild: the guild id is its Nakama group's id, so the group is created first (after the rules'
// checks) and deleted again if the guild is not stored.
func createGuild(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, request guildOpRequest) (string, error) {
	// The rules first (a refused create makes no group). "pending" is no guild id in use.
	probe := request
	probe.GuildID = "pending"
	tx := newGuildTx(ctx, nk)
	outcome, err := tx.apply(probe, guildSettings)
	if err != nil {
		logger.Error("guild create by %s failed: %v", request.ActorID, err)
		return "", errStorage
	}
	if outcome.result != guildOK {
		response, err := tx.state([]string{request.ActorID}, guildSettings)
		if err != nil {
			return "", errStorage
		}
		response.Result = outcome.result
		return marshalGuildAnswer(response)
	}
	name := trimGuildName(request.Name)
	group, err := createGuildGroup(ctx, logger, nk, name, request.ActorID)
	if err != nil {
		if errors.Is(err, errGroupNameHeld) {
			return marshalGuildAnswer(guildStateResponse{Result: guildNameTaken, NowMs: nowMs(), Players: map[string]guildPlayerState{}, Guilds: map[string]guildView{}})
		}
		logger.Error("guild create by %s: group create failed: %v", request.ActorID, err)
		return "", errStorage
	}
	request.GuildID = group
	response, err := runGuildOp(ctx, logger, nk, request)
	if err != nil || response.Result != guildOK {
		if deleteErr := nk.GroupDelete(ctx, group); deleteErr != nil {
			logger.Warn("guild create by %s refused; its group %s could not be deleted: %v", request.ActorID, group, deleteErr)
		}
	}
	if err != nil {
		return "", err
	}
	if response.Result == guildOK {
		logger.Info("guild %s (%q) founded by %s", group, name, request.ActorID)
	}
	return marshalGuildAnswer(response)
}

var errGroupNameHeld = errors.New("a group with this name exists")

// createGuildGroup creates the guild's group (closed, founder as superadmin). A group under the same
// name with no guild behind it, older than staleGroupAgeSec, is a leftover: deleted, then tried again.
func createGuildGroup(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, name, founderID string) (string, error) {
	for attempt := 0; attempt < 2; attempt++ {
		group, err := nk.GroupCreate(ctx, founderID, name, founderID, "", "", "", false, map[string]interface{}{"crossguard_guild": true}, guildSettings.MaxMembers)
		if err == nil {
			return group.GetId(), nil
		}
		if !errors.Is(err, runtime.ErrGroupNameInUse) && !strings.Contains(strings.ToLower(err.Error()), "name in use") {
			return "", err
		}
		groups, _, listErr := nk.GroupsList(ctx, name, "", nil, nil, 10, "")
		if listErr != nil {
			return "", listErr
		}
		removed := false
		for _, existing := range groups {
			if !strings.EqualFold(existing.GetName(), name) {
				continue
			}
			tx := newGuildTx(ctx, nk)
			record, readErr := tx.guild(existing.GetId())
			if readErr != nil {
				return "", readErr
			}
			age := int64(0)
			if existing.GetCreateTime() != nil {
				age = now().Unix() - existing.GetCreateTime().GetSeconds()
			}
			if record != nil || age < staleGroupAgeSec {
				return "", errGroupNameHeld
			}
			logger.Warn("guild group %s (%q) has no guild: a leftover, deleted", existing.GetId(), existing.GetName())
			if err := nk.GroupDelete(ctx, existing.GetId()); err != nil {
				return "", err
			}
			removed = true
		}
		if !removed {
			return "", errGroupNameHeld
		}
	}
	return "", errGroupNameHeld
}

// Nakama group states for the ranks.
func groupStateOf(rank string) int {
	switch rank {
	case rankLeader:
		return 0
	case rankOfficer:
		return 1
	}
	return 2
}

// mirrorGuildGroup brings the guild's Nakama group in line with the stored guild (deleted with it).
// Best effort: failures are logged; the next change of the guild repairs them.
func mirrorGuildGroup(ctx context.Context, logger runtime.Logger, nk runtime.NakamaModule, tx *guildTx, guildID string) {
	loaded, ok := tx.guilds[guildID]
	if !ok || guildID == "" {
		return
	}
	if loaded.deleted {
		if err := nk.GroupDelete(ctx, guildID); err != nil {
			logger.Warn("guild %s disbanded; its group could not be deleted: %v", guildID, err)
		}
		return
	}
	if loaded.record == nil || !loaded.dirty {
		return
	}
	if err := syncGuildGroup(ctx, nk, loaded.record); err != nil {
		logger.Warn("guild %s: group mirror not updated (repaired on its next change): %v", guildID, err)
	}
}

func syncGuildGroup(ctx context.Context, nk runtime.NakamaModule, g *guildRecord) error {
	current := map[string]int{}
	cursor := ""
	for page := 0; page < 10; page++ {
		users, next, err := nk.GroupUsersList(ctx, g.ID, 100, nil, cursor)
		if err != nil {
			return err
		}
		for _, user := range users {
			if user.GetUser() != nil && user.GetState() != nil {
				current[user.GetUser().GetId()] = int(user.GetState().GetValue())
			}
		}
		if next == "" {
			break
		}
		cursor = next
	}
	desired := map[string]int{}
	var add []string
	for _, member := range g.Members {
		desired[member.UserID] = groupStateOf(member.Rank)
		if state, ok := current[member.UserID]; !ok || state > 2 {
			// Not in the group (or only a join request): added as a member, then promoted below.
			add = append(add, member.UserID)
			current[member.UserID] = 2
		}
	}
	if len(add) > 0 {
		if err := nk.GroupUsersAdd(ctx, "", g.ID, add); err != nil {
			return err
		}
	}
	// Promotions first: the group always keeps a superadmin (the new Leader before the old one steps down).
	for userID, want := range desired {
		for state := current[userID]; state > want; state-- {
			if err := nk.GroupUsersPromote(ctx, "", g.ID, []string{userID}); err != nil {
				return err
			}
		}
	}
	for userID, want := range desired {
		for state := current[userID]; state < want; state++ {
			if err := nk.GroupUsersDemote(ctx, "", g.ID, []string{userID}); err != nil {
				return err
			}
		}
	}
	var kick []string
	for userID := range current {
		if _, ok := desired[userID]; !ok {
			kick = append(kick, userID)
		}
	}
	if len(kick) > 0 {
		sort.Strings(kick)
		return nk.GroupUsersKick(ctx, "", g.ID, kick)
	}
	return nil
}

type guildGetRequest struct {
	UserIDs []string `json:"user_ids"`
}

// RpcGuildGetFn: crossguard_guild_get. {"user_ids": [...] (<= 256)} -> guildStateResponse for them (hub
// servers: on a player's arrival and every few seconds for everyone on the server). Reads only.
func RpcGuildGetFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request guildGetRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if len(request.UserIDs) > maxPartyStateUserIDs {
		return "", errTooManyUsers
	}
	if err := validUserIDs(request.UserIDs...); err != nil {
		return "", err
	}
	tx := newGuildTx(ctx, nk)
	response, err := tx.state(request.UserIDs, guildSettings)
	if err != nil {
		logger.Error("guild get failed: %v", err)
		return "", errStorage
	}
	return marshalGuildAnswer(response)
}

type guildLogRequest struct {
	GuildID string `json:"guild_id"`
	// Entries before this sequence number (0: from the newest).
	BeforeSeq int64 `json:"before_seq"`
	Limit     int   `json:"limit"`
}

type guildLogResponse struct {
	GuildID string `json:"guild_id"`
	// Newest first.
	Entries []guildLogEntry `json:"entries"`
	LogSeq  int64           `json:"log_seq"`
}

// RpcGuildLogFn: crossguard_guild_log. {"guild_id", "before_seq", "limit" (1-100)} -> {"entries" (newest
// first), "log_seq"}: the append-only bank history (guild_bank_log), also of a disbanded guild when
// before_seq is given.
func RpcGuildLogFn(ctx context.Context, logger runtime.Logger, db *sql.DB, nk runtime.NakamaModule, payload string) (string, error) {
	if err := requireServerCall(ctx); err != nil {
		return "", err
	}
	var request guildLogRequest
	if err := decodePayload(payload, &request); err != nil {
		return "", err
	}
	if !uuidPattern.MatchString(request.GuildID) {
		return "", errBadGuildID
	}
	if request.Limit <= 0 || request.Limit > maxGuildLogRead {
		request.Limit = maxGuildLogRead
	}
	response := guildLogResponse{GuildID: request.GuildID, Entries: []guildLogEntry{}}
	tx := newGuildTx(ctx, nk)
	record, err := tx.guild(request.GuildID)
	if err != nil {
		logger.Error("guild log read failed: %v", err)
		return "", errStorage
	}
	newest := request.BeforeSeq - 1
	if record != nil {
		response.LogSeq = record.LogSeq
		if request.BeforeSeq <= 0 || request.BeforeSeq > record.LogSeq+1 {
			newest = record.LogSeq
		}
	}
	var reads []*runtime.StorageRead
	for seq := newest; seq > 0 && len(reads) < request.Limit; seq-- {
		reads = append(reads, &runtime.StorageRead{Collection: collectionGuildBankLog, Key: guildLogKey(request.GuildID, seq), UserID: systemUserID})
	}
	if len(reads) > 0 {
		objects, err := nk.StorageRead(ctx, reads)
		if err != nil {
			logger.Error("guild log read failed: %v", err)
			return "", errStorage
		}
		for _, object := range objects {
			var entry guildLogEntry
			if json.Unmarshal([]byte(object.GetValue()), &entry) == nil {
				response.Entries = append(response.Entries, entry)
			}
		}
		sort.Slice(response.Entries, func(i, j int) bool { return response.Entries[i].Seq > response.Entries[j].Seq })
	}
	out, _ := json.Marshal(response)
	return string(out), nil
}
