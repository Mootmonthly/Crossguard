#!/usr/bin/env python3
"""End-to-end check of a running local backend (Tools/run_local_backend): logs in with device ids,
verifies sessions the way a dedicated server does, and walks through what the game servers do:

  1. login and session verification (tampered token, wrong HTTP key);
  2. character saves under a save lease: load, write, stale write (version conflict), quarantine,
     2 MB record, the refusals (no lease, client calls, client storage writes);
  3. save lease hand-off hub -> raid -> hub (held, release, acquire, old token refused, renew,
     expired lease taken over);
  4. parties: invite, inbox, accept, decline, leave, still there after the hand-off, client refusals;
  5. matchmaking: a match server and a hub register, two parties queue and land on the same match
     server with one ticket each, ticket checks (valid, wrong server, another player's, unknown,
     expired when the ticket TTL is short), match over, hub pick (the party's hub), deregister;
  6. guilds: create / invite / accept / rank / kick / leave across two "hub shards" (server ids), the
     Nakama group mirror, permission refusals, client refusals, the bank: deposits and withdrawals that
     carry the player's save (written together), a retried request applied once, stale saves refused,
     concurrent withdrawals of the same gold and item (never more than the bank holds, the item once),
     the append-only log, the last member taking the bank;
  7. sessions: refresh token flow (new token verifies, bad refresh token refused), Steam login reaches
     Nakama's Steam check (refused without a real ticket; "not configured" without STEAM_* keys).

Exit code 0 when everything behaves. Reads keys from Backend/.env. docs/backend-setup.md.

Needs CROSSGUARD_DEV_AUTH=1 (device login) and CROSSGUARD_MATCH_MIN_TEAMS of 2 (the default). Creates
throwaway accounts on every run. The expired-ticket check waits for the ticket TTL when it is 15 s or
less (CROSSGUARD_MATCH_TICKET_TTL_SEC=10), else it is skipped; the expired-lease check waits 11 s.
"""
import base64
import json
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:7350"
RUN = uuid.uuid4().hex[:8]


def read_env():
    env = {}
    for line in (ROOT / "Backend" / ".env").read_text().splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            env[key.strip()] = value.strip()
    return env


def basic(user):
    return "Basic " + base64.b64encode(f"{user}:".encode()).decode()


def call(method, path, body=None, auth=None):
    data = None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode())
    request = urllib.request.Request(URL + path, data=data, method=method)
    request.add_header("Content-Type", "application/json")
    if auth:
        request.add_header("Authorization", auth)
    # The local backend: never through a proxy.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(request, timeout=10) as response:
            return response.status, json.loads(response.read() or b"null")
    except urllib.error.HTTPError as error:
        text = error.read()
        try:
            return error.code, json.loads(text)
        except ValueError:
            return error.code, text.decode(errors="replace")


failures = []


def check(name, condition, detail=""):
    print(("ok   " if condition else "FAIL ") + name + ("" if condition else f"  ({detail})"))
    if not condition:
        failures.append(name)
    return condition


class Backend:
    def __init__(self, env):
        self.env = env
        self.http_key = basic(env["NAKAMA_HTTP_KEY"])

    def rpc(self, rpc_id, payload, auth=None):
        return call("POST", f"/v2/rpc/{rpc_id}?unwrap", payload, auth or self.http_key)

    def login(self, name):
        """A fresh device account: (session token, user id)."""
        device = f"smoke-{RUN}-{name}-" + uuid.uuid4().hex
        status, session = call("POST", "/v2/account/authenticate/device?create=true", {"id": device}, basic(self.env["NAKAMA_SERVER_KEY"]))
        if status != 200:
            raise SystemExit(f"device login failed: {status} {session} (CROSSGUARD_DEV_AUTH=1 in Backend/.env?)")
        status, who = self.rpc("crossguard_verify_session", {"token": session["token"]})
        if status != 200:
            raise SystemExit(f"verify session failed: {status} {who}")
        return session["token"], who["user_id"]

    def acquire(self, user_id, server_id, ttl=60):
        return self.rpc("crossguard_lease_acquire", {"user_id": user_id, "server_id": server_id, "ttl_sec": ttl})

    def lease(self, user_id, server_id, ttl=60):
        status, body = self.acquire(user_id, server_id, ttl)
        if status != 200:
            raise SystemExit(f"lease acquire for {user_id} on {server_id} failed: {status} {body}")
        return body["lease_token"]

    def party(self, op, actor, target="", party_id="", server_id=""):
        return self.rpc("crossguard_party", {"op": op, "actor_id": actor, "target_id": target, "party_id": party_id, "server_id": server_id})


def check_login_and_saves(backend):
    env = backend.env
    # 1. Client login (development device id) and the dedicated server's check.
    device = "smoke-" + uuid.uuid4().hex
    status, session = call("POST", "/v2/account/authenticate/device?create=true", {"id": device}, basic(env["NAKAMA_SERVER_KEY"]))
    check("device login", status == 200 and "token" in session, f"{status} {session}")
    if status != 200:
        return None
    token = session["token"]
    status, who = backend.rpc("crossguard_verify_session", {"token": token})
    check("verify session", status == 200 and who.get("user_id"), f"{status} {who}")
    user_id = who["user_id"]
    status, body = backend.rpc("crossguard_verify_session", {"token": token[:-4] + "AAAA"})
    check("tampered token refused (401)", status == 401, f"{status} {body}")
    status, body = backend.rpc("crossguard_verify_session", {"token": token}, auth=basic("wrong-key"))
    check("wrong http key refused (401)", status == 401, f"{status} {body}")

    # 2. Saves, under a lease.
    hub = f"smoke-hub-{RUN}"
    ref = {"user_id": user_id, "character_id": "main"}
    status, lease = backend.acquire(user_id, hub)
    check("lease acquire (new character)", status == 200 and lease.get("lease_token") and lease.get("save_version") == "*", f"{status} {lease}")
    token_hub = lease["lease_token"]

    status, loaded = backend.rpc("crossguard_save_load", ref)
    check("load new character", status == 200 and loaded == {"found": False, "version": "*", "record": None}, f"{status} {loaded}")

    record = {"version": 1, "playerId": user_id, "gold": 10, "stash": {"purchasedTabs": 0, "tabs": [{"items": []}]}}
    status, body = backend.rpc("crossguard_save_write", dict(ref, record=record, expected_version="*"))
    check("write without a lease token refused (400)", status == 400 and "lease_token" in str(body), f"{status} {body}")
    status, body = backend.rpc("crossguard_save_write", dict(ref, record=record, expected_version="*", lease_token="not-the-lease"))
    check("write with a wrong lease token refused (400)", status == 400 and "lease" in str(body), f"{status} {body}")

    status, written = backend.rpc("crossguard_save_write", dict(ref, record=record, expected_version="*", lease_token=token_hub))
    check("first write", status == 200 and written.get("version"), f"{status} {written}")
    first_version = written.get("version")

    status, loaded = backend.rpc("crossguard_save_load", ref)
    check("load after write", status == 200 and loaded["found"] and loaded["version"] == first_version and loaded["record"]["gold"] == 10,
          f"{status} {loaded}")

    record["gold"] = 20
    status, written = backend.rpc("crossguard_save_write", dict(ref, record=record, expected_version=first_version, lease_token=token_hub))
    check("write with loaded version", status == 200 and written.get("version") not in (None, first_version), f"{status} {written}")
    second_version = written.get("version")

    record["gold"] = 30
    status, body = backend.rpc("crossguard_save_write", dict(ref, record=record, expected_version=first_version, lease_token=token_hub))
    check("stale write refused (409)", status == 409 and "version conflict" in str(body), f"{status} {body}")
    status, loaded = backend.rpc("crossguard_save_load", ref)
    check("stale write did not land", loaded["record"]["gold"] == 20, f"{loaded}")

    status, body = backend.rpc("crossguard_save_write", dict(ref, record=dict(record, playerId="someone-else"), expected_version="*", lease_token=token_hub))
    check("record for another player refused (400)", status == 400, f"{status} {body}")

    status, body = backend.rpc("crossguard_save_quarantine", dict(ref, reason="smoke test"))
    check("quarantine", status == 200 and body.get("quarantined") is True, f"{status} {body}")

    # Clients cannot use the server RPCs or write the collections.
    bearer = "Bearer " + token
    status, body = backend.rpc("crossguard_save_load", ref, auth=bearer)
    check("client cannot call save RPC (403)", status == 403, f"{status} {body}")
    status, body = backend.rpc("crossguard_lease_acquire", dict(ref, server_id="x"), auth=bearer)
    check("client cannot take a lease (403)", status == 403, f"{status} {body}")
    for collection in ("characters", "save_leases"):
        status, body = call("PUT", "/v2/storage", {"objects": [{"collection": collection, "key": "main", "value": json.dumps(record)}]}, bearer)
        check(f"client cannot write {collection} (403)", status == 403, f"{status} {body}")
    status, body = call("GET", f"/v2/storage/characters?user_id={user_id}", None, bearer)
    check("client can read own save", status == 200 and len(body.get("objects", [])) == 1, f"{status} {body}")

    # Large record (well over Nakama's default 256 KB request cap).
    big = dict(record, pad="x" * (2 * 1024 * 1024))
    status, written = backend.rpc("crossguard_save_write", dict(ref, record=big, expected_version=second_version, lease_token=token_hub))
    check("2 MB record accepted", status == 200, f"{status} {str(written)[:200]}")
    return user_id, token_hub, written.get("version")


def check_lease_handoff(backend, user_id, token_hub, version):
    hub, raid = f"smoke-hub-{RUN}", f"smoke-raid-{RUN}"
    ref = {"user_id": user_id, "character_id": "main"}
    record = {"version": 1, "playerId": user_id, "gold": 40}

    # The player travels to a raid: the raid server's login comes while the hub still holds the lease.
    status, body = backend.acquire(user_id, raid)
    check("lease held by the hub: raid server must wait (409)", status == 409 and hub in str(body), f"{status} {body}")
    status, body = backend.acquire(user_id, hub)
    check("holder re-acquires: same token", status == 200 and body.get("lease_token") == token_hub, f"{status} {body}")
    # The hub writes its last save, then releases.
    status, written = backend.rpc("crossguard_save_write", dict(ref, record=record, expected_version=version, lease_token=token_hub))
    check("hub's last write before the travel", status == 200, f"{status} {written}")
    version = written.get("version")
    status, body = backend.rpc("crossguard_lease_release", dict(ref, server_id=hub, lease_token=token_hub))
    check("hub releases the lease", status == 200 and body == {"released": True}, f"{status} {body}")
    status, body = backend.rpc("crossguard_lease_release", dict(ref, server_id=hub, lease_token=token_hub))
    check("second release does nothing", status == 200 and body == {"released": False}, f"{status} {body}")
    status, lease = backend.acquire(user_id, raid)
    check("raid server acquires; sees the hub's last version", status == 200 and lease.get("save_version") == version and not lease.get("took_over_from"),
          f"{status} {lease} (want version {version})")
    token_raid = lease.get("lease_token")

    # The hub's old token is dead: no write, no renew.
    status, body = backend.rpc("crossguard_save_write", dict(ref, record=dict(record, gold=999), expected_version=version, lease_token=token_hub))
    check("hub's old token cannot write (400 lease)", status == 400 and "lease" in str(body), f"{status} {body}")
    status, body = backend.rpc("crossguard_lease_renew", dict(ref, server_id=hub, lease_token=token_hub, ttl_sec=60))
    check("hub's old token cannot renew (400)", status == 400, f"{status} {body}")
    status, loaded = backend.rpc("crossguard_save_load", ref)
    check("refused write did not land", status == 200 and loaded["record"]["gold"] == 40, f"{status} {loaded}")
    status, body = backend.rpc("crossguard_lease_renew", dict(ref, server_id=raid, lease_token=token_raid, ttl_sec=60))
    check("raid server renews", status == 200 and body.get("expires_at_ms", 0) > 0, f"{status} {body}")
    status, written = backend.rpc("crossguard_save_write", dict(ref, record=dict(record, gold=50), expected_version=version, lease_token=token_raid))
    check("raid server writes", status == 200, f"{status} {written}")

    # The raid server crashes (no release): the lease expires and the hub may take it over.
    status, body = backend.rpc("crossguard_lease_renew", dict(ref, server_id=raid, lease_token=token_raid, ttl_sec=10))
    print("     (waiting 11 s for a 10 s lease to expire)")
    time.sleep(11)
    status, lease = backend.acquire(user_id, hub)
    check("expired lease taken over", status == 200 and lease.get("took_over_from") == raid, f"{status} {lease}")
    status, body = backend.rpc("crossguard_save_write", dict(ref, record=dict(record, gold=60), expected_version=written.get("version"), lease_token=token_raid))
    check("crashed server's token cannot write", status == 400, f"{status} {body}")


def check_parties(backend):
    hub, other_hub, raid = f"smoke-hub-{RUN}", f"smoke-hub2-{RUN}", f"smoke-raid-{RUN}"
    (token_a, a), (_, b), (_, c) = backend.login("a"), backend.login("b"), backend.login("c")
    for user in (a, b, c):
        backend.lease(user, hub)

    status, invited = backend.party("invite", a, b, server_id=hub)
    party_id = invited.get("party_id") if status == 200 else None
    check("invite founds a party", status == 200 and invited.get("result") == "None" and party_id and invited["players"][a]["party_id"] == party_id,
          f"{status} {invited}")
    if not party_id:
        return None
    check("invitee's inbox", invited["players"][b]["invites"][0]["party_id"] == party_id and invited["players"][b]["invites"][0]["invited_by"] == a,
          f"{invited['players'].get(b)}")
    status, body = backend.party("accept", b, party_id=party_id, server_id=hub)
    check("accept", status == 200 and body.get("result") == "None" and body["parties"][party_id]["members"] == [a, b], f"{status} {body}")
    status, body = backend.party("invite", b, c, server_id=hub)
    check("member cannot invite (NotLeader)", status == 200 and body.get("result") == "NotLeader", f"{status} {body}")
    status, body = backend.party("invite", a, c, server_id=hub)
    check("leader invites a third", status == 200 and body.get("result") == "None", f"{status} {body}")
    status, body = backend.party("decline", c, party_id=party_id, server_id=hub)
    check("decline", status == 200 and body.get("result") == "None" and body["players"][c]["invites"] == [], f"{status} {body}")

    # The party lives in Nakama: still there after the members' hand-off to a raid server and to another hub.
    for user in (a, b):
        token = backend.lease(user, hub)
        backend.rpc("crossguard_lease_release", {"user_id": user, "server_id": hub, "lease_token": token})
        backend.lease(user, raid)
    status, body = backend.rpc("crossguard_party_get", {"user_ids": [a, b, c]})
    check("party persists across the hand-off", status == 200 and body["players"][a]["party_id"] == party_id and body["players"][b]["party_id"] == party_id
          and body["parties"][party_id]["leader_id"] == a and body["players"][c]["party_id"] == "", f"{status} {body}")
    for user in (a, b):
        token = backend.lease(user, raid)
        backend.rpc("crossguard_lease_release", {"user_id": user, "server_id": raid, "lease_token": token})
        backend.lease(user, other_hub)
    status, body = backend.rpc("crossguard_party_get", {"user_ids": [b]})
    check("party seen from another hub shard", status == 200 and body["players"][b]["party_id"] == party_id, f"{status} {body}")

    # Clients: read their own membership, never write it or call the party RPCs.
    bearer = "Bearer " + token_a
    status, body = backend.rpc("crossguard_party", {"op": "leave", "actor_id": a}, auth=bearer)
    check("client cannot call the party RPC (403)", status == 403, f"{status} {body}")
    status, body = call("PUT", "/v2/storage", {"objects": [{"collection": "party_members", "key": "current", "value": "{}"}]}, bearer)
    check("client cannot write party_members (403)", status == 403, f"{status} {body}")
    status, body = call("GET", f"/v2/storage/party_members?user_id={a}", None, bearer)
    check("client reads own party membership", status == 200 and party_id in json.dumps(body), f"{status} {body}")

    # Leave: the leader goes, the lead passes on; the last one out disbands it.
    status, body = backend.party("leave", a, server_id=other_hub)
    check("leader leaves: lead passes on", status == 200 and body["parties"][party_id]["leader_id"] == b, f"{status} {body}")
    status, body = backend.party("leave", b, server_id=other_hub)
    check("last member leaves: disbanded", status == 200 and body.get("disbanded") is True, f"{status} {body}")
    status, body = backend.party("leave", b, server_id=other_hub)
    check("leave again: NotInParty", status == 200 and body.get("result") == "NotInParty", f"{status} {body}")
    return True


def check_matchmaking(backend):
    hub, match, other_match = f"smoke-hub-{RUN}", f"smoke-match-{RUN}", f"smoke-match2-{RUN}"
    hub_address, match_address = "127.0.0.1:17777", "127.0.0.1:17778"
    status, registered = backend.rpc("crossguard_server_register", {"server_id": match, "kind": "match", "address": match_address})
    check("match server registers (48 players by default)", status == 200 and registered.get("capacity") == 48, f"{status} {registered}")
    ticket_ttl = registered.get("ticket_ttl_sec", 120) if status == 200 else 120
    status, body = backend.rpc("crossguard_server_register", {"server_id": hub, "kind": "hub", "address": hub_address, "capacity": 64})
    check("hub server registers", status == 200, f"{status} {body}")
    status, body = backend.rpc("crossguard_server_heartbeat", {"server_id": match, "players": 0})
    check("heartbeat", status == 200 and body == {"known": True}, f"{status} {body}")
    status, body = backend.rpc("crossguard_server_heartbeat", {"server_id": f"smoke-unknown-{RUN}", "players": 0})
    check("heartbeat from an unknown server: register again", status == 200 and body == {"known": False}, f"{status} {body}")

    players = {name: backend.login(name) for name in ("a", "b", "c", "d")}
    ids = {name: user for name, (_, user) in players.items()}
    for user in ids.values():
        backend.lease(user, hub)
    party_ab = backend.party("invite", ids["a"], ids["b"], server_id=hub)[1]["party_id"]
    backend.party("accept", ids["b"], party_id=party_ab, server_id=hub)
    party_cd = backend.party("invite", ids["c"], ids["d"], server_id=hub)[1]["party_id"]
    backend.party("accept", ids["d"], party_id=party_cd, server_id=hub)

    status, body = backend.rpc("crossguard_match_request", {"party_id": party_ab, "user_ids": [ids["a"], ids["c"]], "server_id": hub})
    check("request with a player outside the party refused (400)", status == 400, f"{status} {body}")
    status, first = backend.rpc("crossguard_match_request", {"party_id": party_ab, "user_ids": [ids["a"], ids["b"]], "server_id": hub})
    check("first party queued", status == 200 and first.get("state") in ("queued", "assigned"), f"{status} {first}")
    status, body = backend.rpc("crossguard_match_request", {"party_id": party_ab, "user_ids": [ids["a"], ids["b"]], "server_id": hub})
    check("same party twice refused (409)", status == 409, f"{status} {body}")
    status, second = backend.rpc("crossguard_match_request", {"party_id": party_cd, "user_ids": [ids["c"], ids["d"]], "server_id": hub})
    status, first = backend.rpc("crossguard_match_status", {"request_id": first.get("request_id", "")})
    both = first.get("state") == "assigned" and second.get("state") == "assigned"
    check("two parties: one match server", both and first["match_id"] == second["match_id"] and first["address"] == second["address"],
          f"{first} / {second} (another registered match server or CROSSGUARD_MATCH_MIN_TEAMS=1 can split them)")
    if not both:
        return
    check("the registered smoke match server", first["server_id"] == match and first["address"] == match_address,
          f"{first['server_id']} {first['address']} (a match server left over from another run was free)")
    server_id = first["server_id"]
    tickets = dict(first["tickets"], **second["tickets"])
    check("one ticket per player", sorted(tickets) == sorted(ids.values()) and len(set(tickets.values())) == 4, f"{tickets}")

    def verify(ticket, server, user):
        return backend.rpc("crossguard_ticket_verify", {"ticket": ticket, "server_id": server, "user_id": user})

    status, body = verify(tickets[ids["a"]], server_id, ids["a"])
    check("ticket verifies: player, party, match", status == 200 and body.get("user_id") == ids["a"] and body.get("party_id") == party_ab
          and body.get("match_id") == first["match_id"], f"{status} {body}")
    status, body = verify(tickets[ids["c"]], server_id, ids["c"])
    check("other party's ticket gives its party", status == 200 and body.get("party_id") == party_cd, f"{status} {body}")
    status, body = verify(tickets[ids["a"]], other_match, ids["a"])
    check("ticket on another server refused (403)", status == 403, f"{status} {body}")
    status, body = verify(tickets[ids["a"]], server_id, ids["b"])
    check("another player's ticket refused (403)", status == 403, f"{status} {body}")
    status, body = verify("0" * 32, server_id, ids["a"])
    check("unknown ticket refused (401)", status == 401, f"{status} {body}")
    status, body = backend.rpc("crossguard_ticket_verify", {"ticket": tickets[ids["a"]], "server_id": server_id, "user_id": ids["a"]},
                               auth="Bearer " + players["a"][0])
    check("client cannot verify tickets (403)", status == 403, f"{status} {body}")
    status, body = backend.rpc("crossguard_match_request", {"user_ids": [ids["a"]]}, auth="Bearer " + players["a"][0])
    check("client cannot queue (403)", status == 403, f"{status} {body}")
    if ticket_ttl <= 15:
        print(f"     (waiting {ticket_ttl + 1} s for the ticket TTL)")
        time.sleep(ticket_ttl + 1)
        status, body = verify(tickets[ids["d"]], server_id, ids["d"])
        check("unused ticket expired (401)", status == 401 and "expired" in str(body), f"{status} {body}")
        status, body = verify(tickets[ids["a"]], server_id, ids["a"])
        check("used ticket still valid for a reconnect", status == 200, f"{status} {body}")
    else:
        print(f"skip expired-ticket check (ticket TTL {ticket_ttl} s; CROSSGUARD_MATCH_TICKET_TTL_SEC=10 checks it)")

    # Back to the hub: the party's hub (it launched from there) is picked.
    status, body = backend.rpc("crossguard_hub_find", {"user_id": ids["a"], "party_id": party_ab})
    check("hub pick: the party's hub", status == 200 and body.get("server_id") == hub and body.get("address") == hub_address, f"{status} {body}")
    status, body = backend.rpc("crossguard_hub_find_mine", {}, auth="Bearer " + players["b"][0])
    check("client finds its hub (menu Play)", status == 200 and body.get("server_id") == hub, f"{status} {body}")

    status, body = backend.rpc("crossguard_match_over", {"server_id": server_id, "match_id": first["match_id"]})
    check("match over frees the server", status == 200 and body == {"ok": True}, f"{status} {body}")
    for server in (match, hub):
        status, body = backend.rpc("crossguard_server_deregister", {"server_id": server})
        check(f"{server.split('-')[1]} server deregisters", status == 200 and body == {"ok": True}, f"{status} {body}")


class Player:
    """A logged-in player whose save a hub server holds (lease + save version), for guild bank calls."""

    def __init__(self, backend, name, hub):
        self.backend = backend
        self.token, self.id = backend.login(name)
        self.lease = backend.lease(self.id, hub)
        self.version = "*"
        self.gold = 1000

    def save(self, gold=None):
        """The save after the operation (gold: the banked gold it would leave)."""
        return {"user_id": self.id, "record": {"version": 1, "playerId": self.id, "gold": self.gold if gold is None else gold},
                "expected_version": self.version, "lease_token": self.lease}

    def stored(self):
        status, body = self.backend.rpc("crossguard_save_load", {"user_id": self.id, "character_id": "main"})
        return (body.get("version"), (body.get("record") or {}).get("gold")) if status == 200 else (None, None)


def check_guilds(backend):
    hub_a, hub_b = f"smoke-guild-hub-a-{RUN}", f"smoke-guild-hub-b-{RUN}"
    lead, officer, member, outsider = (Player(backend, name, hub_a) for name in ("glead", "gofficer", "gmember", "goutsider"))
    name = f"Smoke {RUN}"

    def guild(op, actor, server=hub_a, **fields):
        return backend.rpc("crossguard_guild", dict(op=op, actor_id=actor.id, server_id=server, **fields))

    # Membership, alternating between two hub shards: every answer comes from Nakama's storage.
    status, created = guild("create", lead, name=f"  {name} ")
    check("guild create", status == 200 and created.get("result") == "None" and created["guilds"][created["guild_id"]]["name"] == name,
          f"{status} {created}")
    gid = created.get("guild_id")
    status, body = guild("create", outsider, server=hub_b, name=name.upper())
    check("guild name taken ignoring case", status == 200 and body.get("result") == "NameTaken", f"{status} {body}")
    status, body = guild("invite", lead, target_id=officer.id)
    check("guild invite (hub A)", status == 200 and body.get("result") == "None", f"{status} {body}")
    status, seen = backend.rpc("crossguard_guild_get", {"user_ids": [officer.id]})
    invites = seen.get("players", {}).get(officer.id, {}).get("invites", []) if status == 200 else []
    check("invite seen from hub B", len(invites) == 1 and invites[0]["guild_id"] == gid and invites[0]["guild_name"] == name, f"{status} {seen}")
    status, body = guild("accept", officer, server=hub_b, guild_id=gid)
    check("guild accept (hub B)", status == 200 and body.get("result") == "None", f"{status} {body}")
    for player in (member, outsider):
        guild("invite", lead, target_id=player.id)
        status, body = guild("accept", player, server=hub_b, guild_id=gid)
    check("third and fourth members join", status == 200 and len(body["guilds"][gid]["members"]) == 4, f"{status} {body}")
    status, body = guild("promote", lead, server=hub_b, target_id=officer.id)
    check("promote to officer (hub B)", status == 200 and body.get("result") == "None", f"{status} {body}")
    status, body = guild("kick", officer, target_id=outsider.id)
    check("officer kicks a member (hub A)", status == 200 and body.get("result") == "None" and body["players"][outsider.id]["guild_id"] == "",
          f"{status} {body}")
    status, seen = backend.rpc("crossguard_guild_get", {"user_ids": [lead.id, officer.id, member.id, outsider.id]})
    ranks = {m["user_id"]: m["rank"] for m in seen.get("guilds", {}).get(gid, {}).get("members", [])}
    check("roster and ranks persisted (fresh read)", ranks == {lead.id: "Leader", officer.id: "Officer", member.id: "Member"}
          and seen["players"][outsider.id]["guild_id"] == "", f"{status} {seen}")

    # The Nakama group mirrors it; clients may read, never change it.
    lead_bearer = "Bearer " + lead.token
    status, groups = call("GET", f"/v2/user/{lead.id}/group", None, lead_bearer)
    mine = [g for g in (groups.get("user_groups") or []) if g["group"]["id"] == gid] if status == 200 else []
    check("group mirror: the leader is superadmin", len(mine) == 1 and mine[0].get("state", 0) == 0, f"{status} {groups}")
    status, users = call("GET", f"/v2/group/{gid}/user", None, lead_bearer)
    states = {u["user"]["id"]: u.get("state", 0) for u in (users.get("group_users") or [])} if status == 200 else {}
    check("group mirror: ranks", states == {lead.id: 0, officer.id: 1, member.id: 2}, f"{status} {users}")
    for method, path, body in (("POST", "/v2/group", {"name": f"x{RUN}"}), ("POST", f"/v2/group/{gid}/join", {}),
                               ("POST", f"/v2/group/{gid}/kick?user_ids={member.id}", {}), ("POST", f"/v2/group/{gid}/promote?user_ids={member.id}", {}),
                               ("DELETE", f"/v2/group/{gid}", None)):
        status, answer = call(method, path, body, lead_bearer)
        check(f"client cannot {method} {path.split('?')[0].replace(gid, '<guild>')} (403)", status == 403, f"{status} {answer}")
    status, body = backend.rpc("crossguard_guild", {"op": "leave", "actor_id": lead.id}, auth=lead_bearer)
    check("client cannot call crossguard_guild (403)", status == 403, f"{status} {body}")
    for collection in ("guilds", "guild_members", "guild_bank_log"):
        status, body = call("PUT", "/v2/storage", {"objects": [{"collection": collection, "key": "x", "value": "{}"}]}, lead_bearer)
        check(f"client cannot write {collection} (403)", status == 403, f"{status} {body}")

    # Permission refusals (the rules' answers).
    for what, (op, actor, fields), want in (
            ("member invites", ("invite", member, {"target_id": outsider.id}), "NotPermitted"),
            ("officer kicks an officer's equal", ("kick", officer, {"target_id": lead.id}), "NotPermitted"),
            ("member promotes", ("promote", member, {"target_id": officer.id}), "NotPermitted"),
            ("leader leaves without transferring", ("leave", lead, {}), "LeaderMustTransfer")):
        status, body = guild(op, actor, **fields)
        check(f"refused: {what}", status == 200 and body.get("result") == want, f"{status} {body}")

    # Bank: the deposit and the player's save in one transaction.
    status, body = guild("deposit_gold", member, amount=100)
    check("bank op without the save refused (400)", status == 400, f"{status} {body}")
    request = {"amount": 1000, "request_id": f"dep-{RUN}", "save": member.save(gold=0)}
    status, body = guild("deposit_gold", member, **request)
    check("deposit gold with the save", status == 200 and body.get("result") == "None" and body["guilds"][gid]["bank"]["gold"] == "1000"
          and body.get("save_version"), f"{status} {body}")
    member.version = body.get("save_version")
    check("member's save written with it", member.stored() == (member.version, 0), f"{member.stored()}")
    status, body = guild("deposit_gold", member, **request)
    check("retried deposit replayed, not applied twice", status == 200 and body.get("replayed") is True and body["guilds"][gid]["bank"]["gold"] == "1000",
          f"{status} {body}")
    status, body = guild("withdraw_gold", member, amount=10, request_id=f"mw-{RUN}", save=member.save(gold=10))
    check("member withdrawal refused, save untouched", status == 200 and body.get("result") == "NotPermitted" and member.stored() == (member.version, 0),
          f"{status} {body}")
    status, body = guild("deposit_gold", member, amount=1, request_id=f"stale-{RUN}", save=dict(member.save(), expected_version="stale"))
    check("stale save refused (409)", status == 409, f"{status} {body}")
    status, body = guild("deposit_gold", outsider, amount=1, request_id=f"out-{RUN}", save=outsider.save(gold=999))
    check("outsider deposit refused", status == 200 and body.get("result") == "NotInGuild", f"{status} {body}")

    # Concurrent withdrawals: six officers race for 1000 gold, 300 each, and for one item.
    racers = [officer] + [Player(backend, f"gracer{i}", hub_a) for i in range(5)]
    for racer in racers[1:]:
        guild("invite", lead, target_id=racer.id)
        guild("accept", racer, guild_id=gid)
        guild("promote", lead, target_id=racer.id)
    item = {"item_id": "sword_one_handed", "rarity": 3, "count": 1, "width": 1, "height": 3, "payload": json.dumps({"affixes": [["sharp", 4]]})}
    status, body = guild("deposit_item", member, item=item, request_id=f"item-{RUN}", save=member.save())
    check("deposit item (the bank places it)", status == 200 and body.get("result") == "None" and body["item"]["slot_id"] == 1
          and body["item"]["payload"] == item["payload"], f"{status} {body}")
    member.version = body.get("save_version")

    def race(op, extra, results):
        def run(racer):
            status, body = guild(op, racer, request_id=f"{op}-{racer.id}", save=racer.save(gold=racer.gold + 300 if op == "withdraw_gold" else None), **extra)
            results.append((racer, status, body))
            if status == 200 and body.get("result") == "None":
                racer.version = body["save_version"]
        threads = [threading.Thread(target=run, args=(racer,)) for racer in racers]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

    gold_results, item_results = [], []
    race("withdraw_gold", {"amount": 300}, gold_results)
    winners = {r[0].id for r in gold_results if r[1] == 200 and r[2].get("result") == "None"}
    saved = [(r[0].stored()[1], r[0].id in winners) for r in gold_results]
    check("only the winners' saves were written", all((gold == 1300) == won and (won or gold is None) for gold, won in saved), f"{saved}")
    for racer in racers:
        racer.gold = 1300 if racer.id in winners else racer.gold
    race("withdraw_item", {"slot_id": 1}, item_results)
    gold_ok = [r for r in gold_results if r[1] == 200 and r[2].get("result") == "None"]
    item_ok = [r for r in item_results if r[1] == 200 and r[2].get("result") == "None"]
    odd = [r[1:] for r in gold_results + item_results if not (r[1] == 409 or (r[1] == 200 and r[2].get("result") in ("None", "NotEnoughGold", "UnknownItem")))]
    status, seen = backend.rpc("crossguard_guild_get", {"user_ids": [lead.id]})
    bank = seen["guilds"][gid]["bank"]
    check("concurrent gold withdrawals: never more than the bank held", 1 <= len(gold_ok) <= 3 and bank["gold"] == str(1000 - 300 * len(gold_ok)),
          f"{len(gold_ok)} succeeded, bank {bank['gold']}")
    check("concurrent item withdrawals: the item left exactly once", len(item_ok) == 1 and bank["items"] == [], f"{len(item_ok)} succeeded, {bank['items']}")
    check("racers got refusals or 'try again' (409), nothing else", not odd, f"{odd}")

    # The append-only log: every bank action, newest first.
    status, history = backend.rpc("crossguard_guild_log", {"guild_id": gid, "limit": 100})
    actions = [entry["action"] for entry in history.get("entries", [])] if status == 200 else []
    expected = ["WithdrawItem"] + ["WithdrawGold"] * len(gold_ok) + ["DepositItem", "DepositGold"]
    check("bank log: every deposit and withdrawal, newest first", actions == expected and history["log_seq"] == len(expected), f"{status} {actions}")

    # Leaving: everyone but the leader, then the leader takes what is left of the bank (with their save).
    for player in [member] + racers:
        guild("leave", player)
    status, seen = backend.rpc("crossguard_guild_get", {"user_ids": [lead.id]})
    bank = seen["guilds"][gid]["bank"]
    status, body = guild("leave", lead, request_id=f"last-{RUN}", save=lead.save(gold=1000 + int(bank["gold"])),
                          expected_bank={"gold": int(bank["gold"]) + 1, "slot_ids": []})
    check("last member's leave with a stale view of the bank refused", status == 200 and body.get("result") == "BankChanged", f"{status} {body}")
    status, body = guild("leave", lead, request_id=f"last2-{RUN}", save=lead.save(gold=1000 + int(bank["gold"])),
                          expected_bank={"gold": int(bank["gold"]), "slot_ids": []})
    check("last member takes the bank; guild disbanded", status == 200 and body.get("disbanded") is True and body["payout"]["gold"] == bank["gold"]
          and lead.stored()[1] == 1000 + int(bank["gold"]), f"{status} {body}")
    status, users = call("GET", f"/v2/group/{gid}/user", None, lead_bearer)
    check("group deleted with the guild", status == 404 or not (users.get("group_users") or []), f"{status} {users}")
    status, history = backend.rpc("crossguard_guild_log", {"guild_id": gid, "before_seq": len(expected) + 1})
    check("bank log outlives the guild", status == 200 and len(history.get("entries", [])) == len(expected), f"{status} {history}")
    status, body = guild("create", outsider, name=name)
    check("name free again", status == 200 and body.get("result") == "None", f"{status} {body}")


def check_sessions(backend):
    env = backend.env
    server_key = basic(env["NAKAMA_SERVER_KEY"])
    device = f"smoke-{RUN}-refresh-" + uuid.uuid4().hex
    status, session = call("POST", "/v2/account/authenticate/device?create=true", {"id": device}, server_key)
    check("login gives a refresh token", status == 200 and session.get("refresh_token"), f"{status} {session}")
    if status != 200:
        return
    time.sleep(1.1)  # Token times are in seconds: the refreshed one must differ.
    status, refreshed = call("POST", "/v2/account/session/refresh", {"token": session["refresh_token"]}, server_key)
    check("refresh gives a new session token", status == 200 and refreshed.get("token") and refreshed["token"] != session["token"], f"{status} {refreshed}")
    if status == 200:
        status, who = backend.rpc("crossguard_verify_session", {"token": refreshed["token"]})
        status_old, who_old = backend.rpc("crossguard_verify_session", {"token": session["token"]})
        check("refreshed token verifies as the same player", status == 200 and who.get("user_id") == who_old.get("user_id"), f"{status} {who} {who_old}")
    status, body = call("POST", "/v2/account/session/refresh", {"token": session["refresh_token"][:-4] + "AAAA"}, server_key)
    check("tampered refresh token refused (401)", status == 401, f"{status} {body}")
    status, body = backend.rpc("crossguard_verify_session", {"token": session["refresh_token"]})
    check("a refresh token is not a session token (401)", status == 401, f"{status} {body}")

    status, body = call("POST", "/v2/account/authenticate/steam?create=true", {"account": {"token": "00ff00ff"}}, server_key)
    if env.get("STEAM_APP_ID") and env.get("STEAM_PUBLISHER_KEY"):
        check("Steam login with a fake ticket refused by Steam", status not in (200, 403), f"{status} {body}")
    else:
        check("Steam login answers 'not configured' without STEAM_* keys", status == 400 and "not configured" in str(body), f"{status} {body}")


def main():
    backend = Backend(read_env())
    result = check_login_and_saves(backend)
    if result:
        check_lease_handoff(backend, *result)
    check_parties(backend)
    check_matchmaking(backend)
    check_guilds(backend)
    check_sessions(backend)


if __name__ == "__main__":
    main()
    print("\n" + ("All backend checks passed." if not failures else f"{len(failures)} check(s) failed: {', '.join(failures)}"))
    sys.exit(1 if failures else 0)
