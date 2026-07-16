#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-make}"
root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$build_dir" = /* ]]; then
  bin_prefix="$build_dir"
else
  bin_prefix="$root/$build_dir"
fi

python3 - "$root" "$bin_prefix" <<'PY'
import json
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

root, bin_prefix = sys.argv[1], sys.argv[2]
tmpdir = tempfile.mkdtemp(prefix="mutineer-console-protocol.")
proc = None

def free_port(exclude=None):
    exclude = set(exclude or [])
    s = socket.socket()
    try:
        while True:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
            if port not in exclude:
                return port
            s.close()
            s = socket.socket()
    finally:
        s.close()

telnet_port = free_port()
console_port = free_port({telnet_port})

def cleanup():
    global proc
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
    shutil.rmtree(tmpdir, ignore_errors=True)

def wait_port(port, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.25):
                return
        except OSError:
            time.sleep(0.1)
    raise AssertionError(f"port {port} did not open")

def start_daemon():
    global proc
    proc = subprocess.Popen(
        [os.path.join(bin_prefix, "mutineer"), "-c", "conf/mutineer.conf"],
        cwd=tmpdir,
        stdout=open(os.path.join(tmpdir, "daemon.stdout"), "ab"),
        stderr=subprocess.STDOUT,
    )
    wait_port(console_port)

def stop_daemon():
    global proc
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
    proc = None

def recv_line(sock, timeout=3):
    sock.settimeout(timeout)
    data = bytearray()
    while True:
        b = sock.recv(1)
        if not b:
            raise EOFError("connection closed")
        if b == b"\n":
            break
        if b != b"\r":
            data.extend(b)
    return data.decode("utf-8", "replace")

def send_obj(sock, obj):
    sock.sendall((json.dumps(obj, separators=(",", ":")) + "\n").encode())

def expect_response(sock, id_, ok=None, error=None, timeout=8):
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = recv_line(sock, max(0.1, deadline - time.time()))
        msg = json.loads(line)
        if msg.get("id") != id_:
            continue
        if ok is not None and msg.get("ok") is not ok:
            raise AssertionError(f"{id_}: expected ok={ok}, got {msg}")
        if error is not None and msg.get("error") != error:
            raise AssertionError(f"{id_}: expected error={error}, got {msg}")
        return msg
    raise AssertionError(f"no response for id {id_}")

def login(user="sysop", password="mutineer"):
    s = socket.create_connection(("127.0.0.1", console_port), timeout=3)
    send_obj(s, {"id": "hello", "cmd": "hello"})
    expect_response(s, "hello", ok=True)
    send_obj(s, {"id": "login", "cmd": "login", "user": user, "password": password})
    expect_response(s, "login", ok=True)
    saw_snapshot = False
    deadline = time.time() + 8
    while time.time() < deadline:
        line = recv_line(s, max(0.1, deadline - time.time()))
        msg = json.loads(line)
        if msg.get("event") == "snapshot":
            saw_snapshot = True
            break
    assert saw_snapshot, "login did not produce snapshot"
    return s

def request(sock, id_, cmd, **payload):
    msg = {"id": id_, "cmd": cmd}
    msg.update(payload)
    send_obj(sock, msg)
    return expect_response(sock, id_, timeout=10)

def read_until(sock, needle, timeout=5):
    sock.settimeout(0.2)
    deadline = time.time() + timeout
    buf = bytearray()
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf.extend(chunk)
        if needle.encode() in buf:
            return bytes(buf)
    raise AssertionError(f"did not see {needle!r}; buffer={bytes(buf)!r}")

try:
    for name in ("art", "menus", "sql", "conf"):
        shutil.copytree(os.path.join(root, name), os.path.join(tmpdir, name))

    subprocess.check_call(
        [os.path.join(bin_prefix, "mutineer-initbbs"), "-c", "conf/mutineer.conf", "-y"],
        cwd=tmpdir,
        stdout=open(os.path.join(tmpdir, "init.log"), "wb"),
        stderr=subprocess.STDOUT,
    )

    conf = os.path.join(tmpdir, "conf", "mutineer.conf")
    text = open(conf, encoding="utf-8").read()
    replacements = {
        "port=": f"port={telnet_port}\n",
        "console_port=": f"console_port={console_port}\n",
        "console_enabled=": "console_enabled=1\n",
        "login_max_attempts=": "login_max_attempts=5\n",
        "login_window_sec=": "login_window_sec=120\n",
    }
    out = []
    seen = set()
    for line in text.splitlines(True):
        for prefix, value in replacements.items():
            if line.startswith(prefix):
                out.append(value)
                seen.add(prefix)
                break
        else:
            out.append(line)
    for prefix, value in replacements.items():
        if prefix not in seen:
            out.append(value)
    open(conf, "w", encoding="utf-8").write("".join(out))

    db_path = os.path.join(tmpdir, "data", "mutineer.db")
    db = sqlite3.connect(db_path)
    pw_hash = db.execute("SELECT pw_hash FROM users WHERE handle='sysop'").fetchone()[0]
    db.execute(
        "INSERT INTO users (handle, pw_hash, security_level_id, email, city_state, flags, first_on, logged_on) "
        "VALUES (?, ?, 1, ?, ?, 0, datetime('now'), 1)",
        ("regular", pw_hash, "regular@example.invalid", "Nowhere"),
    )
    db.commit()
    db.close()

    start_daemon()

    s = login()
    configured_nodes = []
    for cmd in ("stats.get", "nodes.list", "node.inspect", "callers.list", "history.list", "logs.tail", "system.status"):
        payload = {"node": 1} if cmd == "node.inspect" else {}
        resp = request(s, cmd, cmd, **payload)
        assert resp["ok"] is True, (cmd, resp)
        if cmd == "nodes.list":
            configured_nodes = [int(n["node"]) for n in resp.get("nodes", []) if "node" in n]

    assert request(s, "unknown", "does.not.exist")["error"] == "unknown command"
    assert request(s, "badnode", "node.lock", node=9999)["error"] == "invalid node"
    assert request(s, "kickoffline", "node.kick", node=1)["ok"] is True
    assert request(s, "broadcast", "broadcast.send", message="protocol smoke")["ok"] is True
    assert request(s, "shell", "shell.run")["error"] == "shell disabled"
    assert request(s, "lock7", "node.lock", node=7)["ok"] is True
    assert request(s, "unlock7", "node.unlock", node=7)["ok"] is True
    assert request(s, "lockpersist", "node.lock", node=7)["ok"] is True

    send_obj(s, {"id": "passthrough", "cmd": "menu.session.start", "action": "who"})
    expect_response(s, "passthrough", ok=True)
    data = read_until(s, "(press ENTER)")
    assert b"passthrough.begin" in data and b"Online users" in data, data
    s.sendall(b"\r")
    data = read_until(s, "passthrough.end")
    assert b"passthrough.end" in data, data
    s.close()

    stop_daemon()
    start_daemon()
    s = login()
    resp = request(s, "inspect7", "node.inspect", node=7)
    assert resp["ok"] is True and resp.get("node", {}).get("locked") is True, resp

    assert request(s, "lock1_login", "node.lock", node=1)["ok"] is True
    t = socket.create_connection(("127.0.0.1", telnet_port), timeout=3)
    t.settimeout(0.5)
    chunks = []
    deadline = time.time() + 4
    while time.time() < deadline:
        try:
            chunk = t.recv(512)
        except socket.timeout:
            continue
        if not chunk:
            break
        chunks.append(chunk)
        if b"ANSI graphics" in b"".join(chunks):
            break
    banner = b"".join(chunks).decode("utf-8", "replace")
    assert "ANSI graphics" in banner, banner
    nodes_after_telnet = request(s, "nodes_after_telnet", "nodes.list").get("nodes", [])
    assigned = [int(n["node"]) for n in nodes_after_telnet
                if n.get("status") == "logging_in" and int(n.get("node", 0)) != 1]
    assert assigned, nodes_after_telnet
    t.close()

    request(s, "unlock1", "node.unlock", node=1)
    shutdown_resp = request(s, "shutdown", "system.shutdown")
    assert shutdown_resp["ok"] is False, shutdown_resp
    s.close()
    stop_daemon()
    start_daemon()

    bad = socket.create_connection(("127.0.0.1", console_port), timeout=3)
    send_obj(bad, {"id": "bad", "cmd": "login", "user": "sysop", "password": "wrong"})
    assert expect_response(bad, "bad", ok=False)["error"] == "authentication failed"
    bad.close()

    non = socket.create_connection(("127.0.0.1", console_port), timeout=3)
    send_obj(non, {"id": "regular", "cmd": "login", "user": "regular", "password": "mutineer"})
    assert expect_response(non, "regular", ok=False)["error"] == "authentication failed"
    non.close()

    for i in range(6):
        c = socket.create_connection(("127.0.0.1", console_port), timeout=3)
        send_obj(c, {"id": f"throttle{i}", "cmd": "login", "user": f"nosuch{i}", "password": "wrong"})
        expect_response(c, f"throttle{i}", ok=False)
        c.close()
    log = open(os.path.join(tmpdir, "logs", "mutineer.log"), encoding="utf-8", errors="replace").read()
    assert "console_login_failed" in log, log
    assert "console_login_non_sysop" in log, log
    assert "console_login_throttled" in log, log

    malformed = socket.create_connection(("127.0.0.1", console_port), timeout=3)
    malformed.sendall(b"not json\n")
    msg = json.loads(recv_line(malformed))
    assert msg["ok"] is False and msg["error"] == "malformed_json", msg
    malformed.close()
except Exception:
    print(f"console protocol tempdir: {tmpdir}", file=sys.stderr)
    log_path = os.path.join(tmpdir, "logs", "mutineer.log")
    if os.path.exists(log_path):
        print("--- mutineer.log ---", file=sys.stderr)
        print(open(log_path, encoding="utf-8", errors="replace").read()[-12000:], file=sys.stderr)
    out_path = os.path.join(tmpdir, "daemon.stdout")
    if os.path.exists(out_path):
        print("--- daemon.stdout ---", file=sys.stderr)
        print(open(out_path, encoding="utf-8", errors="replace").read()[-12000:], file=sys.stderr)
    raise
finally:
    cleanup()
PY
