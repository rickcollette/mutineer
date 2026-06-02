# MCI (Mutineer Command Interpreter)

Tokens begin with `%` and are two characters long (except conditionals). They are expanded in menus, prompts, ANSI art/MOTD, bulletins, and emails.

## Supported tokens
- `%NL` — empty output (spacing).
- `%PE` — pause prompt `(press ENTER)`.
- `%UN` — current user handle.
- `%NN` — node number.
- `%DA` — current date `YYYY-MM-DD`.
- `%TM` — current time `HH:MM`.
- `%TI` / `%TL` — time left (minutes).
- `%CR` — credits.
- `%FP` — file points.
- `%SL` — security level.
- `%PO` — current user's post count.
- `%MT` — total messages in system.
- `%FT` — total files in system.
- `%MA` — messages in current area (fallback to total).
- `%FA` — files in current area (fallback to total).
- `%NO` — nodes online.
- `%AR` — AR flag list for current user.
- Conditional: `%?ACS{then|else}` — evaluates the ACS expression; expands `then` if true else `else` (either part may be empty).

Unsupported tokens are emitted verbatim.
