#pragma once
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

/* Simple thread-safe logging helpers */

bool log_init(const char* path); /* NULL/"" -> stderr */
void log_close(void);

void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);
void log_audit(const char* user, const char* action, const char* detail);
void log_trap(const char* detail);
