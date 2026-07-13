#include "bbs_util.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

void str_trim(char* s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n && (s[n-1] == '\n' || s[n-1] == '\r' || isspace((unsigned char)s[n-1]))) {
    s[--n] = '\0';
  }
  size_t i = 0;
  while (s[i] && isspace((unsigned char)s[i])) i++;
  if (i) memmove(s, s + i, strlen(s + i) + 1);
}

bool str_starts_with(const char* s, const char* prefix) {
  if (!s || !prefix) return false;
  size_t a = strlen(prefix);
  return strncmp(s, prefix, a) == 0;
}

bool fd_write_all(int fd, const void* buf, size_t len) {
  const uint8_t* p = (const uint8_t*)buf;
  size_t off = 0;
  while (off < len) {
    ssize_t w = write(fd, p + off, len - off);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (w == 0) return false;
    off += (size_t)w;
  }
  return true;
}

int fd_readline(int fd, int timeout_sec, uint8_t* buf, size_t cap) {
  size_t n = 0;
  while (n + 1 < cap) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int r = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (r == 0) return -1; /* timeout */
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }

    uint8_t ch;
    ssize_t got = read(fd, &ch, 1);
    if (got <= 0) return 0; /* disconnect */

    if (ch == '\n') break;
    if (ch == '\r') break;

    if (ch == 0x08 || ch == 0x7F) { /* backspace */
      if (n > 0) n--;
      continue;
    }

    buf[n++] = ch;
  }
  buf[n] = 0;
  return (int)n;
}

char* file_read_all(const char* path, size_t* len_out) {
  if (!path) return NULL;
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0) { fclose(f); return NULL; }
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
  char* buf = (char*)malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[got] = 0;
  if (len_out) *len_out = got;
  return buf;
}

void path_join(const char* dir, const char* leaf, char* out, size_t out_cap) {
  if (!out || out_cap == 0) return;
  out[0] = 0;
  if (!dir || dir[0] == 0) {
    snprintf(out, out_cap, "%s", leaf ? leaf : "");
    return;
  }
  if (!leaf || leaf[0] == 0) {
    snprintf(out, out_cap, "%s", dir);
    return;
  }
  size_t dlen = strlen(dir);
  if (dir[dlen-1] == '/') {
    snprintf(out, out_cap, "%s%s", dir, leaf);
  } else {
    snprintf(out, out_cap, "%s/%s", dir, leaf);
  }
}

bool file_copy(const char* src, const char* dst, int* size_out) {
  int in = open(src, O_RDONLY);
  if (in < 0) return false;
  int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) { close(in); return false; }
  char buf[8192];
  ssize_t r;
  int total = 0;
  while ((r = read(in, buf, sizeof(buf))) > 0) {
    total += (int)r;
    if (!fd_write_all(out, buf, (size_t)r)) { close(in); close(out); return false; }
  }
  close(in);
  close(out);
  if (size_out) *size_out = total;
  return r == 0;
}

bool bbs_safe_identifier(const char* value, size_t max_len) {
  if (!value || !value[0]) return false;
  size_t len = strlen(value);
  if (len == 0 || len > max_len) return false;
  if (!strcmp(value, ".") || !strcmp(value, "..")) return false;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    if (c == '/' || c == '\\') return false;
    if (!(isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
  }
  return true;
}

bool bbs_safe_filename(const char* value, size_t max_len) {
  if (!bbs_safe_identifier(value, max_len)) return false;
  if (strchr(value, '/')) return false;
  if (strchr(value, '\\')) return false;
  return true;
}

bool bbs_mkdir_p(const char* path, int mode) {
  if (!path || !path[0]) return false;
  char tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len == 0) return false;
  if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

  for (char* p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(tmp, (mode_t)mode) != 0 && errno != EEXIST) return false;
    *p = '/';
  }
  return mkdir(tmp, (mode_t)mode) == 0 || errno == EEXIST;
}

bool bbs_remove_tree(const char* path) {
  struct stat st;
  if (!path || !path[0]) return false;
  if (lstat(path, &st) != 0) return errno == ENOENT;
  if (S_ISDIR(st.st_mode)) {
    DIR* d = opendir(path);
    if (!d) return false;
    bool ok = true;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
      if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
      char child[1024];
      path_join(path, ent->d_name, child, sizeof(child));
      if (!bbs_remove_tree(child)) ok = false;
    }
    closedir(d);
    return rmdir(path) == 0 && ok;
  }
  return unlink(path) == 0;
}
