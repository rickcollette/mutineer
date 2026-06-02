# ACS / MCI Reference (Mutineer)

## ACS
- Operators: `!` (not), `&` (and), `|` (or), parentheses.
- Terms:
  - `SLnn` / `Lnn` — security level >= nn
  - `+X` — AR flag X set (A–Z)
  - `ARABC` — all listed flags present
  - `C>=N` — credits comparison
  - `R>=N` — download ratio comparison (uses user ratio fields)
  - `T>=N` — time remaining minutes comparison
- Examples: `SL30&+A`, `!(L10|+B)`, `ARAB&C>500`.

## MCI Tokens
- `%UN` user handle
- `%NN` node number
- `%DA` date `YYYY-MM-DD`
- `%TM` time `HH:MM`
- `%TL` time left (minutes)
- `%TI` alias for time left
- `%CR` credits
- `%FP` file points
- `%SL` security level
- `%PO` user posts
- `%MT` total messages
- `%FT` total files
- `%MA` messages in current area
- `%FA` files in current area
- `%NO` nodes online
- `%AR` AR flags list
- `%PE` pause `(press ENTER)`
- `%NL` empty
- Conditional: `%?ACS{then|else}` expands `then` if ACS is true else `else`.
