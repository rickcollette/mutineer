# Door Leaderboard Standard

Mutineer provides one canonical, opt-in leaderboard pipeline for native, DOS,
and Buccaneer doors. The BBS owns player identity, score storage, ranking, and
public presentation. A door only declares its leaderboard and submits a result.

## Door configuration

Leaderboard support is disabled unless the door record explicitly opts in.

| Contract name | Door column | Default | Meaning |
|---|---|---:|---|
| `LB_ENABLE` | `lb_enable` | `0` | `1` publishes and accepts scores; `0` does neither |
| `LB_GAME_KEY` | `lb_key` | `door-<id>` | Stable machine-readable game identifier |
| `LB_SCORE_LABEL` | `lb_label` | `Score` | Human label such as `Gold`, `Turns`, or `Wins` |
| `LB_ORDER` | `lb_order` | `desc` | `desc` means higher is better; `asc` means lower is better |

Example operator configuration:

```sql
UPDATE doors
SET lb_enable = 1,
    lb_key = 'vulgar-dungeon',
    lb_label = 'Gold',
    lb_order = 'desc'
WHERE name = 'Vulgar Dungeon';
```

`LB_GAME_KEY` must remain stable across releases. Disabling a leaderboard stops
score submission and removes that game from public results without deleting its
stored history. Mutineer keeps each authenticated player's best result for each
door according to `LB_ORDER`.

## Native and DOS doors

Mutineer writes a `leaderboard` object into `MUTINEER_SESSION.JSON`:

```json
{
  "leaderboard": {
    "LB_ENABLE": 1,
    "LB_GAME_KEY": "vulgar-dungeon",
    "LB_SCORE_LABEL": "Gold",
    "LB_ORDER": "desc",
    "LB_RESULT_FILE": "MUTINEER_LB_RESULT.JSON"
  }
}
```

On a successful door exit, an enabled door may write the named result file in
its runtime directory:

```json
{"score": 12500, "detail": "Level 9"}
```

`score` is a signed 64-bit integer. `detail` is optional display context, not a
second score. Mutineer reads at most 4096 bytes and attributes the result to the
authenticated BBS session; doors cannot choose or spoof the player handle.

## Buccaneer doors

Request the `leaderboard.write` capability in the door manifest, then use:

```text
LEADERBOARD.ENABLED()          -> BOOLEAN
LEADERBOARD.SUBMIT(score)      -> BOOLEAN
LEADERBOARD.SUBMIT(score, detail) -> BOOLEAN
```

Submission returns false when the door has `LB_ENABLE=0`, lacks the capability,
or storage rejects the result. The host supplies the current door and caller
identity.

## BBSLIB SDK

Include `bbslib/leaderboard.h` (also included by `bbslib.h`). Integrations can
inspect configuration, submit an authenticated result, or list ranked scores:

```c
BbsLibLeaderboardConfig config;
bbslib_leaderboard_config(ctx, door_id, &config);
bbslib_leaderboard_submit(ctx, door_id, handle, score, detail);
int count = bbslib_leaderboard_list(ctx, scores, capacity);
```

Callers outside the BBS session layer are responsible for authenticating the
`handle` before submission. Public web code should consume the REST endpoint,
not link directly to the database.

## Public REST representation

`GET /api/web/leaderboards` needs no bridge token and returns every enabled door
with `LB_ENABLE=1`, including games that do not have a score yet. Each game has
three canonical views:

- `top`: the best 10 players according to `LB_ORDER`.
- `recent`: the last 10 players whose personal best changed, newest first.
- `current`: callers currently inside that door, ranked by their stored personal
  best. A caller without a stored score has `score: null` and sorts last.

“Current” means currently running the door. Native and DOS results are accepted
after a successful exit, so the score shown while they play is their previous
personal best. Buccaneer doors can submit during play when an immediate update is
appropriate.

```json
{
  "leaderboards": [{
    "game_key": "vulgar-dungeon",
    "game_name": "Vulgar Dungeon",
    "score_label": "Gold",
    "score_order": "desc",
    "top": [{
      "rank": 1,
      "handle": "sysop",
      "score": 12500,
      "detail": "Level 9",
      "achieved_at": "2026-07-15 12:00:00"
    }],
    "recent": [{
      "handle": "sysop",
      "score": 12500,
      "detail": "Level 9",
      "achieved_at": "2026-07-15 12:00:00"
    }],
    "current": [{
      "handle": "sysop",
      "node": 1,
      "score": 12500
    }]
  }]
}
```

The endpoint exposes leaderboard display data only. It does not expose message
boards, files, downloads, IP addresses, credentials, or score mutation.

## Rules for door authors

1. Treat `LB_ENABLE` as authoritative and do not emit results when it is `0`.
2. Submit one comparable integer whose meaning matches `LB_SCORE_LABEL`.
3. Never accept a BBS handle from gameplay input for score attribution.
4. Write the result only after a completed, valid game and exit successfully.
5. Keep `detail` short and non-sensitive; it is publicly visible.
6. Do not implement a parallel leaderboard database or public endpoint.

## Ranking and time semantics

The canonical store contains one row per game and authenticated handle. A new
submission only replaces that row when it beats the stored score according to
`LB_ORDER`; `achieved_at` is therefore the time that personal best was achieved,
not simply the player's most recent login. This makes the top and recent views
stable and comparable across every door implementation.
