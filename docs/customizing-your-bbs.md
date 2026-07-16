# Customizing Your BBS

This is the practical branding and content checklist for turning a stock
Mutineer installation into your own BBS. Edit source files in the directories
below, then rebuild or copy the changed content into the installed BBS. Do not
edit `dist/` as the source of truth; release builds regenerate it.

## Start here

1. Edit `conf/mutineer.conf` for the BBS identity, paths, listeners, limits,
   console service, plugins, and runtime policy.
2. Replace the paired `.ans` and `.asc` screens in `art/` and `menus/`.
3. Edit the `.mnu` files in `menus/` to choose commands, labels, hotkeys, and
   access requirements.
4. Replace `art/welcome.txt`, bulletins, and any prompt art.
5. Run `mutineer-validate -c conf/mutineer.conf` and test with both ANSI and
   plain-text clients.

## Configuration

### `conf/mutineer.conf`

The main live configuration file. The most important customization keys are:

| Key | What to customize |
|---|---|
| `bbs_name` | Public BBS name shown by MCI and the sysop console |
| `sysop_name` | Public sysop name |
| `motd` | Path to the message-of-the-day screen |
| `menu_main` | First menu shown after login |
| `bind`, `port` | Telnet listener address and port |
| `db_path` | Live SQLite database |
| `data_path`, `logs_path`, `art_path` | Installation content locations |
| `session_time_limit_min`, `idle_timeout_sec` | Session policy |
| `console_*`, `wfc_*` | Sysop console listener and refresh behavior |
| `plugins_*` | Plugin directory, allowlist, and denylist |
| `doors_path`, `dropfile_path`, `dosbox_path` | Door installation and runtime paths |
| `welcome_letter_*` | First-login letter, sender, and enable switch |
| `guest_*` | Guest account policy |
| `login_*`, `password_*` | Authentication throttling and password policy |

See `docs/configuration.md` for the complete key reference.

### Defaults for newly initialized installations

If you distribute your own branded build, also change the defaults embedded in:

- `src/tools/mutineer-initbbs.c` — generated configuration, starter MOTD,
  starter welcome letter, and initial system identity.
- `sql/schema.sql` — database defaults such as `system_info.bbs_name` and
  `system_info.sysop_name` for newly created databases.

Changing these files does not rewrite an existing database. Use the sysop
editors or an intentional database migration for existing installations.

## Login and system art

Keep `.ans` and `.asc` variants aligned. ANSI-capable callers receive the
colored screen; plain terminals need the ASCII fallback.

| Files | Screen or purpose |
|---|---|
| `art/mutineer.ans`, `art/mutineer.asc` | Primary BBS logo or title art |
| `art/welcome.ans`, `art/welcome.asc` | Welcome screen |
| `art/motd.ans` | Message of the day |
| `art/logon.ans` | Successful-logon screen |
| `art/newuser.ans`, `art/newuser.asc` | New-user registration |
| `art/goodbye.ans`, `art/goodbye.asc` | Logoff screen |
| `art/help.ans`, `art/help.asc` | General help |
| `art/sysop.ans`, `art/sysop.asc` | Sysop screen |
| `art/wfc.ans`, `art/wfc.asc` | Legacy WFC artwork |
| `art/multilog.ans` | Duplicate-login warning |
| `art/PWCHANGE.ans`, `art/PWCHANGE.asc` | Expired-password change screen |
| `art/NOACCESS.ans`, `art/NOACCESS.asc` | Access denied |
| `art/NOCREDTS.ans`, `art/NOCREDTS.asc` | Insufficient credits |
| `art/NOTLEFTA.ans`, `art/NOTLEFTA.asc` | No time/calls remaining |
| `art/2MANYCAL.ans`, `art/2MANYCAL.asc` | Calls-per-day limit |
| `art/prompts/handle.ans` | Handle prompt art |
| `art/prompts/password.ans` | Password prompt art |

### Letters and bulletins

- `art/welcome.txt` — first-login welcome letter body.
- `art/bulletins/bull1.ans` through `bull3.ans` — ANSI bulletins.
- `art/bulletins/bull1.asc` through `bull3.asc` — ASCII bulletin fallbacks.

Add more bulletins using the same paired naming convention and expose them
through your bulletin configuration or menu flow.

## Menus

Each menu normally has three related files:

- `.mnu` — command definitions.
- `.ans` — colored ANSI frame/template.
- `.asc` — plain-text frame/template.

Customize these sets:

| Menu set | Purpose |
|---|---|
| `menus/logon.*` | Pre-authentication welcome, signup, and login |
| `menus/main.*` | Main caller menu |
| `menus/message.*` | Message and mail commands |
| `menus/file.*` | File area commands |
| `menus/chat.*` | Chat, wall, and page commands |
| `menus/door.*` | Doors and games |
| `menus/vote.*` | Voting booth |
| `menus/sysop.*` | Sysop-only tools |

`.mnu` entries use pipe-separated fields:

```text
key|label|action|data|acs|command-flags|password
```

Short entries can omit unused trailing fields. Keep hotkeys unique within a
menu, use ACS expressions for restricted commands, and validate every edit
with `mutineer-validate`.

Menu frame templates can use placeholders such as `%%USERNAME%%`, `%%NODE%%`,
`%%TIME_LEFT%%`, `%%MENU_TITLE%%`, `%%MENU_ITEMS%%`, and `%%PROMPT%%`. The last
two are essential to stock rendered menus.

## Content managed inside the BBS

These are normally customized through sysop editors rather than static files:

- Message conferences and message areas, including names, descriptions, ACS,
  origins, and scan behavior.
- File areas, descriptions, paths, ACS, upload policy, and file listings.
- Security and validation levels.
- Voting questions and answers.
- Scheduled events.
- Transfer protocols.
- Subscription types.
- User records, sysop notes, expiration, and access flags.

The records live in the SQLite database selected by `db_path`. Back it up before
bulk edits.

## Doors and plugins

- `doors/<door>/` — door executable/assets and its JSON manifest. Customize the
  display name, paths, drop-file format, command, and bundled door artwork.
- `plugins/hello/hello.c` and `plugins/chat/chat.c` — sample plugin identity,
  labels, author metadata, and behavior. Fork these into a new plugin rather
  than silently rebranding their IDs if compatibility matters.
- `menus/main.mnu` and `menus/door.mnu` — expose installed doors and plugins to
  callers with the appropriate ACS.

Check door manifests for machine-specific absolute paths before packaging. The
sample `doors/testdoor/testdoor.json` is development content, not a portable
production configuration.

## Sysop console branding

The notcurses dashboard takes the live BBS name from `bbs_name`. Its layout,
palette, labels, metric cards, node glyphs, and command footer are implemented
in `src/tools/mutineer-console.c`. Change that file only when you want a
branded console application rather than ordinary site configuration.

## Source-level product naming

Search before publishing a fully rebranded distribution:

```bash
rg -n 'Mutineer|mutineer|Sysop' art menus conf doors plugins src sql docs
```

Not every match should be changed. Executable names, protocol identifiers,
database filenames, plugin IDs, API symbols, and compatibility strings are
product internals; change those only if you intend to maintain a fork.

## Validation checklist

```bash
cmake --build build-make --target mutineer mutineer-console mutineer-validate
build-make/mutineer-validate -c conf/mutineer.conf
ctest --test-dir build-make --output-on-failure -R 'menu|docs_consistency'
```

Then call the BBS once with an ANSI client and once with a plain terminal,
complete a new-user signup, navigate every menu, and verify the welcome letter,
MOTD, bulletins, password screens, and goodbye screen.
