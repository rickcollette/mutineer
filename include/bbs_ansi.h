#pragma once

/* ANSI helpers with Mutineer’s green-forward palette */

#define ANSI_RESET "\x1b[0m"
#define ANSI_BASE  "\x1b[0;32m"  /* green */
#define ANSI_HILITE "\x1b[1;37m" /* bright white */
#define ANSI_ALERT "\x1b[1;31m"  /* bright red */
#define ANSI_PROMPT "\x1b[0;36m" /* cyan prompt accent */
