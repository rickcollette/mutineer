# BBS Directory Updater: Mutineer Core Integration

## Purpose

When explicitly enabled by the sysop, Mutineer should check in with the
Mutineer BBS directory every 15 minutes. The directory powers the Fleet page at
`mutineerbbs.com` and lists Mutineer-powered BBS installations that have
reported recently.

This work belongs in the Mutineer daemon. It must not block callers, delay the
listener, or make successful directory access a requirement for running a BBS.
Directory registration is optional and failures are non-fatal.

## Current core integration points

The relevant code in the current tree is:

| Concern | Existing integration point |
| --- | --- |
| Configuration structure | `include/bbs_config.h` (`BbsConfig`) |
| Defaults, parsing, and saving | `src/config.c` |
| Distributed configuration template | `conf/mutineer.conf` |
| Newly initialized BBS template | `src/tools/mutineer-initbbs.c` |
| Daemon startup and shutdown | `src/main.c` |
| Live caller registry | `src/session.c` and `include/bbs_session.h` |
| CMake source/dependencies | `CMakeLists.txt` |
| Make build parity | top-level `Makefile` |

The new keys already appear in `conf/mutineer.conf`, but they are not yet
members of `BbsConfig` and are not parsed or saved by `src/config.c`.
`hostname` is in the sample configuration but is also not currently part of
`BbsConfig`.

## Configuration

Use the correctly spelled path `conf/mutineer.conf`.

```ini
# Public address used in the directory and telnet banner. When the BBS is
# behind NAT, use the public DNS name or public IP address, not localhost.
hostname=bbs.example.com

# BBS Directory configuration
bbs_dir_enabled=1
bbs_dir_server=https://mutineerbbs.com
bbs_dir_show_sysop=1
bbs_dir_show_bbs_name=1
bbs_dir_show_connections=1
bbs_dir_show_hostname=1
```

Recommended defaults:

```ini
hostname=localhost
bbs_dir_enabled=0
bbs_dir_server=https://mutineerbbs.com
bbs_dir_show_sysop=1
bbs_dir_show_bbs_name=1
bbs_dir_show_connections=1
bbs_dir_show_hostname=1
```

Directory check-in must be opt-in. Existing installations must remain disabled
after upgrading until the sysop sets `bbs_dir_enabled=1`.

### Additions to `BbsConfig`

Add these fields near the BBS identity fields in `include/bbs_config.h`:

```c
char hostname[256];
int bbs_dir_enabled;
char bbs_dir_server[256];
int bbs_dir_show_sysop;
int bbs_dir_show_bbs_name;
int bbs_dir_show_connections;
int bbs_dir_show_hostname;
```

Use `cfg_bool()` for all five Boolean options. Use bounded `snprintf()` for the
two strings, following the existing parser style. Add all seven keys to:

1. `cfg_defaults()`
2. `cfg_load()`
3. `cfg_save()`
4. `conf/mutineer.conf`
5. the configuration emitted by `mutineer-initbbs`
6. configuration parser/save round-trip tests

### Server URL normalization

The updater should accept either:

```ini
bbs_dir_server=mutineerbbs.com
```

or:

```ini
bbs_dir_server=https://mutineerbbs.com
```

Normalization rules:

1. Trim surrounding whitespace.
2. If no scheme is present, prepend `https://`.
3. Remove trailing `/` characters.
4. Append `/api/bbs_directory` exactly once.
5. Reject non-HTTPS URLs by default. A build-time or explicit development-only
   override may permit HTTP for local tests.

The final production URL is:

```text
https://mutineerbbs.com/api/bbs_directory
```

## HTTP contract

Method:

```text
POST /api/bbs_directory
```

Headers:

```text
Content-Type: application/json
Accept: application/json
User-Agent: Mutineer-BBS/<version>
```

Payload:

```json
{
  "sysop": "Sysop Name",
  "bbs_name": "BBS Name",
  "connections": 5,
  "hostname": "bbs.example.com",
  "port": 2929
}
```

The missing comma after `hostname` in the original draft has been corrected
above.

The current directory service returns HTTP `201 Created` with:

```json
{"ok": true}
```

Treat any `2xx` response as success. Log other status codes at warning level,
including at most a small bounded response excerpt. Never log secrets or an
unbounded response body.

## Privacy-switch contract

The current server requires all five payload fields and uses `hostname` plus
`port` as the stable record key. Therefore, simply omitting or blanking fields
when a `bbs_dir_show_*` option is disabled is not safe:

- blank `hostname` values cannot uniquely identify installations;
- blank `bbs_name` and `sysop` values currently fail server validation;
- sending a fabricated hostname such as `hidden` would merge unrelated BBSes.

For the first core implementation, use this rule:

> Start the updater only when `bbs_dir_enabled=1` and all four
> `bbs_dir_show_*` settings are enabled. Otherwise log one configuration warning
> at startup and leave the updater stopped.

This preserves the sysop's privacy choice and avoids publishing information
they disabled.

A follow-up protocol revision should add a stable, randomly generated
installation identifier and explicit visibility flags, for example:

```json
{
  "installation_id": "persistent-random-id",
  "sysop": "Sysop Name",
  "bbs_name": "BBS Name",
  "connections": 5,
  "hostname": "bbs.example.com",
  "port": 2929,
  "visibility": {
    "sysop": true,
    "bbs_name": true,
    "connections": true,
    "hostname": true
  }
}
```

After the website API supports that contract, the core can transmit identity
separately from what the Fleet page exposes. Do not silently send disabled
fields under the current contract.

## New core module

Add:

```text
include/bbs_directory.h
src/bbs_directory.c
tests/test_bbs_directory.c
```

Suggested public interface:

```c
#pragma once

#include "bbs_config.h"

bool bbs_directory_start(const BbsConfig *cfg);
void bbs_directory_stop(void);
```

The module copies the `BbsConfig` during startup. It must not retain a pointer to
stack-owned configuration state from `main()` unless that lifetime is made
explicit.

Internal updater state should contain:

```c
typedef struct {
  BbsConfig cfg;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t wake;
  int running;
  int stop;
} BbsDirectoryUpdater;
```

Use one process-global instance, consistent with the scheduler, console, and
door-janitor services.

## Scheduler behavior

The updater owns one background thread. Do not run network I/O in the listener,
session threads, or the existing event scheduler.

Required cadence:

1. Validate configuration during `bbs_directory_start()`.
2. If disabled or invalid, return successfully without starting a thread.
3. Start the thread after the BBS database and online registry are ready.
4. Perform the first check-in immediately after the thread starts.
5. Wait 15 minutes between completed attempts.
6. Wake immediately when `bbs_directory_stop()` signals the condition variable.
7. Join the worker thread before closing the BBS database or logging system.

Use `pthread_cond_timedwait()` with `CLOCK_MONOTONIC`. Do not implement shutdown
with one uninterruptible `sleep(900)`, because that can make daemon shutdown
wait up to 15 minutes.

There should be only one in-flight request. If an attempt takes longer than the
HTTP timeout, abort it; never allow check-ins to overlap.

## Caller count

The active session registry is currently private to `src/session.c` and guarded
by `g_online_mu`. Add this accessor to `include/bbs_session.h`:

```c
int online_count(void);
```

Implementation in `src/session.c`:

```c
int online_count(void) {
  int count = 0;
  pthread_mutex_lock(&g_online_mu);
  for (int i = 0; i < MAX_ONLINE; i++) {
    if (g_online[i]) count++;
  }
  pthread_mutex_unlock(&g_online_mu);
  return count;
}
```

Count registered live sessions, not configured nodes and not persistent node
rows. Take the count immediately before building each payload.

## JSON generation

Do not build JSON by directly interpolating unescaped configuration strings.
At minimum, add a bounded JSON-string escaping helper covering:

- quotation marks;
- backslashes;
- control characters `0x00` through `0x1f`;
- newline, carriage return, tab, backspace, and form feed.

Prefer a small existing JSON library if Mutineer adopts one. Until then, keep
the encoder local, bounded, and unit-tested. A 2 KiB payload buffer is ample for
the configured limits, but every append must check remaining capacity.

Payload sources:

| JSON field | Core source |
| --- | --- |
| `sysop` | `cfg.sysop_name` |
| `bbs_name` | `cfg.bbs_name` |
| `connections` | `online_count()` |
| `hostname` | `cfg.hostname` |
| `port` | `cfg.port` |

The updater must reject `hostname=localhost`, loopback addresses, an empty BBS
name, or a port outside `1..65535` when directory updates are enabled. These
values cannot produce a useful public Fleet entry.

## HTTPS client

Use libcurl. Mutineer currently links OpenSSL for cryptographic functionality
but does not provide a general outbound HTTPS client. Building HTTP and TLS
manually would duplicate URL parsing, certificate verification, redirects,
timeouts, framing, and response handling.

Add to CMake:

```cmake
find_package(CURL REQUIRED)
target_link_libraries(mutineer PRIVATE CURL::libcurl)
```

Add `src/bbs_directory.c` to `BBSLIB_DAEMON_SOURCES`, not
`BBSLIB_CORE_SOURCES`. This keeps the website integration out of reusable
`bbs_core_obj` and prevents unrelated core tests from acquiring a libcurl link
dependency.

Keep Makefile and packaging dependency lists in parity with CMake.

Recommended curl options:

```c
CURLOPT_URL                final_url
CURLOPT_POST               1L
CURLOPT_POSTFIELDS         payload
CURLOPT_POSTFIELDSIZE      payload_length
CURLOPT_HTTPHEADER         Content-Type, Accept, User-Agent
CURLOPT_CONNECTTIMEOUT_MS  5000L
CURLOPT_TIMEOUT_MS         10000L
CURLOPT_NOSIGNAL           1L
CURLOPT_FOLLOWLOCATION     0L
CURLOPT_SSL_VERIFYPEER     1L
CURLOPT_SSL_VERIFYHOST     2L
```

Do not disable certificate or hostname verification. Do not follow redirects,
because a compromised endpoint redirect must not forward directory data to an
unexpected host.

Call `curl_global_init()` before creating the worker thread and
`curl_global_cleanup()` only after it has joined. A new easy handle per 15-minute
attempt is acceptable and keeps failure state isolated.

## Daemon lifecycle

In `src/main.c`, start the updater with the other background services, after
the online registry and configuration are available:

```c
console_service_start(&cfg, db);
scheduler_start(&cfg, db);
door_janitor_start(&cfg, db);
bbs_directory_start(&cfg);
```

Stop it immediately after `net_run_listener()` returns and before session,
database, and logging teardown:

```c
int rc = net_run_listener(&cfg, db, &g_stop);

bbs_directory_stop();
online_broadcast("\r\nSystem shutting down.\r\n");
online_shutdown_and_wait();
```

The stop function must be safe when the updater was disabled or failed to
start. Starting twice must not create two threads.

## Failure handling

Directory availability must never affect BBS availability.

- Configuration errors: log once during startup; do not start the worker.
- DNS, connection, TLS, timeout, or non-2xx errors: log one concise warning and
  try again at the next 15-minute interval.
- Allocation or JSON-encoding failures: log and skip the current attempt.
- Do not retry in a tight loop.
- Do not terminate Mutineer because a directory check-in failed.
- Log successful registration at debug or info level without repeating the
  full payload.

The website marks a BBS inactive after 24 hours and removes it after 72 hours.
A fixed 15-minute cadence gives 96 opportunities to refresh before the inactive
threshold, so aggressive retry logic is unnecessary.

## Tests

### Configuration tests

- Defaults keep the updater disabled.
- Every new key loads correctly.
- Boolean spellings accepted by `cfg_bool()` work.
- Saving and reloading preserves all directory values.
- Overlong values remain bounded and NUL-terminated.

### Payload tests

- Exact field names and numeric types match the API.
- Quotes, backslashes, newlines, and control characters are escaped.
- Caller count is collected under the online-registry lock.
- Invalid hostname and port values prevent transmission.
- Privacy switches prevent startup under the v1 contract.

### HTTP tests

Use a local test HTTP server, never the production directory.

- The request is POST to `/api/bbs_directory`.
- `Content-Type` and `Accept` are `application/json`.
- A `201` response counts as success.
- Other `2xx` responses also count as success.
- `4xx`, `5xx`, DNS failure, refusal, and timeout are non-fatal.
- TLS verification remains enabled in production configuration.
- Redirects are rejected.

### Thread/lifecycle tests

- Enabled configuration starts exactly one worker.
- Disabled configuration starts none.
- First check-in happens without a 15-minute initial delay.
- Stop wakes and joins promptly while waiting.
- Stop is safe before start and after a failed start.
- A slow request respects the 10-second total timeout.
- ThreadSanitizer reports no race between session changes and `online_count()`.

### Acceptance test

With the directory service running locally behind a test proxy:

1. Start Mutineer with directory updates enabled and a public-looking test
   hostname.
2. Confirm one POST arrives immediately.
3. Log in and confirm the next forced/test check-in reports one connection.
4. Confirm repeated posts update one SQLite row rather than creating duplicates.
5. Confirm the Fleet GET response contains the BBS.
6. Stop the directory service and confirm Mutineer continues accepting callers.
7. Stop Mutineer and confirm shutdown does not wait for the 15-minute timer.

## Definition of done

- All seven configuration fields are implemented in defaults, load, save,
  templates, and tests.
- The updater is opt-in and runs outside caller threads.
- Check-ins occur immediately and every 15 minutes thereafter.
- The posted JSON matches the documented API exactly.
- Caller counts come from the mutex-protected online registry.
- HTTPS certificate and hostname verification are mandatory.
- Failures are logged and never stop the BBS.
- Shutdown is interruptible and joins the worker cleanly.
- CMake, Makefile, packaging, and dependency documentation agree.
- Unit, integration, shutdown, and TSAN tests pass.
