#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void str_trim(char* s);
bool str_starts_with(const char* s, const char* prefix);

/* Safe write-all to socket. */
bool fd_write_all(int fd, const void* buf, size_t len);

/* Read a line (with basic backspace). Returns length, 0 on disconnect, -1 on error/timeout. */
int  fd_readline(int fd, int timeout_sec, uint8_t* buf, size_t cap);

/* Read entire file into a malloc'd buffer (nul-terminated). Returns buf or NULL. */
char* file_read_all(const char* path, size_t* len_out);

/* Join dir + '/' + leaf into out (size cap), skipping duplicate slashes. */
void path_join(const char* dir, const char* leaf, char* out, size_t out_cap);

/* Copy file; returns true on success. size_out optional. */
bool file_copy(const char* src, const char* dst, int* size_out);

/* Stable relative identifiers: non-empty [A-Za-z0-9._-], no "."/"..", no slashes. */
bool bbs_safe_identifier(const char* value, size_t max_len);

/* Basename-only safe filenames for user-visible file objects. */
bool bbs_safe_filename(const char* value, size_t max_len);

/* mkdir -p for simple directory trees. */
bool bbs_mkdir_p(const char* path, int mode);

bool bbs_remove_tree(const char* path);
