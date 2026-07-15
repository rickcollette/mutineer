# Mutineer Console Protocol

`mutineer-console` talks to the daemon over the console-control TCP service.
The default listener is `127.0.0.1:2931`; non-loopback binds are administrative
only and must be configured deliberately.

## Framing

Normal control traffic is newline-delimited JSON. Each client request is one
JSON object followed by `\n`. Requests that expect a response include an `id`.
Responses include the same `id`, an `ok` boolean, and either command payload
fields or an `error` string.

The server may send event objects without an `id`. Events are used for the
initial `snapshot`, node changes, broadcasts, shutdown, and passthrough state.

## Login

Clients start with:

```json
{"id":"1","cmd":"hello"}
```

Then authenticate with a normal BBS username and password:

```json
{"id":"2","cmd":"login","user":"sysop","password":"mutineer"}
```

The account must pass sysop ACS `+A`. Bad passwords, missing users, non-sysop
users, and throttled attempts return `ok:false`. Console login attempts use the
same throttling policy as telnet login and write audit records for success,
failure, non-sysop denial, and throttling.

## Commands

Supported command names are:

```text
stats.get
nodes.list
node.inspect
node.kick
node.lock
node.unlock
broadcast.send
callers.list
history.list
logs.tail
system.status
system.shutdown
shell.run
menu.session.start
```

Node commands require a numeric `node` field in the supported node range.
Invalid node ids return a structured error and do not update runtime or DB
state. `broadcast.send` requires `message`. `menu.session.start` requires an
`action` name from the BBS menu action table.

## Passthrough Mode

Interactive actions use explicit control frames:

```json
{"event":"passthrough.begin"}
```

After this frame, the connection carries raw terminal bytes for the server-side
interactive action. The client forwards keyboard bytes to the daemon and writes
server bytes directly to the local terminal. No JSON command traffic is sent
while raw passthrough is active.

When the action exits, the daemon sends:

```json
{"event":"passthrough.end"}
```

The console client restores the local terminal and returns to the dashboard.
`shell.run` uses the same passthrough framing and runs the configured server-side
command under the existing process supervisor; it never launches a local shell
on the console client machine.

## Errors

Malformed JSON returns:

```json
{"id":"","ok":false,"error":"malformed_json"}
```

Unknown commands return `unknown command`; unauthenticated commands return
`not authenticated`; disabled shell execution returns `shell disabled`.
