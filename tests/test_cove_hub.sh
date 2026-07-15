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
tmpdir = tempfile.mkdtemp(prefix="mutineer-cove-hub.")
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

hub_port = free_port()
mgmt_port = free_port({hub_port})
token = "smoke-token"

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

def wait_port(port, timeout=45):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.25):
                return
        except OSError:
            time.sleep(0.1)
    raise AssertionError(f"port {port} did not open")

def request(method, path, body="", auth=False):
    headers = [
        f"{method} {path} HTTP/1.1",
        "Host: 127.0.0.1",
        "Connection: close",
    ]
    if auth:
        headers.append(f"Authorization: Bearer {token}")
    if body:
        headers.append("Content-Type: application/x-www-form-urlencoded")
    body_bytes = body.encode()
    headers.append(f"Content-Length: {len(body_bytes)}")
    raw = ("\r\n".join(headers) + "\r\n\r\n").encode() + body_bytes
    sock = socket.create_connection(("127.0.0.1", mgmt_port), timeout=4)
    sock.sendall(raw)
    sock.shutdown(socket.SHUT_WR)
    chunks = []
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        chunks.append(chunk)
    sock.close()
    response = b"".join(chunks).decode("utf-8", "replace")
    status = int(response.split(" ", 2)[1])
    data = response.split("\r\n\r\n", 1)[1] if "\r\n\r\n" in response else ""
    try:
        parsed = json.loads(data)
    except json.JSONDecodeError:
        parsed = {"raw": data}
    return status, parsed

try:
    for name in ("sql", "conf", "art", "menus"):
        shutil.copytree(os.path.join(root, name), os.path.join(tmpdir, name))

    conf = os.path.join(tmpdir, "hub.conf")
    db_path = os.path.join(tmpdir, "data", "mutineer.db")
    auth_db = os.path.join(tmpdir, "data", "cove-auth.db")
    mgmt_db = os.path.join(tmpdir, "data", "cove-management.db")
    os.makedirs(os.path.dirname(db_path), exist_ok=True)

    subprocess.check_call(
        [os.path.join(bin_prefix, "mutineer-initbbs"), "-c", os.path.join(tmpdir, "conf", "mutineer.conf"), "-y"],
        cwd=tmpdir,
        stdout=open(os.path.join(tmpdir, "init.log"), "wb"),
        stderr=subprocess.STDOUT,
    )
    with open(conf, "w", encoding="utf-8") as f:
        f.write(f"""message_base_path={db_path}
base_id=smoke
auth_db_path={auth_db}
management_db_path={mgmt_db}
bind=127.0.0.1
port={hub_port}
management_bind=127.0.0.1
management_port={mgmt_port}
management_token={token}
foreground=1
""")

    proc = subprocess.Popen(
        [os.path.join(bin_prefix, "coved"), "-mode=hub", conf],
        cwd=tmpdir,
        stdout=open(os.path.join(tmpdir, "coved.log"), "ab"),
        stderr=subprocess.STDOUT,
    )
    wait_port(mgmt_port)
    wait_port(hub_port)

    assert os.path.exists(auth_db), "auth db not created"
    assert os.path.exists(mgmt_db), "management db not created"

    status, data = request("GET", "/health")
    assert status == 200 and data.get("ok") is True and data.get("mode") == "hub", (status, data)

    status, data = request("POST", "/nodes", "node_addr=a")
    assert status == 401, (status, data)

    status, data = request("POST", "/nodes", "node_addr=a", auth=True)
    assert status == 400, (status, data)

    body = "node_addr=node1&node_name=Node%201&network_name=SmokeNet&remote_host=127.0.0.1&remote_port=1234&notes=smoke"
    status, data = request("POST", "/nodes", body, auth=True)
    assert status == 200 and data.get("ok") is True, (status, data)

    status, data = request("GET", "/nodes")
    assert status == 200 and data.get("nodes"), (status, data)
    node_id = data["nodes"][0]["id"]

    for suffix in ("disable", "enable"):
        status, data = request("POST", f"/nodes/{node_id}/{suffix}", "", auth=True)
        assert status == 200 and data.get("ok") is True, (suffix, status, data)

    status, data = request("GET", "/links/health")
    assert status == 200 and "links" in data, (status, data)
    status, data = request("GET", "/queue")
    assert status == 200 and "pending" in data and "deadletters" in data, (status, data)
    status, data = request("GET", "/events")
    assert status == 200 and "events" in data, (status, data)

    hub = socket.create_connection(("127.0.0.1", hub_port), timeout=3)
    hub.sendall(b"PLANK smoke\r\n")
    hub.close()
    time.sleep(0.5)
    status, data = request("GET", "/connections")
    assert status == 200 and "connections" in data, (status, data)

    status, data = request("DELETE", f"/nodes/{node_id}", "", auth=True)
    assert status == 200 and data.get("ok") is True, (status, data)

    db = sqlite3.connect(mgmt_db)
    actions = {row[0] for row in db.execute("SELECT action FROM cove_management_events")}
    db.close()
    assert {"hub.start", "auth.failed", "node.add", "node.disable", "node.enable", "node.delete"} <= actions, actions
except Exception:
    for log_name in ("init.log", "coved.log"):
        log_path = os.path.join(tmpdir, log_name)
        if os.path.exists(log_path):
            print(f"--- {log_name} ---", file=sys.stderr)
            with open(log_path, "r", encoding="utf-8", errors="replace") as log:
                print(log.read(), file=sys.stderr)
    if proc is not None:
        print(f"--- coved exit status: {proc.poll()} ---", file=sys.stderr)
    raise
finally:
    cleanup()
PY
