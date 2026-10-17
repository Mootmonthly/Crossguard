package main

import (
	"fmt"
	"strings"
	"testing"
)

// The C++ guild unit test (Tests/CrossguardRules/TestMain.cpp, TestGuild) case for case, against the Go
// copy of the rules (guild_rules.go). KEEP IN STEP with the C++ test: a case added there is added here.
// Not mirrored: FGuildRegistry::Restore (saved guilds; the backend reads its own records instead).

// memGuildWorld: FGuildRegistry's maps (guilds, name index, player -> guild) plus the invite inboxes.
type memGuildWorld struct {
	guilds  map[string]*guildRecord
	names   map[string]string
	members map[string]string
	inboxes map[string]map[string]guildInvite
	history []guildLogEntry
}

func newMemGuildWorld() *memGuildWorld {
	return &memGuildWorld{guilds: map[string]*guildRecord{}, names: map[string]string{}, members: map[string]string{},
		inboxes: map[string]map[string]guildInvite{}}
}

func (w *memGuildWorld) guild(id string) (*guildRecord, error) { return w.guilds[id], nil }
func (w *memGuildWorld) membershipOf(playerID string) (string, error) {
	return w.members[playerID], nil
}
func (w *memGuildWorld) nameHolder(key string) (string, error) { return w.names[key], nil }
func (w *memGuildWorld) changed(g *guildRecord)                {}
func (w *memGuildWorld) created(g *guildRecord) {
	w.guilds[g.ID] = g
	w.names[guildNameKey(g.Name)] = g.ID
}
func (w *memGuildWorld) disbanded(g *guildRecord) {
	delete(w.guilds, g.ID)
	delete(w.names, guildNameKey(g.Name))
}
func (w *memGuildWorld) inviteGone(playerID, guildID string) error {
	delete(w.inboxes[playerID], guildID)
	return nil
}
func (w *memGuildWorld) logged(g *guildRecord, entry guildLogEntry) {
	w.history = append(w.history, entry)
}
func (w *memGuildWorld) setMembership(playerID, guildID string) error {
	if guildID == "" {
		delete(w.members, playerID)
	} else {
		w.members[playerID] = guildID
	}
	return nil
}
func (w *memGuildWorld) inviteSent(playerID string, g *guildRecord, invite guildInvite) error {
	if w.inboxes[playerID] == nil {
		w.inboxes[playerID] = map[string]guildInvite{}
	}
	w.inboxes[playerID][g.ID] = invite
	return nil
}

// guildRegistry: the FGuildRegistry API over the Go rules, so the cases read like the C++ ones. Times
// are seconds as in C++ (converted to ms).
type guildRegistry struct {
	world  *memGuildWorld
	config guildConfig
}

func newGuildRegistry(config guildConfig) *guildRegistry {
	return &guildRegistry{world: newMemGuildWorld(), config: config}
}

func (g *guildRegistry) rules(now float64) *guildRules {
	return &guildRules{config: g.config, world: g.world, nowMs: int64(now * 1000)}
}

func must(result string, err error) string {
	if err != nil {
		panic(err)
	}
	return result
}

func (g *guildRegistry) Create(id, name, founder string) string {
	return must(g.rules(0).create(id, name, founder))
}
func (g *guildRegistry) Invite(actor, invitee string, now float64) string {
	return must(g.rules(now).invite(actor, invitee))
}
func (g *guildRegistry) Accept(id, player string, now float64) string {
	return must(g.rules(now).accept(id, player))
}
func (g *guildRegistry) Decline(id, player string) string {
	return must(g.rules(0).decline(id, player))
}
func (g *guildRegistry) PruneExpiredInvites(now float64) int {
	total := 0
	for _, guild := range g.world.guilds {
		dropped, err := g.rules(now).pruneExpiredInvites(guild)
		if err != nil {
			panic(err)
		}
		total += dropped
	}
	return total
}
func (g *guildRegistry) Kick(actor, target string) string {
	return must(g.rules(0).kick(actor, target))
}
func (g *guildRegistry) Promote(actor, target string) string {
	return must(g.rules(0).promote(actor, target))
}
func (g *guildRegistry) Demote(actor, target string) string {
	return must(g.rules(0).demote(actor, target))
}
func (g *guildRegistry) TransferLeadership(actor, target string) string {
	return must(g.rules(0).transferLeadership(actor, target))
}

type leaveResult struct {
	Error     string
	Disbanded bool
	Payout    guildPayout
}

func (g *guildRegistry) Leave(player string) leaveResult {
	result, disbanded, payout, err := g.rules(0).leave(player)
	if err != nil {
		panic(err)
	}
	return leaveResult{result, disbanded, payout}
}
func (g *guildRegistry) DepositGold(actor string, amount int64, now float64) string {
	return must(g.rules(now).depositGold(actor, amount))
}
func (g *guildRegistry) WithdrawGold(actor string, amount int64, now float64) string {
	return must(g.rules(now).withdrawGold(actor, amount))
}
func (g *guildRegistry) RedistributeGold(actor, recipient string, amount int64, now float64) string {
	return must(g.rules(now).redistributeGold(actor, recipient, amount))
}

type itemResult struct {
	Error string
	Item  bankItem
}

func (g *guildRegistry) DepositItem(actor string, item bankItem, at *gridPlacement, now float64) itemResult {
	result, stored, err := g.rules(now).depositItem(actor, item, at)
	if err != nil {
		panic(err)
	}
	return itemResult{result, stored}
}
func (g *guildRegistry) WithdrawItem(actor string, slot int, now float64) itemResult {
	result, item, err := g.rules(now).withdrawItem(actor, slot)
	if err != nil {
		panic(err)
	}
	return itemResult{result, item}
}
func (g *guildRegistry) RedistributeItem(actor, recipient string, slot int, now float64) itemResult {
	result, item, err := g.rules(now).redistributeItem(actor, recipient, slot)
	if err != nil {
		panic(err)
	}
	return itemResult{result, item}
}
func (g *guildRegistry) MoveBankItem(actor string, slot int, at gridPlacement) string {
	return must(g.rules(0).moveBankItem(actor, slot, at))
}
func (g *guildRegistry) Find(id string) *guildRecord { return g.world.guilds[id] }
func (g *guildRegistry) FindByName(name string) *guildRecord {
	return g.world.guilds[g.world.names[guildNameKey(name)]]
}
func (g *guildRegistry) GuildOf(player string) *guildRecord {
	guild, _, err := g.rules(0).guildOf(player)
	if err != nil {
		panic(err)
	}
	return guild
}

// RankOf: "" when in no guild (std::nullopt).
func (g *guildRegistry) RankOf(player string) string {
	_, member, _ := g.rules(0).guildOf(player)
	if member == nil {
		return ""
	}
	return member.Rank
}

// guildCheck: CHECK(...) that keeps going and reports the line's text.
type guildCheck struct {
	t      *testing.T
	failed int
}

func (c *guildCheck) check(ok bool, what string, args ...any) {
	c.t.Helper()
	if !ok {
		c.failed++
		c.t.Errorf("CHECK failed: "+what, args...)
	}
}

func (c *guildCheck) eq(got, want any, what string) {
	c.t.Helper()
	c.check(fmt.Sprint(got) == fmt.Sprint(want), "%s: got %v, want %v", what, got, want)
}

func TestGuildRulesMirrorCpp(t *testing.T) {
	c := &guildCheck{t: t}
	config := defaultGuildConfig()
	c.eq(config.MaxMembers, 50, "MaxMembers")
	c.eq(config.BankWidth, 10, "BankWidth (one stash tab)")
	c.eq(config.BankHeight, 30, "BankHeight (one stash tab)")
	c.eq(config.BankLogSize, 100, "BankLogSize")
	c.eq(config.InviteExpiryMs, int64(300_000), "InviteExpirySeconds")

	// Permissions matrix.
	for _, rank := range []string{rankMember, rankOfficer, rankLeader} {
		member := rank == rankMember
		c.check(guildRankCan(rank, permDeposit), "%s deposits", rank)
		c.check(guildRankCan(rank, permInvite) == !member, "%s invite", rank)
		c.check(guildRankCan(rank, permWithdraw) == !member, "%s withdraw", rank)
		c.check(guildRankCan(rank, permRedistribute) == !member, "%s redistribute", rank)
		c.check(guildRankCan(rank, permManageBank) == !member, "%s manage bank", rank)
		c.check(guildRankCan(rank, permManageRanks) == (rank == rankLeader), "%s manage ranks", rank)
	}
	c.check(canKick(rankLeader, rankOfficer), "leader kicks officer")
	c.check(canKick(rankLeader, rankMember), "leader kicks member")
	c.check(!canKick(rankLeader, rankLeader), "leader kicks leader")
	c.check(canKick(rankOfficer, rankMember), "officer kicks member")
	c.check(!canKick(rankOfficer, rankOfficer), "officer kicks officer")
	c.check(!canKick(rankOfficer, rankLeader), "officer kicks leader")
	c.check(!canKick(rankMember, rankMember), "member kicks member")
	c.check(!canKick(rankMember, rankOfficer), "member kicks officer")
	c.check(!canKick(rankMember, rankLeader), "member kicks leader")

	// Names: 3-24 characters after trimming; letters, digits, spaces and '-'.
	c.eq(trimGuildName("  Iron Wolves  "), "Iron Wolves", "trim")
	c.eq(trimGuildName("   "), "", "trim spaces")
	for name, want := range map[string]string{
		"Abc": guildOK, "  Abc  ": guildOK, "Ab": guildInvalidName, "  Ab   ": guildInvalidName,
		strings.Repeat("x", 24): guildOK, strings.Repeat("x", 25): guildInvalidName, "Red-Hand 7": guildOK,
		"Red_Hand": guildInvalidName, "Red\tHand": guildInvalidName, "Caf\xC3\xA9s": guildInvalidName, "": guildInvalidName,
	} {
		c.eq(validateGuildName(config, name), want, fmt.Sprintf("ValidateGuildName(%q)", name))
	}

	// Create.
	guilds := newGuildRegistry(config)
	c.eq(guilds.Create("g1", "  Iron Wolves ", "leader"), guildOK, "create g1")
	wolves := guilds.Find("g1")
	c.check(wolves != nil && wolves.Name == "Iron Wolves", "stored trimmed name")
	c.check(wolves != nil && wolves.leaderID() == "leader", "founder leads")
	c.check(wolves != nil && wolves.Bank.Width == 10 && wolves.Bank.Height == 30, "bank 10 x 30")
	c.eq(guilds.RankOf("leader"), rankLeader, "founder rank")
	c.check(guilds.GuildOf("leader") == wolves, "GuildOf founder")
	c.check(guilds.FindByName("IRON WOLVES") == wolves, "FindByName ignores case")
	c.check(guilds.FindByName(" iron wolves ") == wolves, "FindByName trims")
	c.check(guilds.FindByName("Iron Wolf") == nil, "FindByName other")
	c.eq(guilds.Create("g2", "iron WOLVES", "other"), guildNameTaken, "name taken ignoring case")
	c.eq(guilds.Create("g1", "Other Name", "other"), guildIDTaken, "id taken")
	c.eq(guilds.Create("", "Other Name", "other"), guildInvalidGuildID, "empty id")
	c.eq(guilds.Create("g2", "Other Name", ""), guildInvalidPlayer, "empty founder")
	c.eq(guilds.Create("g2", "No!", "other"), guildInvalidName, "bad name")
	// Double guild: one guild per player.
	c.eq(guilds.Create("g2", "Other Name", "leader"), guildAlreadyInGuild, "founder in a guild")
	c.check(guilds.Find("g2") == nil && guilds.FindByName("Other Name") == nil, "refused create left nothing")
	c.eq(guilds.Create("g2", "Ravens", "raven"), guildOK, "create g2")

	// Invites: Leader and Officer invite, Members do not.
	now := 1000.0
	c.eq(guilds.Invite("leader", "officer", now), guildOK, "invite officer")
	c.eq(guilds.Accept("g1", "officer", now+1.0), guildOK, "accept officer")
	c.eq(guilds.RankOf("officer"), rankMember, "joins as member")
	c.eq(guilds.Promote("leader", "officer"), guildOK, "promote officer")
	c.eq(guilds.RankOf("officer"), rankOfficer, "officer rank")
	c.eq(guilds.Invite("officer", "member", now), guildOK, "officer invites")
	c.eq(guilds.Accept("g1", "member", now), guildOK, "accept member")
	c.eq(guilds.Invite("member", "someone", now), guildNotPermitted, "member invites")
	c.eq(guilds.Invite("nobody", "someone", now), guildNotInGuild, "outsider invites")
	c.eq(guilds.Invite("leader", "", now), guildInvalidPlayer, "empty invitee")
	// Double guild: invitee already in this or another guild.
	c.eq(guilds.Invite("leader", "raven", now), guildAlreadyInGuild, "invite another guild's member")
	c.eq(guilds.Invite("leader", "member", now), guildAlreadyInGuild, "invite own member")
	c.check(len(wolves.Members) == 3 && len(wolves.Invites) == 0, "3 members, no invites")

	// Decline, re-invite, duplicate invite.
	c.eq(guilds.Invite("leader", "a", now), guildOK, "invite a")
	c.eq(guilds.Invite("officer", "a", now+10.0), guildAlreadyInvited, "duplicate invite")
	c.eq(guilds.Decline("g1", "a"), guildOK, "decline")
	c.eq(guilds.Decline("g1", "a"), guildNoInvite, "decline again")
	c.eq(guilds.Accept("g1", "a", now), guildNoInvite, "accept declined")
	c.eq(guilds.Decline("nope", "a"), guildUnknownGuild, "decline unknown guild")
	c.eq(guilds.Accept("nope", "a", now), guildUnknownGuild, "accept unknown guild")
	c.eq(guilds.Accept("g1", "", now), guildInvalidPlayer, "accept by nobody")

	// Expired invite: refused, kept until pruned or replaced.
	expiry := float64(config.InviteExpiryMs) / 1000
	c.eq(guilds.Invite("leader", "late", now), guildOK, "invite late")
	c.eq(guilds.Accept("g1", "late", now+expiry), guildInviteExpired, "accept expired")
	c.check(guilds.GuildOf("late") == nil && len(wolves.Invites) == 1, "expired invite kept")
	c.eq(guilds.Accept("g1", "late", now+expiry-0.5), guildOK, "accept just in time")
	c.eq(guilds.Invite("leader", "late2", now), guildOK, "invite late2")
	c.eq(guilds.Invite("leader", "late2", now+expiry), guildOK, "expired invite replaced")
	c.check(len(wolves.Invites) == 1 && wolves.Invites[0].ExpiresAtMs == int64((now+2*expiry)*1000), "replaced invite's expiry")
	c.eq(guilds.PruneExpiredInvites(now+2*expiry-1.0), 0, "prune before expiry")
	c.eq(guilds.PruneExpiredInvites(now+2*expiry), 1, "prune at expiry")
	c.check(len(wolves.Invites) == 0, "pruned")

	// Invites from two guilds: accepting one makes the other fail.
	c.eq(guilds.Invite("leader", "b", now), guildOK, "g1 invites b")
	c.eq(guilds.Invite("raven", "b", now), guildOK, "g2 invites b")
	c.eq(guilds.Accept("g2", "b", now), guildOK, "b joins g2")
	c.eq(guilds.Accept("g1", "b", now), guildAlreadyInGuild, "b's other invite fails")
	c.eq(guilds.Decline("g1", "b"), guildOK, "b declines g1")
	// Members now: leader (L), officer (O), member (M), late (M).

	// Kick matrix.
	c.eq(guilds.Kick("member", "late"), guildNotPermitted, "member kicks member")
	c.eq(guilds.Kick("member", "officer"), guildNotPermitted, "member kicks officer")
	c.eq(guilds.Kick("member", "leader"), guildNotPermitted, "member kicks leader")
	c.eq(guilds.Kick("officer", "leader"), guildNotPermitted, "officer kicks leader")
	c.eq(guilds.Kick("leader", "leader"), guildCannotTargetSelf, "leader kicks self")
	c.eq(guilds.Kick("officer", "officer"), guildCannotTargetSelf, "officer kicks self")
	c.eq(guilds.Kick("leader", "raven"), guildTargetNotInGuild, "kick outsider")
	c.eq(guilds.Kick("nobody", "late"), guildNotInGuild, "outsider kicks")
	c.eq(len(wolves.Members), 4, "4 members")
	c.eq(guilds.Kick("officer", "late"), guildOK, "officer kicks member")
	c.check(guilds.GuildOf("late") == nil && len(wolves.Members) == 3, "late kicked")
	c.eq(guilds.Invite("leader", "o2", now), guildOK, "invite o2")
	c.eq(guilds.Accept("g1", "o2", now), guildOK, "o2 joins")
	c.eq(guilds.Promote("leader", "o2"), guildOK, "o2 officer")
	c.eq(guilds.Kick("officer", "o2"), guildNotPermitted, "officer kicks officer")
	c.eq(guilds.Kick("leader", "o2"), guildOK, "leader kicks officer")
	c.eq(guilds.RankOf("o2"), "", "o2 gone")

	// Promote / demote: Leader only.
	c.eq(guilds.Promote("officer", "member"), guildNotPermitted, "officer promotes")
	c.eq(guilds.Promote("member", "member"), guildCannotTargetSelf, "promote self")
	c.eq(guilds.Demote("officer", "member"), guildNotPermitted, "officer demotes")
	c.eq(guilds.Promote("leader", "officer"), guildAlreadyRank, "promote officer")
	c.eq(guilds.Demote("leader", "member"), guildAlreadyRank, "demote member")
	c.eq(guilds.Promote("leader", "leader"), guildCannotTargetSelf, "leader promotes self")
	c.eq(guilds.Promote("leader", "raven"), guildTargetNotInGuild, "promote outsider")
	c.eq(guilds.Demote("leader", "officer"), guildOK, "demote officer")
	c.eq(guilds.RankOf("officer"), rankMember, "demoted")
	c.eq(guilds.Promote("leader", "officer"), guildOK, "promote again")

	// Leave: the Leader must transfer first.
	c.eq(guilds.Leave("leader").Error, guildLeaderMustTransfer, "leader leaves")
	c.check(guilds.RankOf("leader") == rankLeader && len(wolves.Members) == 3, "leader stayed")
	c.eq(guilds.Leave("nobody").Error, guildNotInGuild, "outsider leaves")
	c.eq(guilds.TransferLeadership("officer", "member"), guildNotPermitted, "officer transfers")
	c.eq(guilds.TransferLeadership("leader", "leader"), guildCannotTargetSelf, "transfer to self")
	c.eq(guilds.TransferLeadership("leader", "raven"), guildTargetNotInGuild, "transfer to outsider")
	c.eq(guilds.TransferLeadership("leader", "member"), guildOK, "transfer")
	c.eq(guilds.RankOf("member"), rankLeader, "new leader")
	c.eq(guilds.RankOf("leader"), rankOfficer, "old leader is officer")
	c.eq(wolves.leaderID(), "member", "LeaderId")
	// Ex-leader is an officer now and can leave.
	left := guilds.Leave("leader")
	c.check(left.Error == guildOK && !left.Disbanded, "officer leaves: %+v", left)
	c.check(guilds.GuildOf("leader") == nil && len(wolves.Members) == 2, "left")
	// Free to found or join another guild.
	c.eq(guilds.Invite("raven", "leader", now), guildOK, "raven invites leader")
	c.eq(guilds.Accept("g2", "leader", now), guildOK, "leader joins g2")

	// Bank, gold: anyone deposits; officers and the leader withdraw and redistribute.
	// g1 now: member (Leader), officer (Officer). Add a plain member.
	c.eq(guilds.Invite("member", "m2", now), guildOK, "invite m2")
	c.eq(guilds.Accept("g1", "m2", now), guildOK, "m2 joins")
	c.eq(guilds.DepositGold("m2", 500, 1.0), guildOK, "m2 deposits")
	c.eq(guilds.DepositGold("officer", 250, 2.0), guildOK, "officer deposits")
	c.eq(wolves.Bank.Gold, int64(750), "gold 750")
	c.eq(guilds.DepositGold("m2", 0, 3.0), guildInvalidAmount, "deposit 0")
	c.eq(guilds.DepositGold("m2", -5, 3.0), guildInvalidAmount, "deposit -5")
	c.eq(guilds.DepositGold("nobody", 5, 3.0), guildNotInGuild, "outsider deposits")
	c.eq(guilds.WithdrawGold("m2", 10, 3.0), guildNotPermitted, "member withdraws")
	c.eq(guilds.RedistributeGold("m2", "officer", 10, 3.0), guildNotPermitted, "member redistributes")
	// Bank underflow.
	c.eq(guilds.WithdrawGold("officer", 751, 3.0), guildNotEnoughGold, "withdraw too much")
	c.eq(guilds.RedistributeGold("member", "m2", 751, 3.0), guildNotEnoughGold, "redistribute too much")
	c.eq(guilds.WithdrawGold("officer", 0, 3.0), guildInvalidAmount, "withdraw 0")
	c.eq(wolves.Bank.Gold, int64(750), "gold still 750")
	c.eq(guilds.WithdrawGold("officer", 100, 4.0), guildOK, "officer withdraws")
	c.eq(guilds.WithdrawGold("member", 50, 5.0), guildOK, "leader withdraws")
	c.eq(guilds.RedistributeGold("officer", "m2", 100, 6.0), guildOK, "officer redistributes")
	c.eq(guilds.RedistributeGold("officer", "raven", 100, 6.0), guildTargetNotInGuild, "redistribute to outsider")
	c.eq(guilds.RedistributeGold("officer", "", 100, 6.0), guildTargetNotInGuild, "redistribute to nobody")
	c.eq(wolves.Bank.Gold, int64(500), "gold 500")
	c.eq(guilds.WithdrawGold("member", 500, 7.0), guildOK, "withdraw all")
	c.eq(wolves.Bank.Gold, int64(0), "gold 0")
	c.eq(guilds.WithdrawGold("member", 1, 7.0), guildNotEnoughGold, "withdraw from empty")
	// Overflow is refused.
	c.eq(guilds.DepositGold("m2", maxInt64, 8.0), guildOK, "deposit INT64_MAX")
	c.eq(guilds.DepositGold("m2", 1, 8.0), guildGoldOverflow, "overflow")
	c.eq(wolves.Bank.Gold, maxInt64, "gold INT64_MAX")
	c.eq(guilds.WithdrawGold("member", maxInt64, 9.0), guildOK, "withdraw INT64_MAX")

	// Log so far: 2 deposits, 3 withdrawals, 1 redistribution, 1 deposit, 1 withdrawal.
	c.eq(len(wolves.BankLog), 8, "log length")
	c.check(wolves.BankLog[0].PlayerID == "m2" && wolves.BankLog[0].Action == actionDepositGold, "log[0] %+v", wolves.BankLog[0])
	c.check(wolves.BankLog[0].Gold == 500 && wolves.BankLog[0].AtMs == 1000 && wolves.BankLog[0].ItemID == "", "log[0] %+v", wolves.BankLog[0])
	c.eq(wolves.BankLog[4].Action, actionRedistributeGold, "log[4] action")
	c.check(wolves.BankLog[4].PlayerID == "officer" && wolves.BankLog[4].RecipientID == "m2", "log[4] %+v", wolves.BankLog[4])
	c.check(wolves.BankLog[2].Action == actionWithdrawGold && wolves.BankLog[2].RecipientID == "", "log[2] %+v", wolves.BankLog[2])
	// Redistributing to oneself is a withdrawal.
	c.eq(guilds.DepositGold("m2", 10, 10.0), guildOK, "deposit 10")
	c.eq(guilds.RedistributeGold("officer", "officer", 10, 10.0), guildOK, "redistribute to self")
	c.eq(wolves.BankLog[len(wolves.BankLog)-1].Action, actionWithdrawGold, "logged as withdrawal")

	// Bank, items: placed on the stash-tab grid.
	sword := bankItem{ItemID: "sword_one_handed", Rarity: 2 /* Rare */, Count: 1, Width: 1, Height: 3, Payload: "affixes:..."}
	deposited := guilds.DepositItem("m2", sword, nil, 11.0)
	c.check(deposited.Error == guildOK && deposited.Item.SlotID > 0, "sword deposited %+v", deposited)
	c.eq(wolves.Bank.itemAt(0, 0), deposited.Item.SlotID, "sword at 0,0")
	c.eq(wolves.Bank.itemAt(0, 2), deposited.Item.SlotID, "sword at 0,2")
	c.check(wolves.Bank.find(deposited.Item.SlotID) != nil, "sword found")
	c.eq(wolves.Bank.find(deposited.Item.SlotID).Payload, "affixes:...", "payload kept")
	last := func() guildLogEntry { return wolves.BankLog[len(wolves.BankLog)-1] }
	c.eq(last().Action, actionDepositItem, "deposit logged")
	c.check(last().ItemID == "sword_one_handed" && last().Count == 1, "deposit log %+v", last())
	c.check(last().Rarity == 2 && last().Gold == 0, "deposit log %+v", last())

	potions := bankItem{ItemID: "healing_potion", Count: 5, Width: 1, Height: 1}
	corner := gridPlacement{X: 9, Y: 29}
	potionsIn := guilds.DepositItem("m2", potions, &corner, 12.0)
	c.check(potionsIn.Error == guildOK && wolves.Bank.itemAt(9, 29) == potionsIn.Item.SlotID, "potions in the corner %+v", potionsIn)
	c.check(potionsIn.Item.SlotID != deposited.Item.SlotID, "new slot id")
	// Occupied or out-of-grid spots are refused.
	onSword := gridPlacement{X: 0, Y: 1}
	outside := gridPlacement{X: 10, Y: 0}
	c.eq(guilds.DepositItem("m2", potions, &onSword, 12.0).Error, guildNoRoom, "on the sword")
	c.eq(guilds.DepositItem("m2", potions, &outside, 12.0).Error, guildNoRoom, "outside")
	bad := potions
	bad.Count = 0
	c.eq(guilds.DepositItem("m2", bad, nil, 12.0).Error, guildInvalidItem, "count 0")
	bad = potions
	bad.ItemID = ""
	c.eq(guilds.DepositItem("m2", bad, nil, 12.0).Error, guildInvalidItem, "no id")
	bad = potions
	bad.Width = 0
	c.eq(guilds.DepositItem("m2", bad, nil, 12.0).Error, guildInvalidItem, "width 0")
	c.eq(guilds.DepositItem("nobody", potions, nil, 12.0).Error, guildNotInGuild, "outsider deposits item")
	c.eq(len(wolves.Bank.Items), 2, "2 stacks")

	// Members cannot withdraw, redistribute or rearrange items.
	c.eq(guilds.WithdrawItem("m2", deposited.Item.SlotID, 13.0).Error, guildNotPermitted, "member withdraws item")
	c.eq(guilds.RedistributeItem("m2", "officer", deposited.Item.SlotID, 13.0).Error, guildNotPermitted, "member gives item")
	c.eq(guilds.MoveBankItem("m2", deposited.Item.SlotID, gridPlacement{5, 5, true}), guildNotPermitted, "member moves")
	// Officers manage: move and rotate.
	c.eq(guilds.MoveBankItem("officer", deposited.Item.SlotID, gridPlacement{5, 5, true}), guildOK, "officer moves")
	c.check(wolves.Bank.itemAt(7, 5) == deposited.Item.SlotID && wolves.Bank.itemAt(0, 0) == 0, "moved and rotated")
	c.eq(guilds.MoveBankItem("officer", deposited.Item.SlotID, gridPlacement{8, 29, true}), guildNoRoom, "move off the grid")
	c.eq(guilds.MoveBankItem("officer", 999, gridPlacement{0, 0, false}), guildUnknownItem, "move unknown")

	given := guilds.RedistributeItem("officer", "m2", deposited.Item.SlotID, 14.0)
	c.check(given.Error == guildOK && given.Item.ItemID == "sword_one_handed" && given.Item.Payload == "affixes:...", "given %+v", given)
	c.check(wolves.Bank.find(deposited.Item.SlotID) == nil && wolves.Bank.itemAt(7, 5) == 0, "sword left the bank")
	c.eq(last().Action, actionRedistributeItem, "give logged")
	c.check(last().RecipientID == "m2" && last().AtMs == 14000, "give log %+v", last())
	c.eq(guilds.RedistributeItem("officer", "m2", deposited.Item.SlotID, 14.0).Error, guildUnknownItem, "give again")
	c.eq(guilds.RedistributeItem("officer", "raven", potionsIn.Item.SlotID, 14.0).Error, guildTargetNotInGuild, "give to outsider")
	taken := guilds.WithdrawItem("member", potionsIn.Item.SlotID, 15.0)
	c.check(taken.Error == guildOK && taken.Item.Count == 5 && len(wolves.Bank.Items) == 0, "potions taken %+v", taken)
	c.check(last().Action == actionWithdrawItem && last().Count == 5, "withdraw log %+v", last())

	// A full bank refuses deposits.
	tiny := defaultGuildConfig()
	tiny.BankWidth = 2
	tiny.BankHeight = 2
	tiny.BankLogSize = 3
	tiny.MaxMembers = 2
	small := newGuildRegistry(tiny)
	c.eq(small.Create("s", "Small", "boss"), guildOK, "create small")
	shield := bankItem{ItemID: "shield", Count: 1, Width: 2, Height: 2}
	c.eq(small.DepositItem("boss", shield, nil, 1.0).Error, guildOK, "shield fills it")
	c.eq(small.DepositItem("boss", potions, nil, 2.0).Error, guildNoRoom, "full bank")
	// Rotation is used to fit.
	narrow := newGuildRegistry(tiny)
	c.eq(narrow.Create("n", "Narrow", "boss"), guildOK, "create narrow")
	bar := bankItem{ItemID: "bar", Count: 1, Width: 1, Height: 2}
	wide := bankItem{ItemID: "wide", Count: 1, Width: 2, Height: 1}
	c.eq(narrow.DepositItem("boss", wide, nil, 1.0).Error, guildOK, "wide in")
	rotated := narrow.DepositItem("boss", bar, nil, 1.0)
	c.check(rotated.Error == guildOK && narrow.Find("n").Bank.find(rotated.Item.SlotID).Rotated, "bar rotated %+v", rotated)

	// Log trimming: only the newest BankLogSize entries are kept.
	for i := 0; i < 5; i++ {
		c.eq(small.DepositGold("boss", int64(i+1), 10.0+float64(i)), guildOK, "small deposit")
	}
	smallGuild := small.Find("s")
	c.eq(len(smallGuild.BankLog), 3, "trimmed log")
	c.check(smallGuild.BankLog[0].Gold == 3 && smallGuild.BankLog[2].Gold == 5, "newest kept %+v", smallGuild.BankLog)
	c.eq(smallGuild.BankLog[2].AtMs, int64(14000), "newest time")
	c.check(len(wolves.BankLog) <= 100, "wolves log within 100")
	for i := 0; i < 150; i++ {
		c.eq(guilds.DepositGold("m2", 1, 100.0+float64(i)), guildOK, "deposit 1")
	}
	c.check(len(wolves.BankLog) == 100 && wolves.BankLog[0].AtMs == 150_000, "100 newest kept, first at %d", wolves.BankLog[0].AtMs)

	// Full guild: invites and accepts are refused at MaxMembers.
	c.eq(small.Invite("boss", "p1", 0.0), guildOK, "invite p1")
	c.eq(small.Invite("boss", "p2", 0.0), guildOK, "invite p2")
	c.eq(small.Accept("s", "p1", 1.0), guildOK, "p1 joins")
	c.eq(small.Accept("s", "p2", 1.0), guildFull, "p2 refused: full")
	c.eq(small.Invite("boss", "p3", 1.0), guildFull, "invite while full")
	c.check(small.GuildOf("p2") == nil && len(smallGuild.Members) == 2, "still 2")

	// Default cap of 50.
	big := newGuildRegistry(config)
	c.eq(big.Create("b", "Big", "p0"), guildOK, "create big")
	for i := 1; i < 50; i++ {
		player := fmt.Sprintf("p%d", i)
		c.eq(big.Invite("p0", player, 0.0), guildOK, "invite "+player)
		c.eq(big.Accept("b", player, 0.0), guildOK, "accept "+player)
	}
	c.eq(len(big.Find("b").Members), 50, "50 members")
	c.eq(big.Invite("p0", "p50", 0.0), guildFull, "51st refused")

	// Last member leaving disbands the guild and hands over the bank; the name frees up.
	c.eq(small.Kick("boss", "p1"), guildOK, "kick p1")
	disbanded := small.Leave("boss")
	c.check(disbanded.Error == guildOK && disbanded.Disbanded, "disbanded %+v", disbanded)
	c.check(disbanded.Payout.Gold == 15 && len(disbanded.Payout.Items) == 1 && disbanded.Payout.Items[0].ItemID == "shield", "payout %+v", disbanded.Payout)
	c.check(small.Find("s") == nil && small.FindByName("Small") == nil && small.GuildOf("boss") == nil, "gone")
	c.eq(small.Accept("s", "p2", 1.0), guildUnknownGuild, "accept into a disbanded guild")
	c.eq(small.Create("s", "SMALL", "p2"), guildOK, "name free again")

	// The full history saw every bank action; the record keeps the newest 100.
	c.check(len(guilds.world.history) == 8+2+2+2+150 && guilds.world.history[len(guilds.world.history)-1].Seq == wolves.LogSeq,
		"history %d entries, last seq %d", len(guilds.world.history), wolves.LogSeq)
}
