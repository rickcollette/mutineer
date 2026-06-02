#pragma once

/* ANSI helpers with Mutineer’s green-forward palette */

static const char* ANSI_RESET = "\x1b[0m";
static const char* ANSI_BASE  = "\x1b[0;32m";  /* green */
static const char* ANSI_HILITE = "\x1b[1;37m"; /* bright white */
static const char* ANSI_ALERT = "\x1b[1;31m";  /* bright red */
static const char* ANSI_PROMPT = "\x1b[0;36m"; /* cyan prompt accent */

