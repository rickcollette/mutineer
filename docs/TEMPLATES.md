# Menu Template System

This document describes Mutineer's template-driven menu presentation system.

## Overview

The menu template system separates menu behavior from presentation:

- **`.mnu` files** define menu behavior: items, keys, actions, ACS restrictions, flags
- **`.ans` / `.asc` files** define presentation: visual layout, colors, borders

Templates allow hand-drawn ANSI or ASCII art to contain dynamically generated menu content.

## File Layout

Templates are stored alongside menu files in the `menus/` directory:

```
menus/
  main.mnu        # Menu definition
  main.ans        # ANSI template (for ANSI-capable sessions)
  main.asc        # ASCII template (for ASCII-only sessions)
  file.mnu
  file.ans
  file.asc
  ...
```

## Template Structure

Every template has three logical regions:

1. **Header** - All lines before `%%MENU_ITEMS%%`
2. **Body Row Template** - The single line containing `%%MENU_ITEMS%%`
3. **Footer** - All lines after `%%MENU_ITEMS%%`

### Example Template

```
+------------------------------------------------------------------------------+
| Mutineer BBS - %%MENU_TITLE%%                                                |
| Menu: %%MENU_NAME%%   User: %%USERNAME%%   Node: %%NODE%%                    |
+------------------------------------------------------------------------------+
| %%MENU_ITEMS%% |
+------------------------------------------------------------------------------+
| Time Left: %%TIME_LEFT%% min                                                 |
| %%PROMPT%%                                                                   |
+------------------------------------------------------------------------------+
```

## Placeholders

Only the placeholders listed below are valid. Any other `%%...%%` token fails validation and the menu falls back to classic rendering.

### Structural Placeholder

| Placeholder | Description |
|-------------|-------------|
| `%%MENU_ITEMS%%` | Replaced with generated menu item rows. **Required.** |

### Menu Metadata Placeholders

| Placeholder | Description |
|-------------|-------------|
| `%%MENU_TITLE%%` | Menu title from `@TITLE` directive |
| `%%MENU_NAME%%` | Menu filename (without extension) |
| `%%PROMPT%%` | Menu prompt from `@PROMPT` directive. **Required for stock templates.** |

### Session Placeholders

| Placeholder | Description | Fallback |
|-------------|-------------|----------|
| `%%USERNAME%%` | Current user's handle | Empty string |
| `%%NODE%%` | Node number | `0` |
| `%%TIME_LEFT%%` | Minutes remaining | `0` |

## Body Row Generation

The body row template line is repeated once per row of menu items. The `%%MENU_ITEMS%%` token is replaced with formatted menu cells.

### Cell Format

Each visible menu item is formatted as:

```
[KEY] Label
```

Examples:
- `[W] Who's Online`
- `[GO] Goto Section` (multi-character key)

### Column Layout

The renderer automatically chooses 1, 2, or 3 columns based on:

1. The `@COLS` directive in the `.mnu` file (default: 3)
2. Available body width (must fit safely)

Body width is calculated as:

```
80 - visible_width(left_border) - visible_width(right_border)
```

### Truncation

If a cell is too wide for its column, it is truncated with `...`:

```
[W] Who's Onlin...
```

## ANSI vs ASCII

- **ANSI sessions** load `.ans` templates (may contain ANSI escape sequences)
- **ASCII sessions** load `.asc` templates (must not contain ANSI escapes)

The renderer uses `Session.ansi` to determine which template to load.

### Width Calculation

ANSI escape sequences are ignored when calculating visible width. This ensures correct layout regardless of color codes.

## Validation Rules

### Template Validation

Templates are validated for:

| Rule | Severity |
|------|----------|
| Exactly one `%%MENU_ITEMS%%` line | Fatal |
| `%%PROMPT%%` present (stock templates) | Fatal |
| No unknown `%%...%%` placeholders | Fatal |
| No line exceeds 80 visible columns | Fatal |
| Body width >= 10 columns | Fatal |
| No ANSI escapes in `.asc` files | Fatal |

### Menu Key Validation

Menus are validated for:

| Rule | Severity |
|------|----------|
| No duplicate single-character keys | Fatal |
| No duplicate multi-character keys | Fatal |
| No collision between single and multi-char keys | Fatal |

## Fallback Behavior

If template rendering fails for any reason, the system falls back to the classic text renderer. Failures are logged with details.

Fallback triggers:
- Template file not found
- Template validation fails
- Body width too small
- File read error

## Creating Custom Templates

### Requirements

1. Include exactly one `%%MENU_ITEMS%%` line
2. Include `%%PROMPT%%` for stock templates
3. Keep all lines under 80 visible columns
4. Use only known placeholders
5. For `.asc` files, do not include ANSI escapes

### Best Practices

1. Test with both ANSI and ASCII sessions
2. Run `mutineer-validate` to check templates
3. Use the stock template as a starting point
4. Ensure borders align properly
5. Leave adequate body width for menu items

### Example: Minimal Template

```
+---[ %%MENU_TITLE%% ]---+
| %%MENU_ITEMS%% |
+------------------------+
%%PROMPT%%
```

### Example: ANSI Template

```
[1;36m+---[ [1;37m%%MENU_TITLE%%[1;36m ]---+
[1;33m|[0;37m %%MENU_ITEMS%% [1;33m|
[1;36m+------------------------+[0m
[1;33m%%PROMPT%%[0m
```

## Validation Tool

Use `mutineer-validate` to check menus and templates:

```bash
# Validate all menus and templates
./mutineer-validate menus

# Verbose output
./mutineer-validate -v menus

# Quiet mode (errors only)
./mutineer-validate -q menus
```

Exit code is non-zero if any fatal errors are found.

## Menu File Reference

### Directives

| Directive | Description |
|-----------|-------------|
| `@TITLE` | Menu title (used in `%%MENU_TITLE%%`) |
| `@PROMPT` | Custom prompt (used in `%%PROMPT%%`) |
| `@FLAGS` | Menu flags (comma-separated) |
| `@ART` | Custom template filename |
| `@COLS` | Requested column count (1-3) |
| `@FALLBACK` | Fallback menu name |

### Menu Flags

| Flag | Description |
|------|-------------|
| `clrscr` | Clear screen before display |
| `nocenter` | Don't center title |
| `notitle` | Don't show title |
| `pause` | Force pause after display |
| `autotime` | Auto-display time remaining |
| `noprompt` | Don't show prompt |
| `lightbar` | Use lightbar navigation |
| `hotkeys` | Enable hotkeys |

### Item Format

```
KEY|Label|Action|Data|ACS|CmdFlags|Password
```

Or legacy format:

```
KEY|Label|Action|ACS
```

### Command Flags

| Flag | Description |
|------|-------------|
| `hidden` | Don't display in menu |
| `unhidden` | Always display (override ACS) |
| `password` | Requires password |
| `sysoplog` | Log to sysop log |

## Troubleshooting

### Template Not Loading

1. Check file exists: `ls menus/menuname.ans`
2. Check file permissions
3. Run `mutineer-validate menus` for errors

### Layout Issues

1. Check visible width with `mutineer-validate -v`
2. Verify ANSI escapes are properly terminated
3. Ensure body width is sufficient

### Duplicate Key Errors

1. Run `mutineer-validate menus` to identify conflicts
2. Change one of the conflicting keys
3. Consider using multi-character keys for less common actions

## API Reference

### Header: `bbs_menu_template.h`

```c
// Calculate visible length ignoring ANSI escapes
size_t visible_len(const char* str);

// Check if file exists
bool file_exists(const char* path);

// Load file into allocated buffer
char* slurp_file(const char* path, size_t* out_len);

// Resolve template path for menu and session
bool resolve_menu_template_path(const Menu* m, const Session* s,
                                char* out, size_t cap);

// Collect visible menu items
size_t collect_visible_menu_items(const Menu* m, const Session* s,
                                  const MenuItem** out, size_t cap);

// Validate template content
TemplateValidation validate_menu_template(const char* content, 
                                          bool is_stock, bool is_asc);

// Validate menu keys for duplicates
MenuKeyValidation validate_menu_keys(const Menu* m);

// Render menu using template (returns 0 on failure)
size_t menu_render_template(const Menu* m, const Session* s,
                            char* buf, size_t cap);
```
