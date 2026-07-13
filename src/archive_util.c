#define _POSIX_C_SOURCE 200809L
#include "bbs_archive.h"
#include "bbs_util.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct archive;
struct archive_entry;
typedef long long la_int64_t;

extern struct archive* archive_write_new(void);
extern int archive_write_set_format_zip(struct archive*);
extern int archive_write_open_filename(struct archive*, const char*);
extern int archive_write_header(struct archive*, struct archive_entry*);
extern la_int64_t archive_write_data(struct archive*, const void*, size_t);
extern int archive_write_close(struct archive*);
extern int archive_write_free(struct archive*);
extern struct archive* archive_read_new(void);
extern int archive_read_support_filter_all(struct archive*);
extern int archive_read_support_format_all(struct archive*);
extern int archive_read_open_filename(struct archive*, const char*, size_t);
extern int archive_read_next_header(struct archive*, struct archive_entry**);
extern la_int64_t archive_read_data(struct archive*, void*, size_t);
extern int archive_read_data_skip(struct archive*);
extern int archive_read_close(struct archive*);
extern int archive_read_free(struct archive*);
extern const char* archive_error_string(struct archive*);
extern int archive_errno(struct archive*);
extern struct archive_entry* archive_entry_new(void);
extern void archive_entry_free(struct archive_entry*);
extern void archive_entry_set_pathname(struct archive_entry*, const char*);
extern const char* archive_entry_pathname(struct archive_entry*);
extern void archive_entry_set_size(struct archive_entry*, la_int64_t);
extern la_int64_t archive_entry_size(struct archive_entry*);
extern void archive_entry_set_filetype(struct archive_entry*, unsigned int);
extern unsigned int archive_entry_filetype(struct archive_entry*);
extern void archive_entry_set_perm(struct archive_entry*, int);

#define ARCHIVE_OK 0
#define ARCHIVE_EOF 1
#define AE_IFREG 0100000

static void set_archive_err(struct archive* a, char* errbuf, size_t errcap, const char* fallback) {
  if (!errbuf || errcap == 0) return;
  const char* msg = a ? archive_error_string(a) : NULL;
  if (!msg || !msg[0]) msg = fallback ? fallback : "archive operation failed";
  snprintf(errbuf, errcap, "%s", msg);
}

static bool write_file_to_archive(struct archive* a, const char* dir, const char* name,
                                  char* errbuf, size_t errcap) {
  if (!bbs_safe_filename(name, 255)) return true;
  char path[1024];
  path_join(dir, name, path, sizeof(path));
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return true;

  struct archive_entry* ent = archive_entry_new();
  if (!ent) {
    snprintf(errbuf, errcap, "out of memory");
    return false;
  }
  archive_entry_set_pathname(ent, name);
  archive_entry_set_size(ent, (la_int64_t)st.st_size);
  archive_entry_set_filetype(ent, AE_IFREG);
  archive_entry_set_perm(ent, 0644);
  if (archive_write_header(a, ent) != ARCHIVE_OK) {
    set_archive_err(a, errbuf, errcap, "archive_write_header failed");
    archive_entry_free(ent);
    return false;
  }
  archive_entry_free(ent);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    snprintf(errbuf, errcap, "open %s failed: %s", path, strerror(errno));
    return false;
  }
  char buf[16384];
  ssize_t n;
  bool ok = true;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    if (archive_write_data(a, buf, (size_t)n) < 0) {
      set_archive_err(a, errbuf, errcap, "archive_write_data failed");
      ok = false;
      break;
    }
  }
  if (n < 0) {
    snprintf(errbuf, errcap, "read %s failed: %s", path, strerror(errno));
    ok = false;
  }
  close(fd);
  return ok;
}

bool bbs_archive_create_zip_from_dir(const char* dir, const char* output_path,
                                     char* errbuf, size_t errcap) {
  struct archive* a = archive_write_new();
  if (!a) {
    snprintf(errbuf, errcap, "archive_write_new failed");
    return false;
  }
  if (archive_write_set_format_zip(a) != ARCHIVE_OK ||
      archive_write_open_filename(a, output_path) != ARCHIVE_OK) {
    set_archive_err(a, errbuf, errcap, "failed to open output archive");
    archive_write_free(a);
    return false;
  }

  DIR* d = opendir(dir);
  if (!d) {
    snprintf(errbuf, errcap, "opendir %s failed: %s", dir, strerror(errno));
    archive_write_free(a);
    return false;
  }
  bool ok = true;
  struct dirent* ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    if (!write_file_to_archive(a, dir, ent->d_name, errbuf, errcap)) {
      ok = false;
      break;
    }
  }
  closedir(d);
  archive_write_close(a);
  archive_write_free(a);
  return ok;
}

static bool open_archive_for_read(const char* path, struct archive** out, char* errbuf, size_t errcap) {
  struct archive* a = archive_read_new();
  if (!a) {
    snprintf(errbuf, errcap, "archive_read_new failed");
    return false;
  }
  archive_read_support_filter_all(a);
  archive_read_support_format_all(a);
  if (archive_read_open_filename(a, path, 16384) != ARCHIVE_OK) {
    set_archive_err(a, errbuf, errcap, "failed to open archive");
    archive_read_free(a);
    return false;
  }
  *out = a;
  return true;
}

bool bbs_archive_list_to_text(const char* archive_path, char* out, size_t outcap,
                              int max_entries, char* errbuf, size_t errcap) {
  if (!out || outcap == 0) return false;
  out[0] = '\0';
  struct archive* a = NULL;
  if (!open_archive_for_read(archive_path, &a, errbuf, errcap)) return false;
  struct archive_entry* ent = NULL;
  int count = 0;
  size_t off = 0;
  while (archive_read_next_header(a, &ent) == ARCHIVE_OK) {
    const char* name = archive_entry_pathname(ent);
    la_int64_t size = archive_entry_size(ent);
    off += (size_t)snprintf(out + off, off < outcap ? outcap - off : 0,
                            "%10lld  %s\r\n", size, name ? name : "(unnamed)");
    count++;
    archive_read_data_skip(a);
    if ((max_entries > 0 && count >= max_entries) || off >= outcap) break;
  }
  archive_read_close(a);
  archive_read_free(a);
  return true;
}

bool bbs_archive_test(const char* archive_path, char* errbuf, size_t errcap) {
  struct archive* a = NULL;
  if (!open_archive_for_read(archive_path, &a, errbuf, errcap)) return false;
  struct archive_entry* ent = NULL;
  char buf[16384];
  bool ok = true;
  int rc;
  while ((rc = archive_read_next_header(a, &ent)) == ARCHIVE_OK) {
    while (archive_read_data(a, buf, sizeof(buf)) > 0) {}
  }
  if (rc != ARCHIVE_EOF) {
    set_archive_err(a, errbuf, errcap, "archive read failed");
    ok = false;
  }
  archive_read_close(a);
  archive_read_free(a);
  return ok;
}

bool bbs_archive_extract_to_dir(const char* archive_path, const char* dest_dir,
                                char* errbuf, size_t errcap) {
  if (!bbs_mkdir_p(dest_dir, 0700)) {
    snprintf(errbuf, errcap, "failed to create extraction directory");
    return false;
  }
  struct archive* a = NULL;
  if (!open_archive_for_read(archive_path, &a, errbuf, errcap)) return false;
  struct archive_entry* ent = NULL;
  char buf[16384];
  bool ok = true;
  int rc;
  while ((rc = archive_read_next_header(a, &ent)) == ARCHIVE_OK) {
    const char* name = archive_entry_pathname(ent);
    const char* base = name ? strrchr(name, '/') : NULL;
    base = base ? base + 1 : name;
    if (!base || !bbs_safe_filename(base, 255) || archive_entry_filetype(ent) != AE_IFREG) {
      archive_read_data_skip(a);
      continue;
    }
    char outpath[1024];
    path_join(dest_dir, base, outpath, sizeof(outpath));
    int fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
      snprintf(errbuf, errcap, "open %s failed: %s", outpath, strerror(errno));
      ok = false;
      break;
    }
    la_int64_t n;
    while ((n = archive_read_data(a, buf, sizeof(buf))) > 0) {
      if (!fd_write_all(fd, buf, (size_t)n)) {
        snprintf(errbuf, errcap, "write %s failed: %s", outpath, strerror(errno));
        ok = false;
        break;
      }
    }
    close(fd);
    if (!ok) break;
  }
  if (ok && rc != ARCHIVE_EOF) {
    set_archive_err(a, errbuf, errcap, "archive extract failed");
    ok = false;
  }
  archive_read_close(a);
  archive_read_free(a);
  return ok;
}
