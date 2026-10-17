package main

import (
	"strings"
)

// Guild rules (docs/guild-rules.md): a Go copy of CrossguardRules FGuildRegistry
// (Source/CrossguardRules/Private/Guild.cpp) for the Nakama backend (guilds.go). Same checks, in the
// same order, with the same answers (EGuildError names). KEEP BOTH IN STEP: a change to one is a change
// to the other, and guild_rules_test.go mirrors the C++ unit test (Tests/CrossguardRules/TestMain.cpp,
// TestGuild) case for case.
//
// Differences that are not rules: time is Unix milliseconds (the C++ takes seconds on any clock); the
// rules read and change guilds through a guildWorld (storage in guilds.go, maps in the tests) instead of
// owning them; ranks and bank actions are stored by their C++ enum names.

// guildConfig: FGuildConfig (CrossguardRules/Config.h). Every number is an assumption (placeholder).
type guildConfig struct {
	MaxMembers     int
	MinNameLength  int
	MaxNameLength  int
	InviteExpiryMs int64
	BankWidth      int
	BankHeight     int
	BankLogSize    int
}

func defaultGuildConfig() guildConfig {
	return guildConfig{MaxMembers: 50, MinNameLength: 3, MaxNameLength: 24, InviteExpiryMs: 300_000, BankWidth: 10, BankHeight: 30, BankLogSize: 100}
}

// EGuildError names (the game maps them onto ECrossguardGuildResult).
const (
	guildOK                 = "None"
	guildInvalidPlayer      = "InvalidPlayer"
	guildInvalidGuildID     = "InvalidGuildId"
	guildIDTaken            = "GuildIdTaken"
	guildInvalidName        = "InvalidName"
	guildNameTaken          = "NameTaken"
	guildUnknownGuild       = "UnknownGuild"
	guildNotInGuild         = "NotInGuild"
	guildAlreadyInGuild     = "AlreadyInGuild"
	guildTargetNotInGuild   = "TargetNotInGuild"
	guildNotPermitted       = "NotPermitted"
	guildCannotTargetSelf   = "CannotTargetSelf"
	guildAlreadyRank        = "AlreadyRank"
	guildFull               = "GuildFull"
	guildAlreadyInvited     = "AlreadyInvited"
	guildNoInvite           = "NoInvite"
	guildInviteExpired      = "InviteExpired"
	guildLeaderMustTransfer = "LeaderMustTransfer"
	guildInvalidAmount      = "InvalidAmount"
	guildNotEnoughGold      = "NotEnoughGold"
	guildGoldOverflow       = "GoldOverflow"
	guildInvalidItem        = "InvalidItem"
	guildUnknownItem        = "UnknownItem"
	guildNoRoom             = "NoRoom"
	guildInvalidData        = "InvalidData"
)

// EGuildRank names.
const (
	rankMember  = "Member"
	rankOfficer = "Officer"
	rankLeader  = "Leader"
)

// EGuildPermission.
type guildPermission int

const (
	permInvite guildPermission = iota
	permManageRanks
	permDeposit
	permWithdraw
	permRedistribute
	permManageBank
)

// EGuildBankAction names.
const (
	actionDepositGold      = "DepositGold"
	actionWithdrawGold     = "WithdrawGold"
	actionRedistributeGold = "RedistributeGold"
	actionDepositItem      = "DepositItem"
	actionWithdrawItem     = "WithdrawItem"
	actionRedistributeItem = "RedistributeItem"
)

const maxInt64 = int64(^uint64(0) >> 1)

// --- Records (stored as JSON in guilds/<id>, guilds.go) ------------------------------------------------

type guildMember struct {
	UserID string `json:"user_id"`
	Rank   string `json:"rank"`
}

type guildInvite struct {
	UserID      string `json:"user_id"`
	InvitedBy   string `json:"invited_by"`
	ExpiresAtMs int64  `json:"expires_at_ms"`
}

type gridPlacement struct {
	X       int  `json:"x"`
	Y       int  `json:"y"`
	Rotated bool `json:"rotated"`
}

// bankItem: FGuildBankItem plus its place in the bank grid.
type bankItem struct {
	SlotID int    `json:"slot_id"`
	ItemID string `json:"item_id"`
	Rarity int    `json:"rarity"`
	Count  int    `json:"count"`
	// Unrotated size.
	Width  int `json:"width"`
	Height int `json:"height"`
	gridPlacement
	// The game's own item state (affixes...), round-tripped, never read here.
	Payload string `json:"payload,omitempty"`
}

type guildBank struct {
	Gold       int64      `json:"gold"`
	Width      int        `json:"width"`
	Height     int        `json:"height"`
	NextSlotID int        `json:"next_slot_id"`
	Items      []bankItem `json:"items"`
}

type guildLogEntry struct {
	// 1, 2, 3... per guild: the append-only log's key (guild_bank_log, guilds.go).
	Seq         int64  `json:"seq"`
	PlayerID    string `json:"player_id"`
	Action      string `json:"action"`
	ItemID      string `json:"item_id,omitempty"`
	Rarity      int    `json:"rarity"`
	Count       int    `json:"count"`
	Gold        int64  `json:"gold"`
	RecipientID string `json:"recipient_id,omitempty"`
	AtMs        int64  `json:"at_ms"`
}

type guildRecord struct {
	ID   string `json:"id"`
	Name string `json:"name"`
	// Join order; the founder first.
	Members []guildMember `json:"members"`
	Invites []guildInvite `json:"invites"`
	Bank    guildBank     `json:"bank"`
	// Newest BankLogSize entries, oldest first (the whole history: guild_bank_log).
	BankLog     []guildLogEntry `json:"bank_log"`
	LogSeq      int64           `json:"log_seq"`
	UpdatedAtMs int64           `json:"updated_at_ms"`
}

func (g *guildRecord) member(userID string) *guildMember {
	for i := range g.Members {
		if g.Members[i].UserID == userID {
			return &g.Members[i]
		}
	}
	return nil
}

func (g *guildRecord) leaderID() string {
	for _, member := range g.Members {
		if member.Rank == rankLeader {
			return member.UserID
		}
	}
	return ""
}

func (g *guildRecord) invite(userID string) *guildInvite {
	for i := range g.Invites {
		if g.Invites[i].UserID == userID {
			return &g.Invites[i]
		}
	}
	return nil
}

func (g *guildRecord) removeInvite(userID string) {
	for i := range g.Invites {
		if g.Invites[i].UserID == userID {
			g.Invites = append(g.Invites[:i], g.Invites[i+1:]...)
			return
		}
	}
}

func (g *guildRecord) removeMember(userID string) {
	for i := range g.Members {
		if g.Members[i].UserID == userID {
			g.Members = append(g.Members[:i], g.Members[i+1:]...)
			return
		}
	}
}

func (b *guildBank) find(slotID int) *bankItem {
	for i := range b.Items {
		if b.Items[i].SlotID == slotID {
			return &b.Items[i]
		}
	}
	return nil
}

// --- Free functions (GuildRankCan, CanKick, names) ---------------------------------------------------

func rankLevel(rank string) int {
	switch rank {
	case rankLeader:
		return 2
	case rankOfficer:
		return 1
	}
	return 0
}

// guildRankCan: GuildRankCan.
func guildRankCan(rank string, permission guildPermission) bool {
	switch permission {
	case permDeposit:
		return true
	case permManageRanks:
		return rank == rankLeader
	case permInvite, permWithdraw, permRedistribute, permManageBank:
		return rank == rankOfficer || rank == rankLeader
	}
	return false
}

// canKick: CanKick (Leader kicks anyone but a Leader; Officer kicks Members).
func canKick(actor, target string) bool {
	if actor == rankLeader {
		return target != rankLeader
	}
	return actor == rankOfficer && target == rankMember
}

// trimGuildName: TrimGuildName (spaces only).
func trimGuildName(name string) string {
	return strings.Trim(name, " ")
}

func isGuildNameChar(c byte) bool {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-'
}

// validateGuildName: ValidateGuildName (bytes, so any non-ASCII byte is refused).
func validateGuildName(config guildConfig, name string) string {
	trimmed := trimGuildName(name)
	if len(trimmed) < config.MinNameLength || len(trimmed) > config.MaxNameLength || trimmed == "" {
		return guildInvalidName
	}
	for i := 0; i < len(trimmed); i++ {
		if !isGuildNameChar(trimmed[i]) {
			return guildInvalidName
		}
	}
	return guildOK
}

// guildNameKey: the case-insensitive name index's key (NameKey).
func guildNameKey(name string) string {
	key := []byte(trimGuildName(name))
	for i, c := range key {
		if c >= 'A' && c <= 'Z' {
			key[i] = c - 'A' + 'a'
		}
	}
	return string(key)
}

// --- Bank grid (FInventoryGrid) --------------------------------------------------------------------

func footprint(width, height int, rotated bool) (int, int) {
	if rotated {
		return height, width
	}
	return width, height
}

// canPlace: FInventoryGrid::CanPlace (inside the grid, every square free or ignoreSlot's).
func (b *guildBank) canPlace(width, height int, at gridPlacement, ignoreSlot int) bool {
	w, h := footprint(width, height, at.Rotated)
	// Compared without adding to At (no overflow on huge positions), as in C++.
	if w <= 0 || h <= 0 || at.X < 0 || at.Y < 0 || at.X > b.Width-w || at.Y > b.Height-h {
		return false
	}
	for _, item := range b.Items {
		if item.SlotID == ignoreSlot {
			continue
		}
		iw, ih := footprint(item.Width, item.Height, item.Rotated)
		if at.X < item.X+iw && item.X < at.X+w && at.Y < item.Y+ih && item.Y < at.Y+h {
			return false
		}
	}
	return true
}

// findFreeSpot: FInventoryGrid::FindFreeSpot with rotation (rows top to bottom, left to right;
// unrotated before rotated; squares never rotated).
func (b *guildBank) findFreeSpot(width, height int) (gridPlacement, bool) {
	for y := 0; y < b.Height; y++ {
		for x := 0; x < b.Width; x++ {
			for _, rotated := range []bool{false, true} {
				if rotated && width == height {
					continue
				}
				at := gridPlacement{X: x, Y: y, Rotated: rotated}
				if b.canPlace(width, height, at, 0) {
					return at, true
				}
			}
		}
	}
	return gridPlacement{}, false
}

// itemAt: FInventoryGrid::ItemAt (0: empty or outside).
func (b *guildBank) itemAt(x, y int) int {
	if x < 0 || y < 0 || x >= b.Width || y >= b.Height {
		return 0
	}
	for _, item := range b.Items {
		w, h := footprint(item.Width, item.Height, item.Rotated)
		if x >= item.X && x < item.X+w && y >= item.Y && y < item.Y+h {
			return item.SlotID
		}
	}
	return 0
}

func isValidBankItem(item bankItem) bool {
	return item.ItemID != "" && item.Count > 0 && item.Width > 0 && item.Height > 0
}

// --- The world the rules act on ------------------------------------------------------------------

// guildWorld: what the rules read and change (FGuildRegistry's maps). guilds.go keeps it in storage,
// the tests in maps. A record returned by guild() may be changed in place; changed() records that.
type guildWorld interface {
	// nil: no such guild.
	guild(id string) (*guildRecord, error)
	// The guild the player's membership names ("": none). May be stale; the rules check the roster.
	membershipOf(playerID string) (string, error)
	// The guild holding a name key ("": free).
	nameHolder(key string) (string, error)
	changed(g *guildRecord)
	// A new guild: stored, and its name claimed.
	created(g *guildRecord)
	// Disbanded: deleted, and its name free.
	disbanded(g *guildRecord)
	setMembership(playerID, guildID string) error
	// The player's invite inbox (an index for their view: the guild's own list decides).
	inviteSent(playerID string, g *guildRecord, invite guildInvite) error
	inviteGone(playerID, guildID string) error
	// A bank log entry (also kept in the record): appended to the full history.
	logged(g *guildRecord, entry guildLogEntry)
}

type guildRules struct {
	config guildConfig
	world  guildWorld
	nowMs  int64
}

// guildPayout: what the last member gets when the guild disbands (FGuildLeaveResult).
type guildPayout struct {
	Gold  int64      `json:"gold"`
	Items []bankItem `json:"items"`
}

// guildOf: the guild the player is really in (membership index and roster agree), else nil.
func (r *guildRules) guildOf(playerID string) (*guildRecord, *guildMember, error) {
	if playerID == "" {
		return nil, nil, nil
	}
	id, err := r.world.membershipOf(playerID)
	if err != nil || id == "" {
		return nil, nil, err
	}
	g, err := r.world.guild(id)
	if err != nil || g == nil {
		return nil, nil, err
	}
	member := g.member(playerID)
	if member == nil {
		return nil, nil, nil
	}
	return g, member, nil
}

// actorGuild: ActorGuild (NotInGuild when the actor is in none).
func (r *guildRules) actorGuild(actorID string) (*guildRecord, *guildMember, string, error) {
	g, member, err := r.guildOf(actorID)
	if err != nil {
		return nil, nil, "", err
	}
	if g == nil {
		return nil, nil, guildNotInGuild, nil
	}
	return g, member, guildOK, nil
}

// checkMemberTarget: CheckMemberTarget.
func (r *guildRules) checkMemberTarget(actorID, targetID string) (*guildRecord, *guildMember, *guildMember, string, error) {
	g, actor, result, err := r.actorGuild(actorID)
	if err != nil || result != guildOK {
		return nil, nil, nil, result, err
	}
	if targetID == actorID {
		return nil, nil, nil, guildCannotTargetSelf, nil
	}
	target := g.member(targetID)
	if target == nil {
		return nil, nil, nil, guildTargetNotInGuild, nil
	}
	return g, actor, target, guildOK, nil
}

func (r *guildRules) log(g *guildRecord, entry guildLogEntry) {
	g.LogSeq++
	entry.Seq = g.LogSeq
	entry.AtMs = r.nowMs
	g.BankLog = append(g.BankLog, entry)
	keep := r.config.BankLogSize
	if keep < 0 {
		keep = 0
	}
	if len(g.BankLog) > keep {
		g.BankLog = append([]guildLogEntry{}, g.BankLog[len(g.BankLog)-keep:]...)
	}
	r.world.logged(g, entry)
}

// create: FGuildRegistry::Create.
func (r *guildRules) create(guildID, name, founderID string) (string, error) {
	if founderID == "" {
		return guildInvalidPlayer, nil
	}
	if guildID == "" {
		return guildInvalidGuildID, nil
	}
	existing, err := r.world.guild(guildID)
	if err != nil {
		return "", err
	}
	if existing != nil {
		return guildIDTaken, nil
	}
	if validateGuildName(r.config, name) != guildOK {
		return guildInvalidName, nil
	}
	holder, err := r.world.nameHolder(guildNameKey(name))
	if err != nil {
		return "", err
	}
	if holder != "" {
		return guildNameTaken, nil
	}
	current, _, err := r.guildOf(founderID)
	if err != nil {
		return "", err
	}
	if current != nil {
		return guildAlreadyInGuild, nil
	}
	g := &guildRecord{ID: guildID, Name: trimGuildName(name), Members: []guildMember{{UserID: founderID, Rank: rankLeader}},
		Invites: []guildInvite{}, BankLog: []guildLogEntry{},
		Bank: guildBank{Width: r.config.BankWidth, Height: r.config.BankHeight, NextSlotID: 1, Items: []bankItem{}}}
	r.world.created(g)
	return guildOK, r.world.setMembership(founderID, guildID)
}

// invite: FGuildRegistry::Invite.
func (r *guildRules) invite(actorID, inviteeID string) (string, error) {
	g, actor, result, err := r.actorGuild(actorID)
	if err != nil || result != guildOK {
		return result, err
	}
	if !guildRankCan(actor.Rank, permInvite) {
		return guildNotPermitted, nil
	}
	if inviteeID == "" {
		return guildInvalidPlayer, nil
	}
	inviteeGuild, _, err := r.guildOf(inviteeID)
	if err != nil {
		return "", err
	}
	if inviteeGuild != nil {
		return guildAlreadyInGuild, nil
	}
	if len(g.Members) >= r.config.MaxMembers {
		return guildFull, nil
	}
	fresh := guildInvite{UserID: inviteeID, InvitedBy: actorID, ExpiresAtMs: r.nowMs + r.config.InviteExpiryMs}
	if existing := g.invite(inviteeID); existing != nil {
		if r.nowMs < existing.ExpiresAtMs {
			return guildAlreadyInvited, nil
		}
		*existing = fresh
	} else {
		g.Invites = append(g.Invites, fresh)
	}
	r.world.changed(g)
	return guildOK, r.world.inviteSent(inviteeID, g, fresh)
}

// accept: FGuildRegistry::Accept.
func (r *guildRules) accept(guildID, playerID string) (string, error) {
	if playerID == "" {
		return guildInvalidPlayer, nil
	}
	g, err := r.world.guild(guildID)
	if err != nil {
		return "", err
	}
	if g == nil {
		return guildUnknownGuild, nil
	}
	pending := g.invite(playerID)
	if pending == nil {
		return guildNoInvite, nil
	}
	if r.nowMs >= pending.ExpiresAtMs {
		return guildInviteExpired, nil
	}
	current, _, err := r.guildOf(playerID)
	if err != nil {
		return "", err
	}
	if current != nil {
		return guildAlreadyInGuild, nil
	}
	if len(g.Members) >= r.config.MaxMembers {
		return guildFull, nil
	}
	g.removeInvite(playerID)
	g.Members = append(g.Members, guildMember{UserID: playerID, Rank: rankMember})
	r.world.changed(g)
	if err := r.world.setMembership(playerID, g.ID); err != nil {
		return "", err
	}
	return guildOK, r.world.inviteGone(playerID, g.ID)
}

// decline: FGuildRegistry::Decline (expired or not).
func (r *guildRules) decline(guildID, playerID string) (string, error) {
	g, err := r.world.guild(guildID)
	if err != nil {
		return "", err
	}
	if g == nil {
		return guildUnknownGuild, nil
	}
	if g.invite(playerID) == nil {
		return guildNoInvite, nil
	}
	g.removeInvite(playerID)
	r.world.changed(g)
	return guildOK, r.world.inviteGone(playerID, g.ID)
}

// pruneExpiredInvites: FGuildRegistry::PruneExpiredInvites for one guild. Returns how many went.
func (r *guildRules) pruneExpiredInvites(g *guildRecord) (int, error) {
	dropped := 0
	kept := g.Invites[:0]
	var gone []string
	for _, invite := range g.Invites {
		if r.nowMs >= invite.ExpiresAtMs {
			dropped++
			gone = append(gone, invite.UserID)
			continue
		}
		kept = append(kept, invite)
	}
	g.Invites = kept
	if dropped > 0 {
		r.world.changed(g)
	}
	for _, playerID := range gone {
		if err := r.world.inviteGone(playerID, g.ID); err != nil {
			return dropped, err
		}
	}
	return dropped, nil
}

// kick: FGuildRegistry::Kick.
func (r *guildRules) kick(actorID, targetID string) (string, error) {
	g, actor, target, result, err := r.checkMemberTarget(actorID, targetID)
	if err != nil || result != guildOK {
		return result, err
	}
	if !canKick(actor.Rank, target.Rank) {
		return guildNotPermitted, nil
	}
	g.removeMember(targetID)
	r.world.changed(g)
	return guildOK, r.world.setMembership(targetID, "")
}

// promote: FGuildRegistry::Promote (Member -> Officer).
func (r *guildRules) promote(actorID, targetID string) (string, error) {
	g, actor, target, result, err := r.checkMemberTarget(actorID, targetID)
	if err != nil || result != guildOK {
		return result, err
	}
	if !guildRankCan(actor.Rank, permManageRanks) {
		return guildNotPermitted, nil
	}
	if target.Rank != rankMember {
		return guildAlreadyRank, nil
	}
	target.Rank = rankOfficer
	r.world.changed(g)
	return guildOK, nil
}

// demote: FGuildRegistry::Demote (Officer -> Member).
func (r *guildRules) demote(actorID, targetID string) (string, error) {
	g, actor, target, result, err := r.checkMemberTarget(actorID, targetID)
	if err != nil || result != guildOK {
		return result, err
	}
	if !guildRankCan(actor.Rank, permManageRanks) {
		return guildNotPermitted, nil
	}
	if target.Rank != rankOfficer {
		return guildAlreadyRank, nil
	}
	target.Rank = rankMember
	r.world.changed(g)
	return guildOK, nil
}

// transferLeadership: FGuildRegistry::TransferLeadership (the old Leader becomes Officer).
func (r *guildRules) transferLeadership(actorID, targetID string) (string, error) {
	g, actor, target, result, err := r.checkMemberTarget(actorID, targetID)
	if err != nil || result != guildOK {
		return result, err
	}
	if actor.Rank != rankLeader {
		return guildNotPermitted, nil
	}
	target.Rank = rankLeader
	actor.Rank = rankOfficer
	r.world.changed(g)
	return guildOK, nil
}

// leave: FGuildRegistry::Leave. The last member disbands the guild and gets the bank (payout).
func (r *guildRules) leave(playerID string) (string, bool, guildPayout, error) {
	g, member, result, err := r.actorGuild(playerID)
	if err != nil || result != guildOK {
		return result, false, guildPayout{}, err
	}
	if len(g.Members) > 1 {
		if member.Rank == rankLeader {
			return guildLeaderMustTransfer, false, guildPayout{}, nil
		}
		g.removeMember(playerID)
		r.world.changed(g)
		return guildOK, false, guildPayout{}, r.world.setMembership(playerID, "")
	}
	payout := guildPayout{Gold: g.Bank.Gold, Items: append([]bankItem{}, g.Bank.Items...)}
	r.world.disbanded(g)
	return guildOK, true, payout, r.world.setMembership(playerID, "")
}

// depositGold: FGuildRegistry::DepositGold.
func (r *guildRules) depositGold(actorID string, amount int64) (string, error) {
	g, actor, result, err := r.actorGuild(actorID)
	if err != nil || result != guildOK {
		return result, err
	}
	if !guildRankCan(actor.Rank, permDeposit) {
		return guildNotPermitted, nil
	}
	if amount <= 0 {
		return guildInvalidAmount, nil
	}
	if amount > maxInt64-g.Bank.Gold {
		return guildGoldOverflow, nil
	}
	g.Bank.Gold += amount
	r.world.changed(g)
	r.log(g, guildLogEntry{PlayerID: actorID, Action: actionDepositGold, Gold: amount})
	return guildOK, nil
}

// withdrawGold: FGuildRegistry::WithdrawGold (a redistribution to oneself).
func (r *guildRules) withdrawGold(actorID string, amount int64) (string, error) {
	return r.redistributeGold(actorID, actorID, amount)
}

// checkBankTake: CheckBankTake (recipient "" with self: none to check).
func (r *guildRules) checkBankTake(actorID string, recipientID *string, permission guildPermission) (*guildRecord, string, error) {
	g, actor, result, err := r.actorGuild(actorID)
	if err != nil || result != guildOK {
		return nil, result, err
	}
	if !guildRankCan(actor.Rank, permission) {
		return nil, guildNotPermitted, nil
	}
	if recipientID != nil && g.member(*recipientID) == nil {
		return nil, guildTargetNotInGuild, nil
	}
	return g, guildOK, nil
}

// redistributeGold: FGuildRegistry::RedistributeGold (to oneself: a withdrawal).
func (r *guildRules) redistributeGold(actorID, recipientID string, amount int64) (string, error) {
	self := recipientID == actorID
	var recipient *string
	permission := permWithdraw
	if !self {
		recipient = &recipientID
		permission = permRedistribute
	}
	g, result, err := r.checkBankTake(actorID, recipient, permission)
	if err != nil || result != guildOK {
		return result, err
	}
	if amount <= 0 {
		return guildInvalidAmount, nil
	}
	if g.Bank.Gold < amount {
		return guildNotEnoughGold, nil
	}
	g.Bank.Gold -= amount
	r.world.changed(g)
	entry := guildLogEntry{PlayerID: actorID, Action: actionWithdrawGold, Gold: amount}
	if !self {
		entry.Action = actionRedistributeGold
		entry.RecipientID = recipientID
	}
	r.log(g, entry)
	return guildOK, nil
}

// depositItem: FGuildRegistry::DepositItem. at nil: the first free spot (rotation allowed). Returns
// the item as stored (slot id and placement set).
func (r *guildRules) depositItem(actorID string, item bankItem, at *gridPlacement) (string, bankItem, error) {
	g, actor, result, err := r.actorGuild(actorID)
	if err != nil || result != guildOK {
		return result, bankItem{}, err
	}
	if !guildRankCan(actor.Rank, permDeposit) {
		return guildNotPermitted, bankItem{}, nil
	}
	if !isValidBankItem(item) {
		return guildInvalidItem, bankItem{}, nil
	}
	var placement gridPlacement
	if at != nil {
		if !g.Bank.canPlace(item.Width, item.Height, *at, 0) {
			return guildNoRoom, bankItem{}, nil
		}
		placement = *at
	} else {
		found, ok := g.Bank.findFreeSpot(item.Width, item.Height)
		if !ok {
			return guildNoRoom, bankItem{}, nil
		}
		placement = found
	}
	stored := item
	stored.SlotID = g.Bank.NextSlotID
	stored.gridPlacement = placement
	g.Bank.NextSlotID++
	g.Bank.Items = append(g.Bank.Items, stored)
	r.world.changed(g)
	r.log(g, guildLogEntry{PlayerID: actorID, Action: actionDepositItem, ItemID: item.ItemID, Rarity: item.Rarity, Count: item.Count})
	return guildOK, stored, nil
}

// withdrawItem: FGuildRegistry::WithdrawItem (whole stacks).
func (r *guildRules) withdrawItem(actorID string, slotID int) (string, bankItem, error) {
	return r.takeItem(actorID, nil, slotID)
}

// redistributeItem: FGuildRegistry::RedistributeItem (to oneself: a withdrawal).
func (r *guildRules) redistributeItem(actorID, recipientID string, slotID int) (string, bankItem, error) {
	if recipientID == actorID {
		return r.takeItem(actorID, nil, slotID)
	}
	return r.takeItem(actorID, &recipientID, slotID)
}

// takeItem: FGuildRegistry::TakeItem.
func (r *guildRules) takeItem(actorID string, recipientID *string, slotID int) (string, bankItem, error) {
	permission := permWithdraw
	if recipientID != nil {
		permission = permRedistribute
	}
	g, result, err := r.checkBankTake(actorID, recipientID, permission)
	if err != nil || result != guildOK {
		return result, bankItem{}, err
	}
	for i, item := range g.Bank.Items {
		if item.SlotID != slotID {
			continue
		}
		g.Bank.Items = append(g.Bank.Items[:i], g.Bank.Items[i+1:]...)
		r.world.changed(g)
		entry := guildLogEntry{PlayerID: actorID, Action: actionWithdrawItem, ItemID: item.ItemID, Rarity: item.Rarity, Count: item.Count}
		if recipientID != nil {
			entry.Action = actionRedistributeItem
			entry.RecipientID = *recipientID
		}
		r.log(g, entry)
		return guildOK, item, nil
	}
	return guildUnknownItem, bankItem{}, nil
}

// moveBankItem: FGuildRegistry::MoveBankItem (not logged).
func (r *guildRules) moveBankItem(actorID string, slotID int, at gridPlacement) (string, error) {
	g, actor, result, err := r.actorGuild(actorID)
	if err != nil || result != guildOK {
		return result, err
	}
	if !guildRankCan(actor.Rank, permManageBank) {
		return guildNotPermitted, nil
	}
	item := g.Bank.find(slotID)
	if item == nil {
		return guildUnknownItem, nil
	}
	if !g.Bank.canPlace(item.Width, item.Height, at, slotID) {
		return guildNoRoom, nil
	}
	item.gridPlacement = at
	r.world.changed(g)
	return guildOK, nil
}
