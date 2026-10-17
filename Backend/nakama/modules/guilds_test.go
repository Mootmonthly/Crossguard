package main

import (
	"encoding/json"
	"fmt"
	"testing"
	"time"
)

// crossguard_guild / _get / _log against the fake Nakama: storage, transactions, the carried save, the
// group mirror. The rules themselves: guild_rules_test.go.

func guildCall(nk *fakeNakama, payload map[string]any) (guildStateResponse, error) {
	body, _ := json.Marshal(payload)
	out, err := RpcGuildFn(serverCtx(), &fakeLogger{}, nil, nk, string(body))
	var response guildStateResponse
	if err == nil {
		if decodeErr := json.Unmarshal([]byte(out), &response); decodeErr != nil {
			return response, fmt.Errorf("answer %q: %v", out, decodeErr)
		}
	}
	return response, err
}

func guildDo(t *testing.T, nk *fakeNakama, payload map[string]any) guildStateResponse {
	t.Helper()
	response, err := guildCall(nk, payload)
	if err != nil {
		t.Fatalf("%v: %v", payload["op"], err)
	}
	return response
}

func expectGuild(t *testing.T, got guildStateResponse, want string) {
	t.Helper()
	if got.Result != want {
		t.Fatalf("result %q, want %q (%+v)", got.Result, want, got)
	}
}

// saveOf: a carried save for the user (the shape crossguard_save_write checks) at the expected version.
func saveOf(userID, expected string, gold int) map[string]any {
	return map[string]any{"user_id": userID, "record": map[string]any{"version": 1, "playerId": userID, "gold": gold},
		"expected_version": expected, "lease_token": testLeaseToken}
}

func storedSave(t *testing.T, nk *fakeNakama, userID string) (int, string) {
	t.Helper()
	object, ok := nk.storage[storageKey(collectionCharacters, userID, defaultCharacterID)]
	if !ok {
		return -1, ""
	}
	var record struct {
		Gold int `json:"gold"`
	}
	_ = json.Unmarshal([]byte(object.value), &record)
	return record.Gold, object.version
}

func storedGuild(t *testing.T, nk *fakeNakama, guildID string) *guildRecord {
	t.Helper()
	var record guildRecord
	if !nk.object(collectionGuilds, systemUserID, guildID, &record) {
		return nil
	}
	return &record
}

func foundGuild(t *testing.T, nk *fakeNakama, leader, name string, members ...string) string {
	t.Helper()
	created := guildDo(t, nk, map[string]any{"op": "create", "actor_id": leader, "name": name, "server_id": "hub-1"})
	expectGuild(t, created, guildOK)
	for _, member := range members {
		expectGuild(t, guildDo(t, nk, map[string]any{"op": "invite", "actor_id": leader, "target_id": member}), guildOK)
		expectGuild(t, guildDo(t, nk, map[string]any{"op": "accept", "actor_id": member, "guild_id": created.GuildID}), guildOK)
	}
	return created.GuildID
}

func TestGuildMembershipAndGroupMirror(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	lead, b, c, outsider := uid(1), uid(2), uid(3), uid(4)
	nk := newFakeNakama(lead, b, c, outsider)

	created := guildDo(t, nk, map[string]any{"op": "create", "actor_id": lead, "name": "  Iron Wolves ", "server_id": "hub-1"})
	expectGuild(t, created, guildOK)
	id := created.GuildID
	if !uuidPattern.MatchString(id) || created.Players[lead].GuildID != id || created.Guilds[id].Name != "Iron Wolves" {
		t.Fatalf("created: %+v", created)
	}
	if states := nk.groupStates(id); len(states) != 1 || states[lead] != 0 {
		t.Fatalf("group mirror after create: %v", states)
	}
	var name map[string]string
	if !nk.object(collectionGuildNames, systemUserID, "iron wolves", &name) || name["guild_id"] != id {
		t.Fatalf("name index: %v", name)
	}
	// Name taken ignoring case; the refused create leaves no group behind.
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "create", "actor_id": b, "name": "IRON wolves"}), guildNameTaken)
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "create", "actor_id": lead, "name": "Other"}), guildAlreadyInGuild)
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "create", "actor_id": b, "name": "No!"}), guildInvalidName)
	if len(nk.groups) != 1 {
		t.Fatalf("refused creates made groups: %d", len(nk.groups))
	}

	// Invites: the inbox shows them; unknown players are invalid.
	invited := guildDo(t, nk, map[string]any{"op": "invite", "actor_id": lead, "target_id": b})
	expectGuild(t, invited, guildOK)
	if invites := invited.Players[b].Invites; len(invites) != 1 || invites[0].GuildID != id || invites[0].GuildName != "Iron Wolves" ||
		invites[0].ExpiresAtMs != clock.Add(300*time.Second).UnixMilli() {
		t.Fatalf("invitee's view: %+v", invited.Players[b])
	}
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "invite", "actor_id": lead, "target_id": uid(99)}), guildInvalidPlayer)
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "invite", "actor_id": lead, "target_id": b}), guildAlreadyInvited)
	// The invite is still there on another hub shard (a fresh read).
	got := guildGet(t, nk, b)
	if len(got.Players[b].Invites) != 1 {
		t.Fatalf("invite after a fresh read: %+v", got.Players[b])
	}
	accepted := guildDo(t, nk, map[string]any{"op": "accept", "actor_id": b, "guild_id": id})
	expectGuild(t, accepted, guildOK)
	if len(accepted.Guilds[id].Members) != 2 || accepted.Players[b].GuildID != id || len(accepted.Players[b].Invites) != 0 {
		t.Fatalf("after accept: %+v", accepted)
	}
	if states := nk.groupStates(id); states[b] != 2 {
		t.Fatalf("member mirrored: %v", states)
	}

	// Ranks follow into the group: promote, transfer (old leader becomes officer), demote.
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "promote", "actor_id": lead, "target_id": b}), guildOK)
	if states := nk.groupStates(id); states[b] != 1 {
		t.Fatalf("officer mirrored: %v", states)
	}
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "transfer", "actor_id": lead, "target_id": b}), guildOK)
	if states := nk.groupStates(id); states[b] != 0 || states[lead] != 1 {
		t.Fatalf("transfer mirrored: %v", states)
	}
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "demote", "actor_id": b, "target_id": lead}), guildOK)
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "promote", "actor_id": lead, "target_id": b}), guildNotPermitted)

	// A group failure does not fail the guild; the next change repairs the mirror.
	nk.failGroups = true
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "invite", "actor_id": b, "target_id": c}), guildOK)
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "accept", "actor_id": c, "guild_id": id}), guildOK)
	nk.failGroups = false
	if _, in := nk.groupStates(id)[c]; in {
		t.Fatalf("group changed while down")
	}
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "promote", "actor_id": b, "target_id": c}), guildOK)
	if states := nk.groupStates(id); states[c] != 1 {
		t.Fatalf("mirror repaired: %v", states)
	}

	// Kick and leave.
	// c is an Officer, b the Leader, lead a Member now.
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "kick", "actor_id": c, "target_id": b}), guildNotPermitted)
	kicked := guildDo(t, nk, map[string]any{"op": "kick", "actor_id": b, "target_id": lead})
	expectGuild(t, kicked, guildOK)
	if kicked.Players[lead].GuildID != "" || len(kicked.Guilds[id].Members) != 2 {
		t.Fatalf("after kick: %+v", kicked)
	}
	if _, in := nk.groupStates(id)[lead]; in {
		t.Fatalf("kicked player still in the group")
	}
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "leave", "actor_id": b}), guildLeaderMustTransfer)
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "leave", "actor_id": c}), guildOK)
	// The last member leaves an empty bank: disbanded, name and group gone.
	left := guildDo(t, nk, map[string]any{"op": "leave", "actor_id": b})
	expectGuild(t, left, guildOK)
	if !left.Disbanded || storedGuild(t, nk, id) != nil || nk.groupStates(id) != nil {
		t.Fatalf("disband: %+v", left)
	}
	if nk.object(collectionGuildNames, systemUserID, "iron wolves", &name) {
		t.Fatalf("name not freed")
	}
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "create", "actor_id": outsider, "name": "iron wolves"}), guildOK)

	// Requests are server-to-server only; bad ids are errors.
	if _, err := RpcGuildFn(clientCtx(lead), &fakeLogger{}, nil, nk, `{"op":"leave","actor_id":"`+lead+`"}`); errorCode(err) != codePermissionDenied {
		t.Fatalf("client call: %v", err)
	}
	if _, err := guildCall(nk, map[string]any{"op": "accept", "actor_id": lead, "guild_id": "nope"}); errorCode(err) != codeInvalidArgument {
		t.Fatalf("bad guild id: %v", err)
	}
	if _, err := guildCall(nk, map[string]any{"op": "dance", "actor_id": lead}); errorCode(err) != codeInvalidArgument {
		t.Fatalf("bad op: %v", err)
	}
}

func guildGet(t *testing.T, nk *fakeNakama, userIDs ...string) guildStateResponse {
	t.Helper()
	payload, _ := json.Marshal(map[string]any{"user_ids": userIDs})
	out, err := RpcGuildGetFn(serverCtx(), &fakeLogger{}, nil, nk, string(payload))
	if err != nil {
		t.Fatalf("guild get: %v", err)
	}
	var response guildStateResponse
	if err := json.Unmarshal([]byte(out), &response); err != nil {
		t.Fatalf("guild get answer %q: %v", out, err)
	}
	return response
}

func TestGuildBankCarriesTheSave(t *testing.T) {
	setClock(t, time.Unix(1_800_000_000, 0))
	lead, member, officer := uid(1), uid(2), uid(3)
	nk := newFakeNakama(lead, member, officer)
	id := foundGuild(t, nk, lead, "Ravens", member, officer)
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "promote", "actor_id": lead, "target_id": officer}), guildOK)

	// A bank operation needs the player's save (and a request id).
	if _, err := guildCall(nk, map[string]any{"op": "deposit_gold", "actor_id": member, "amount": 100}); errorCode(err) != codeInvalidArgument {
		t.Fatalf("no save: %v", err)
	}
	if _, err := guildCall(nk, map[string]any{"op": "deposit_gold", "actor_id": member, "amount": 100, "request_id": "r0", "save": saveOf(lead, "*", 0)}); errorCode(err) != codeInvalidArgument {
		t.Fatalf("someone else's save: %v", err)
	}

	// Deposit: the bank and the member's save (gold 400 left of 500) in one transaction.
	deposit := map[string]any{"op": "deposit_gold", "actor_id": member, "amount": 100, "request_id": "r1", "save": saveOf(member, "*", 400)}
	done := guildDo(t, nk, deposit)
	expectGuild(t, done, guildOK)
	gold, version := storedSave(t, nk, member)
	if gold != 400 || done.SaveVersion != version || version == "" {
		t.Fatalf("save after deposit: gold %d version %q answer %q", gold, version, done.SaveVersion)
	}
	if int64(done.Guilds[id].Bank.Gold) != 100 || storedGuild(t, nk, id).Bank.Gold != 100 {
		t.Fatalf("bank after deposit: %+v", storedGuild(t, nk, id).Bank)
	}
	var marker map[string]any
	if !nk.object(collectionGuildRequests, systemUserID, "r1", &marker) || marker["guild_id"] != id {
		t.Fatalf("request marker: %v", marker)
	}
	var entry guildLogEntry
	if !nk.object(collectionGuildBankLog, systemUserID, guildLogKey(id, 1), &entry) || entry.Action != actionDepositGold || entry.Gold != 100 || entry.PlayerID != member {
		t.Fatalf("append-only log entry: %+v", entry)
	}

	// The same request again (its answer was lost): replayed, not applied twice.
	again := guildDo(t, nk, deposit)
	if again.Result != guildOK || !again.Replayed || again.SaveVersion != version || storedGuild(t, nk, id).Bank.Gold != 100 {
		t.Fatalf("replay: %+v, bank %d", again, storedGuild(t, nk, id).Bank.Gold)
	}

	// A refusal by the rules writes nothing, not even the save.
	refused := guildDo(t, nk, map[string]any{"op": "withdraw_gold", "actor_id": member, "amount": 50, "request_id": "r2", "save": saveOf(member, version, 450)})
	expectGuild(t, refused, guildNotPermitted)
	if gold, after := storedSave(t, nk, member); gold != 400 || after != version {
		t.Fatalf("refused withdraw wrote the save: %d %q", gold, after)
	}

	// A stale save version: refused (409), nothing written.
	if _, err := guildCall(nk, map[string]any{"op": "deposit_gold", "actor_id": member, "amount": 10, "request_id": "r3", "save": saveOf(member, "stale", 390)}); errorCode(err) != codeAborted {
		t.Fatalf("stale save: %v", err)
	}
	if storedGuild(t, nk, id).Bank.Gold != 100 {
		t.Fatalf("stale save changed the bank")
	}
	// A lost lease: refused (400 code 9), nothing written.
	lostLease := saveOf(member, version, 390)
	lostLease["lease_token"] = "someone-else"
	if _, err := guildCall(nk, map[string]any{"op": "deposit_gold", "actor_id": member, "amount": 10, "request_id": "r4", "save": lostLease}); errorCode(err) != codeFailedPrecondition {
		t.Fatalf("lost lease: %v", err)
	}

	// Concurrent withdrawals: another officer's withdrawal lands between this one's read and write. The
	// transaction is refused, runs again on the new bank, and the rules answer (not enough gold).
	_, leadVersion := storedSave(t, nk, lead)
	if leadVersion == "" {
		leadVersion = "*"
	}
	nk.beforeMultiUpdate = func() {
		g := storedGuild(t, nk, id)
		g.Bank.Gold -= 80
		value, _ := json.Marshal(g)
		nk.storage[storageKey(collectionGuilds, systemUserID, id)] = storedObject{value: string(value), version: "changed-elsewhere"}
	}
	raced := guildDo(t, nk, map[string]any{"op": "withdraw_gold", "actor_id": lead, "amount": 60, "request_id": "r5", "save": saveOf(lead, leadVersion, 60)})
	expectGuild(t, raced, guildNotEnoughGold)
	if storedGuild(t, nk, id).Bank.Gold != 20 {
		t.Fatalf("bank after the race: %d", storedGuild(t, nk, id).Bank.Gold)
	}
	if gold, _ := storedSave(t, nk, lead); gold == 60 {
		t.Fatalf("the refused withdrawal's save was written")
	}

	// Items: deposit (the bank places it), give to another member (their save), withdraw.
	_, version = storedSave(t, nk, member)
	item := map[string]any{"item_id": "sword_one_handed", "rarity": 2, "count": 1, "width": 1, "height": 3, "payload": `{"affixes":[["sharp",3]]}`, "slot_id": 77, "x": 9, "y": 9}
	in := guildDo(t, nk, map[string]any{"op": "deposit_item", "actor_id": member, "item": item, "request_id": "r6", "save": saveOf(member, version, 400)})
	expectGuild(t, in, guildOK)
	if in.Item == nil || in.Item.SlotID != 1 || in.Item.X != 0 || in.Item.Y != 0 || in.Item.Payload != `{"affixes":[["sharp",3]]}` {
		t.Fatalf("deposited item: %+v", in.Item)
	}
	if bank := in.Guilds[id].Bank; len(bank.Items) != 1 || bank.Width != 10 || bank.Height != 30 {
		t.Fatalf("bank view: %+v", bank)
	}
	_, officerVersion := storedSave(t, nk, officer)
	if officerVersion == "" {
		officerVersion = "*"
	}
	// A give needs the recipient's save, not the giver's.
	if _, err := guildCall(nk, map[string]any{"op": "give_item", "actor_id": lead, "target_id": officer, "slot_id": 1, "request_id": "r7", "save": saveOf(lead, leadVersion, 0)}); errorCode(err) != codeInvalidArgument {
		t.Fatalf("give with the giver's save: %v", err)
	}
	given := guildDo(t, nk, map[string]any{"op": "give_item", "actor_id": lead, "target_id": officer, "slot_id": 1, "request_id": "r8", "save": saveOf(officer, officerVersion, 1)})
	expectGuild(t, given, guildOK)
	if given.Item == nil || given.Item.ItemID != "sword_one_handed" || len(given.Guilds[id].Bank.Items) != 0 {
		t.Fatalf("given: %+v", given)
	}
	if gold, _ := storedSave(t, nk, officer); gold != 1 {
		t.Fatalf("recipient's save not written")
	}
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "withdraw_item", "actor_id": officer, "slot_id": 1, "request_id": "r9", "save": saveOf(officer, given.SaveVersion, 1)}), guildUnknownItem)

	// The log: every bank action, newest first, also through crossguard_guild_log.
	logBody, _ := json.Marshal(map[string]any{"guild_id": id, "limit": 10})
	out, err := RpcGuildLogFn(serverCtx(), &fakeLogger{}, nil, nk, string(logBody))
	if err != nil {
		t.Fatalf("guild log: %v", err)
	}
	var history guildLogResponse
	_ = json.Unmarshal([]byte(out), &history)
	actions := []string{}
	for _, logged := range history.Entries {
		actions = append(actions, logged.Action)
	}
	if fmt.Sprint(actions) != fmt.Sprint([]string{actionRedistributeItem, actionDepositItem, actionDepositGold}) {
		t.Fatalf("history: %v (%s)", actions, out)
	}
}

func TestGuildLastMemberTakesTheBank(t *testing.T) {
	setClock(t, time.Unix(1_800_000_000, 0))
	lead := uid(1)
	nk := newFakeNakama(lead)
	id := foundGuild(t, nk, lead, "Lonely")
	done := guildDo(t, nk, map[string]any{"op": "deposit_gold", "actor_id": lead, "amount": 30, "request_id": "d1", "save": saveOf(lead, "*", 0)})
	item := map[string]any{"item_id": "shield", "count": 1, "width": 2, "height": 2}
	stored := guildDo(t, nk, map[string]any{"op": "deposit_item", "actor_id": lead, "item": item, "request_id": "d2", "save": saveOf(lead, done.SaveVersion, 0)})
	expectGuild(t, stored, guildOK)

	// Without the save, or with a bank that is not the stored one: refused, nothing changes.
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "leave", "actor_id": lead}), guildBankChanged)
	wrong := guildDo(t, nk, map[string]any{"op": "leave", "actor_id": lead, "request_id": "l1", "save": saveOf(lead, stored.SaveVersion, 30),
		"expected_bank": map[string]any{"gold": 30, "slot_ids": []int{}}})
	expectGuild(t, wrong, guildBankChanged)
	if storedGuild(t, nk, id) == nil {
		t.Fatalf("refused leave disbanded")
	}
	left := guildDo(t, nk, map[string]any{"op": "leave", "actor_id": lead, "request_id": "l2", "save": saveOf(lead, stored.SaveVersion, 30),
		"expected_bank": map[string]any{"gold": 30, "slot_ids": []int{stored.Item.SlotID}}})
	expectGuild(t, left, guildOK)
	if !left.Disbanded || left.Payout == nil || int64(left.Payout.Gold) != 30 || len(left.Payout.Items) != 1 {
		t.Fatalf("payout: %+v", left)
	}
	if gold, version := storedSave(t, nk, lead); gold != 30 || version != left.SaveVersion {
		t.Fatalf("leaver's save: %d %q (%q)", gold, version, left.SaveVersion)
	}
	if storedGuild(t, nk, id) != nil || nk.groupStates(id) != nil {
		t.Fatalf("guild not disbanded")
	}
	// The history outlives the guild.
	logBody, _ := json.Marshal(map[string]any{"guild_id": id, "before_seq": 3})
	out, _ := RpcGuildLogFn(serverCtx(), &fakeLogger{}, nil, nk, string(logBody))
	var history guildLogResponse
	_ = json.Unmarshal([]byte(out), &history)
	if len(history.Entries) != 2 || history.Entries[0].Action != actionDepositItem {
		t.Fatalf("history after disband: %s", out)
	}
}

func TestGuildCreateReplacesLeftoverGroup(t *testing.T) {
	clock := setClock(t, time.Unix(1_800_000_000, 0))
	lead, other := uid(1), uid(2)
	nk := newFakeNakama(lead, other)
	// A group named like the guild, with no guild behind it (a failed delete), a minute old.
	if _, err := nk.GroupCreate(serverCtx(), other, "Old Name", other, "", "", "", false, nil, 50); err != nil {
		t.Fatal(err)
	}
	// Young: maybe a create still on its way.
	expectGuild(t, guildDo(t, nk, map[string]any{"op": "create", "actor_id": lead, "name": "Old Name"}), guildNameTaken)
	*clock = clock.Add(2 * time.Minute)
	created := guildDo(t, nk, map[string]any{"op": "create", "actor_id": lead, "name": "Old Name"})
	expectGuild(t, created, guildOK)
	if len(nk.groups) != 1 || nk.groupStates(created.GuildID)[lead] != 0 {
		t.Fatalf("groups: %v", nk.groups)
	}
}

func TestGuildGoldTextKeepsInt64(t *testing.T) {
	out, _ := json.Marshal(guildBankView{Gold: goldText(maxInt64)})
	if string(out) != `{"gold":"9223372036854775807","width":0,"height":0,"next_slot_id":0,"items":null}` {
		t.Fatalf("gold as text: %s", out)
	}
}
