# bbslib Static SDK

`bbslib.a` is Mutineer's standalone C SDK. It lets tools, local services, and sibling projects inspect or interact with a BBS installation without embedding the telnet daemon entrypoint.

The SDK is source-stable first. It is a C11 static archive with public headers under `include/bbslib/` and a convenience umbrella header:

```c
#include "bbslib.h"
```

## Build and Link

```bash
cmake --build build --target bbslib
cmake --install build --prefix /opt/mutineer
```

CMake exports the target as `Mutineer::bbslib` from `lib/cmake/bbslib/bbslibTargets.cmake`. In-tree consumers link the `bbslib` target directly.

## Lifecycle

```c
#include "bbslib.h"

BbsLibContext *ctx = NULL;
if (bbslib_open_path("conf/mutineer.conf", &ctx) != BBSLIB_OK) {
  return 1;
}

BbsLibMetrics metrics;
if (bbslib_metrics_get(ctx, &metrics) == BBSLIB_OK) {
  printf("%s has %d users\n", metrics.bbs_name, metrics.users);
}

bbslib_close(ctx);
```

`BbsLibContext` owns the loaded `BbsConfig`, an open `BbsDb`, and the last error string. Advanced integrations can still reach existing low-level APIs through `bbslib_config()` and `bbslib_db()`.

## Public Modules

| Module | Header | Purpose |
|--------|--------|---------|
| Lifecycle | `bbslib/lifecycle.h` | Context open/close, versions, result codes |
| Metrics | `bbslib/metrics.h` | Counts, daily totals, status JSON |
| Leaderboards | `bbslib/leaderboard.h` | Door opt-in configuration, score submission, and ranked results |
| Users | `bbslib/users.h` | User lookup, update, password authentication |
| Messages | `bbslib/messages.h` | Message area list, message list/post |
| Files | `bbslib/files.h` | File area list, file list/lookups |
| Bridge | `bbslib/bridge.h` | Callback-backed pseudo-session actions and doors |
| Admin | `bbslib/admin.h` | Node listing/locking and maintenance helpers |
| PLANK | `bbslib/plank.h` | PLANK identity and count status |

## Bridge Sessions

Bridge sessions adapt the existing BBS session/action machinery to non-telnet callers. A caller supplies write and readline callbacks. `bbslib` builds a synthetic `Session` around those callbacks.

```c
BbsLibSessionAdapter adapter = {
  .write = write_cb,
  .readline = readline_cb,
  .user_data = app_state,
};
BbsLibSessionOptions opts = {
  .handle = "sysop",
  .ip = "web-bridge",
};

BbsLibSession *session = NULL;
if (bbslib_session_open(ctx, &opts, &adapter, &session) == BBSLIB_OK) {
  bbslib_session_run_action(session, "who");
  bbslib_session_launch_door(session, 1);
  bbslib_session_close(session);
}
```

The default action allowlist covers bridge-safe user actions such as `who`, `messages`, `files`, `doors`, bulletins, voting, short messages, subscriptions, conferences, and help. Sysop/admin actions are denied unless `BBSLIB_SESSION_ALLOW_ADMIN_ACTIONS` is set.

## REST Reference Service

`mutineer-rest` is a small reference service linked only through `bbslib.a`.

```bash
MUTINEER_REST_TOKEN=change-me \
  ./build/mutineer-rest -c conf/mutineer.conf -b 127.0.0.1 -p 8788
```

Read-only endpoints:

| Endpoint | Description |
|----------|-------------|
| `GET /health` | Service health |
| `GET /api/status` | Status JSON |
| `GET /api/metrics` | Alias for status JSON |
| `GET /api/web/status` | Public-safe stats, version, online handles, and active door sessions |
| `GET /api/web/leaderboards` | Public-safe per-game `top` 10, `recent` 10, and score-ranked `current` players |
| `GET /api/nodes` | Node table snapshot |
| `GET /api/messages/areas` | Message area list |
| `GET /api/files/areas` | File area list |
| `GET /api/plank/status` | PLANK peer/link/area counts |

Bridge endpoints require `Authorization: Bearer $MUTINEER_REST_TOKEN` when the token is set:

| Endpoint | Description |
|----------|-------------|
| `POST /api/bridge/session?handle=<handle>` | Create an in-memory bridge session |
| `POST /api/bridge/session/<id>/input` | Queue one input line |
| `POST /api/bridge/session/<id>/action?action=who` | Run an allowlisted action |
| `POST /api/bridge/session/<id>/action?door_id=1` | Launch a configured door |
| `GET /api/bridge/session/<id>/output` | Read captured output |

Bridge sessions are in memory only and expire after 15 minutes of inactivity. The service binds to `127.0.0.1` by default.

## Compatibility

`bbslib.a` is a source-stable SDK boundary for v1. It does not promise a long-term shared-library ABI yet. New standalone integrations should prefer `bbslib` handles and callbacks instead of depending directly on `Session`.
