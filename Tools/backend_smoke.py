#!/usr/bin/env python3
"""End-to-end check of a running local backend (Tools/run_local_backend): logs in with a device id,
verifies the session the way a dedicated server does, and walks a character save through load, write,
a stale write (version conflict), quarantine, and the refusals (client calls, client storage writes,
bad tokens). Exit code 0 when everything behaves. Reads keys from Backend/.env. docs/backend-setup.md.

Needs CROSSGUARD_DEV_AUTH=1 (device login). Creates one throwaway account per run.
"""
import base64
import json
import sys
import urllib.error
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:7350"


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


def main():
    env = read_env()
    http_key = basic(env["NAKAMA_HTTP_KEY"])

    def rpc(rpc_id, payload, auth=http_key):
        return call("POST", f"/v2/rpc/{rpc_id}?unwrap", payload, auth)

    # 1. Client login (development device id).
    device = "smoke-" + uuid.uuid4().hex
    status, session = call("POST", "/v2/account/authenticate/device?create=true", {"id": device}, basic(env["NAKAMA_SERVER_KEY"]))
    check("device login", status == 200 and "token" in session, f"{status} {session}")
    if status != 200:
        return
    token = session["token"]

    # 2. Dedicated server verifies the client's token.
    status, who = rpc("crossguard_verify_session", {"token": token})
    check("verify session", status == 200 and who.get("user_id"), f"{status} {who}")
    user_id = who["user_id"]
    status, body = rpc("crossguard_verify_session", {"token": token[:-4] + "AAAA"})
    check("tampered token refused (401)", status == 401, f"{status} {body}")
    status, body = rpc("crossguard_verify_session", {"token": token}, auth=basic("wrong-key"))
    check("wrong http key refused (401)", status == 401, f"{status} {body}")

    # 3. Saves.
    ref = {"user_id": user_id, "character_id": "main"}
    status, loaded = rpc("crossguard_save_load", ref)
    check("load new character", status == 200 and loaded == {"found": False, "version": "*", "record": None}, f"{status} {loaded}")

    record = {"version": 1, "playerId": user_id, "gold": 10, "stash": {"purchasedTabs": 0, "tabs": [{"items": []}]}}
    status, written = rpc("crossguard_save_write", dict(ref, record=record, expected_version="*"))
    check("first write", status == 200 and written.get("version"), f"{status} {written}")
    first_version = written.get("version")

    status, loaded = rpc("crossguard_save_load", ref)
    check("load after write", status == 200 and loaded["found"] and loaded["version"] == first_version and loaded["record"]["gold"] == 10,
          f"{status} {loaded}")

    record["gold"] = 20
    status, written = rpc("crossguard_save_write", dict(ref, record=record, expected_version=first_version))
    check("write with loaded version", status == 200 and written.get("version") not in (None, first_version), f"{status} {written}")

    record["gold"] = 30
    status, body = rpc("crossguard_save_write", dict(ref, record=record, expected_version=first_version))
    check("stale write refused (409)", status == 409 and "version conflict" in str(body), f"{status} {body}")
    status, loaded = rpc("crossguard_save_load", ref)
    check("stale write did not land", loaded["record"]["gold"] == 20, f"{loaded}")

    status, body = rpc("crossguard_save_write", dict(ref, record=dict(record, playerId="someone-else"), expected_version="*"))
    check("record for another player refused (400)", status == 400, f"{status} {body}")

    status, body = rpc("crossguard_save_quarantine", dict(ref, reason="smoke test"))
    check("quarantine", status == 200 and body.get("quarantined") is True, f"{status} {body}")

    # 4. Clients cannot use the server RPCs or write the collection.
    bearer = "Bearer " + token
    status, body = rpc("crossguard_save_load", ref, auth=bearer)
    check("client cannot call save RPC (403)", status == 403, f"{status} {body}")
    status, body = call("PUT", "/v2/storage", {"objects": [{"collection": "characters", "key": "main", "value": json.dumps(record)}]}, bearer)
    check("client cannot write characters (403)", status == 403, f"{status} {body}")
    status, body = call("GET", f"/v2/storage/characters?user_id={user_id}", None, bearer)
    check("client can read own save", status == 200 and len(body.get("objects", [])) == 1, f"{status} {body}")

    # 5. Large record (well over Nakama's default 256 KB request cap).
    big = dict(record, pad="x" * (2 * 1024 * 1024))
    status, loaded = rpc("crossguard_save_load", ref)
    status, written = rpc("crossguard_save_write", dict(ref, record=big, expected_version=loaded["version"]))
    check("2 MB record accepted", status == 200, f"{status} {str(written)[:200]}")


if __name__ == "__main__":
    main()
    print("\n" + ("All backend checks passed." if not failures else f"{len(failures)} check(s) failed: {', '.join(failures)}"))
    sys.exit(1 if failures else 0)
