<!-- generated-by: gsd-doc-writer -->

# ACS and MCI Reference

Complete reference for Access Control Strings (ACS) and Menu Code Interface (MCI) tokens. Implemented in `src/acs.c` and `src/mci.c`.

## ACS Expression Syntax

### Operators (precedence high to low)

| Operator | Meaning |
|----------|---------|
| `!` | NOT (prefix) |
| `&` | AND |
| `\|` | OR |
| `( )` | Grouping |

Expressions are case-insensitive. Whitespace is ignored.

### Standard Terms

| Term | Description | Example |
|------|-------------|---------|
| `S#` | Security level >= # | `S50` |
| `D#` | Download security level >= # | `D20` |
| `F?` | AR flag ? set (A–Z) | `FA`, `FZ` |
| `R?` | AC restriction flag ? set | `RP`, `RM` |
| `A#` | Age >= # (from birth_date) | `A18` |
| `B#` | Baud >= # × 100 (always true on telnet) | `B96` |
| `C#` | Total logons >= # | `C10` |
| `C?` | In conference ? (A–Z, A=0) | `CA` |
| `G?` | Gender is ? (M/F) | `GM` |
| `H#` | Current hour = # (0–23) | `H20` |
| `N#` | Node number = # | `N1` |
| `P#` | Credits >= # | `P1000` |
| `T#` | Time remaining >= # (minutes) | `T30` |
| `U#` | User number = # | `U42` |
| `V` | User validated (not expired) | `V` |
| `W#` | Day of week = # (0=Sun, 6=Sat) | `W6` |
| `X#` | Days until expiration <= # | `X30` |
| `Z` | Meets post ratio | `Z` |
| `PC` | Post/call ratio met | `PC` |
| `DR` | Download ratio met | `DR` |
| `J#` | Member of conference # (DB check) | `J3` |
| `E#` | Active subscription type # (0=any) | `E1` |

### Legacy Terms

| Term | Description |
|------|-------------|
| `SLnn` | Security level >= nn |
| `Lnn` | Security level >= nn (shorthand) |
| `+X` | AR flag X set |
| `ARABC` | All listed AR flags set |
| `C>=N` / `C<=N` / `C=N` | Credits comparison |
| `T>=N` / `T<=N` / `T=N` | Time remaining comparison |
| `R>=N` / `R<=N` / `R=N` | Download ratio comparison |

### ACS Examples

```
L10                    Level 10+
+A                     Sysop (AR flag A)
S50&FA                 Level 50 with flag F
L20|+B                   Level 20 OR flag B
!(RP)                  NOT restricted from posting
S30&DR&V               Level 30, ratio OK, validated
CA&J2                  In conference A and member of #2
```

## AC Restriction Flags (R? terms)

From `include/bbs_flags.h`:

| ACS | Flag | Constant | Restriction |
|-----|------|----------|-------------|
| `RL` | L | `AC_RLOGON` | Cannot log on |
| `RC` | C | `AC_RCHAT` | Cannot chat |
| `RV` | V | `AC_RVALIDATE` | Cannot validate |
| `RU` | U | `AC_RUSERLIST` | Cannot view user list |
| `RA` | A | `AC_RAMSG` | Cannot see auto-message |
| `R*` | * | `AC_RPOSTAN` | Cannot post anonymously |
| `RP` | P | `AC_RPOST` | Cannot post |
| `RE` | E | `AC_REMAIL` | Cannot send email |
| `RK` | K | `AC_RVOTING` | Cannot vote |
| `RM` | M | `AC_RMSG` | Cannot access messages |

## AR Flags (F? and +X terms)

AR flags A–Z stored as bitset in `users.flags`:

| ACS | Bit | Typical Use |
|-----|-----|-------------|
| `FA` / `+A` | A | Sysop |
| `FB` / `+B` | B | Co-sysop |
| `FC`–`FZ` | C–Z | Custom roles |

## MCI Color Tokens

User color scheme maps `^0`–`^9` to ANSI via scheme table (8 schemes, 0=Mutineer Green default):

| Token | Role |
|-------|------|
| `^0` | Primary accent |
| `^1` | Secondary |
| `^2` | Text |
| `^3` | Dim text |
| `^4` | Highlight |
| `^5` | Error/alert |
| `^6` | Success |
| `^7` | Label |
| `^8` | Border |
| `^9` | Title |

Schemes: Mutineer Green, Classic Blue, Amber, Cyan, Red Sunset, Magenta, Monochrome, Bright White.

## MCI Attribute Tokens

| Token | Effect |
|-------|--------|
| `~L#` | Foreground color (0–15) |
| `~B#` | Background color (0–7) |
| `~K0` | Blink off |
| `~K1` | Blink on |
| `~RS` | Reset all attributes |

## MCI User Tokens (~XX)

| Token | Value |
|-------|-------|
| `~UN` | User handle |
| `~RN` | Real name |
| `~U#` | User ID |
| `~AG` | Age |
| `~BD` | Birth date |
| `~SX` | Sex (M/F/U) |
| `~CT` | City/state |
| `~ST` | Street |
| `~ZP` | Zip code |
| `~PH` | Phone |
| `~FO` | First on date |
| `~LO` | Last on date |
| `~TT` | Total time on (minutes) |
| `~TL` | Time left (minutes) |
| `~TB` | Time bank balance |
| `~SL` | Security level |
| `~DL` | Downloads count |
| `~UL` | Uploads count |
| `~DK` | Download KB |
| `~UK` | Upload KB |
| `~MP` | Messages posted |
| `~ES` | Emails sent |
| `~FB` | Feedback count |
| `~CR` | Credits |
| `~FP` | File points |
| `~LG` | Total logons |
| `~NN` | Node number |
| `~AN` | Current area name |
| `~AR` | AR flags list |

## MCI System Tokens (~XX)

| Token | Value |
|-------|-------|
| `~BN` | BBS name |
| `~SN` | Sysop name |
| `~BP` | BBS phone (N/A) |
| `~TC` | Total calls |
| `~NU` | Number of users |
| `~NF` | Number of files |
| `~NM` | Number of messages |
| `~NO` | Users online |
| `~VR` | Version string |
| `~DA` | Current date (YYYY-MM-DD) |
| `~TM` | Current time (HH:MM) |

## Legacy Percent Tokens (%XX)

| Token | Value |
|-------|-------|
| `%NL` | Newline (consumed, no output) |
| `%PE` | Pause prompt |
| `%UN` | User handle |
| `%TI` / `%TL` | Time remaining |
| `%NN` | Node number |
| `%DA` / `%TM` | Date/time |
| `%PO` | Posts count |
| `%MT` | Total messages |
| `%FT` | Total files |
| `%CR` | Credits |
| `%FP` | File points |
| `%SL` | Security level |
| `%MA` | Messages in area |
| `%FA` | Files in area |
| `%NO` | Users online |
| `%AR` | AR flags list |
| `%?expr{text\|else}` | ACS conditional text |

### Conditional Example

```
%?S50{Sysop Area|User Area}
```

Expands to "Sysop Area" if level >= 50, else "User Area".

## Template Usage Example

`menus/main.ans`:

```
^9~BN^7 — Main Menu
^2Logged in as ^0~UN ^2on node ^0~NN
^3Time left: ^0~TL ^3minutes
%?+A{^5[SYSOP]^2|}
```

## Validation

Test MCI/ACS in templates:

```bash
build/mutineer-validate menus/main.mnu
build/test_mci
build/test_acs
```

## Related Documentation

- [Menus and UI](../menus-and-ui.md)
- [Configuration](../configuration.md)
- Source: `src/acs.c`, `src/mci.c`, `include/bbs_flags.h`
