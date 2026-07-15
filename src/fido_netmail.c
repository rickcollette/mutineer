#include "bbs_fido_netmail.h"
#include "bbs_msg_defs.h"
#include "bbs_util.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static bool ensure_dir(const char *path)
{
  if (!path || !path[0])
    return false;
  struct stat st;
  if (stat(path, &st) == 0)
    return S_ISDIR(st.st_mode);
  return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static void fido_copy(char *dst, size_t cap, const char *src)
{
  if (!dst || cap == 0) return;
  if (!src) src = "";
  size_t n = strnlen(src, cap - 1);
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void write_body(FILE *f, const char *body)
{
  for (const char *p = body ? body : ""; *p; p++)
  {
    if (*p == '\r')
      continue;
    fputc(*p, f);
  }
  fputc('\n', f);
}

static bool write_netmail_file(const char *path, const DbFidoNetmail *nm)
{
  FILE *f = fopen(path, "w");
  if (!f)
    return false;

  fprintf(f, "MUTINEER-FTN-NETMAIL 1\n");
  fprintf(f, "Message-ID: %d\n", nm->id);
  fprintf(f, "From: %s <%d:%d/%d.%d>\n", nm->from_name, nm->from_zone, nm->from_net, nm->from_node, nm->from_point);
  fprintf(f, "To: %s <%d:%d/%d.%d>\n", nm->to_name, nm->to_zone, nm->to_net, nm->to_node, nm->to_point);
  fprintf(f, "Subject: %s\n", nm->subject);
  fprintf(f, "Attr: %u\n", nm->attr | NET_ATTR_SENT);
  fprintf(f, "Created: %s\n", nm->created_at);
  fprintf(f, "\n");
  write_body(f, nm->body);

  return fclose(f) == 0;
}

bool fido_netmail_export_pending(BbsDb *db, const char *out_dir, int limit, bool dry_run,
                                 FidoNetmailExportResult *result)
{
  if (!db || !out_dir || !result)
    return false;

  memset(result, 0, sizeof(*result));
  if (limit <= 0)
    limit = 100;

  if (!dry_run && !ensure_dir(out_dir))
    return false;

  DbFidoNetmail mails[100];
  int max = limit > 100 ? 100 : limit;
  int count = db_fido_netmail_list(db, "pending", mails, max);
  result->scanned = count;

  for (int i = 0; i < count; i++)
  {
    char path[256];
    char leaf[96];
    snprintf(leaf, sizeof(leaf), "netmail_%08d_%ld.pkt", mails[i].id, (long)time(NULL));
    path_join(out_dir, leaf, path, sizeof(path));

    if (dry_run)
    {
      result->exported++;
      fido_copy(result->last_path, sizeof(result->last_path), path);
      continue;
    }

    if (!write_netmail_file(path, &mails[i]))
    {
      result->failed++;
      unlink(path);
      continue;
    }

    if (!db_mail_packet_add(db, 1, "fido-netmail", path) || !db_fido_netmail_mark_sent(db, mails[i].id))
    {
      result->failed++;
      unlink(path);
      continue;
    }

    result->exported++;
    fido_copy(result->last_path, sizeof(result->last_path), path);
  }

  return result->failed == 0;
}

/* =========================================================================
 * Echomail export
 * =========================================================================
 * Format: MUTINEER-FTN-ECHOMAIL 1 header (similar to netmail) followed
 * by the message body. External tossers consume these files.
 */

static bool write_echomail_file(const char *path, const DbFidoEcholink *el,
                                const DbFidoAka *aka, const DbMessage *msg)
{
  FILE *f = fopen(path, "w");
  if (!f) return false;

  fprintf(f, "MUTINEER-FTN-ECHOMAIL 1\n");
  fprintf(f, "Message-ID: %d\n", msg->id);
  fprintf(f, "Area: %s\n", el->echotag);
  fprintf(f, "From: %s <%d:%d/%d.%d>\n",
          msg->from_name[0] ? msg->from_name : msg->user_handle,
          aka->zone, aka->net, aka->node, aka->point);
  fprintf(f, "To: %s\n", msg->to_name[0] ? msg->to_name : "All");
  fprintf(f, "Subject: %s\n", msg->subject);
  if (el->origin[0])
    fprintf(f, "Origin: %s\n", el->origin);
  fprintf(f, "Created: %s\n", msg->created_at);
  fprintf(f, "\n");

  /* Write body with FTN tearline */
  for (const char *p = msg->body[0] ? msg->body : ""; *p; p++) {
    if (*p == '\r') continue;
    fputc(*p, f);
  }
  fprintf(f, "\n--- Mutineer BBS\n");

  return fclose(f) == 0;
}

bool fido_echomail_export_pending(BbsDb *db, const char *out_dir, int limit, bool dry_run,
                                  FidoEchomailExportResult *result)
{
  if (!db || !out_dir || !result) return false;
  memset(result, 0, sizeof(*result));
  if (limit <= 0) limit = 200;

  if (!dry_run && !ensure_dir(out_dir)) return false;

  DbFidoEcholink links[32];
  int lcount = db_fido_echolink_list(db, links, 32);

  for (int li = 0; li < lcount && result->exported + result->failed < limit; li++) {
    DbFidoAka aka;
    memset(&aka, 0, sizeof(aka));
    db_fido_aka_get(db, links[li].aka_id, &aka);

    int message_ids[200];
    int remaining = limit - result->exported - result->failed;
    if (remaining > 200) remaining = 200;
    int mcount = db_fido_echo_queue_pending(db, links[li].id, message_ids, remaining);
    result->scanned += mcount;

    for (int m = 0; m < mcount; m++) {
      DbMessage msg;
      if (!db_message_get(db, message_ids[m], &msg)) {
        result->failed++;
        continue;
      }

      char path[512];
      char leaf[160];
      snprintf(leaf, sizeof(leaf), "echo_%.64s_%08d_%ld.pkt",
               links[li].echotag, msg.id, (long)time(NULL));
      path_join(out_dir, leaf, path, sizeof(path));

      if (dry_run) {
        result->exported++;
        continue;
      }

      if (!write_echomail_file(path, &links[li], &aka, &msg)) {
        result->failed++;
        continue;
      }

      if (!db_fido_echo_queue_mark_exported(db, links[li].id, msg.id)) {
        result->failed++;
        unlink(path);
        continue;
      }

      /* Update high-water mark */
      if (msg.id > links[li].high_water)
        db_fido_echolink_update_highwater(db, links[li].id, msg.id);

      result->exported++;
    }
  }

  return result->failed == 0;
}

/* =========================================================================
 * Echomail import
 * =========================================================================
 * Reads MUTINEER-FTN-ECHOMAIL 1 packet files from in_dir.
 * Matches by Area: echotag to a fido_echolink, posts to the linked area.
 * Moves processed files to in_dir/done/ and failures to in_dir/bad/.
 */

static char *trim_nl(char *s)
{
  size_t n = strlen(s);
  while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
  return s;
}

bool fido_echomail_import(BbsDb *db, const char *in_dir, FidoEchomailImportResult *result)
{
  if (!db || !in_dir || !result) return false;
  memset(result, 0, sizeof(*result));

  char done_dir[512], bad_dir[512];
  path_join(in_dir, "done", done_dir, sizeof(done_dir));
  path_join(in_dir, "bad", bad_dir, sizeof(bad_dir));
  ensure_dir(done_dir);
  ensure_dir(bad_dir);

  DIR *d = opendir(in_dir);
  if (!d) return false;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    /* Only process .pkt files starting with "echo_" */
    if (strncmp(ent->d_name, "echo_", 5) != 0) continue;
    const char *dot = strrchr(ent->d_name, '.');
    if (!dot || strcmp(dot, ".pkt") != 0) continue;

    char filepath[512];
    path_join(in_dir, ent->d_name, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (!f) continue;

    result->scanned++;

    char line[512];
    /* First line must be the magic header */
    if (!fgets(line, sizeof(line), f) || strncmp(line, "MUTINEER-FTN-ECHOMAIL 1", 23) != 0) {
      fclose(f);
      char bad_path[512];
      path_join(bad_dir, ent->d_name, bad_path, sizeof(bad_path));
      rename(filepath, bad_path);
      result->failed++;
      continue;
    }

    /* Parse headers */
    char echotag[64] = {0}, from_name[64] = {0}, to_name[64] = {0};
    char subject[80] = {0}, created_at[32] = {0};

    while (fgets(line, sizeof(line), f)) {
      trim_nl(line);
      if (line[0] == '\0') break; /* blank line = start of body */
      if (strncmp(line, "Area: ", 6) == 0)
        fido_copy(echotag, sizeof(echotag), line + 6);
      else if (strncmp(line, "From: ", 6) == 0) {
        /* "From: Name <addr>" — extract name part */
        char *lt = strchr(line + 6, '<');
        if (lt) { *lt = '\0'; fido_copy(from_name, sizeof(from_name), line + 6); }
        else      fido_copy(from_name, sizeof(from_name), line + 6);
        /* trim trailing space */
        size_t n = strlen(from_name);
        while (n > 0 && from_name[n-1] == ' ') from_name[--n] = '\0';
      }
      else if (strncmp(line, "To: ", 4) == 0)
        fido_copy(to_name, sizeof(to_name), line + 4);
      else if (strncmp(line, "Subject: ", 9) == 0)
        fido_copy(subject, sizeof(subject), line + 9);
      else if (strncmp(line, "Created: ", 9) == 0)
        fido_copy(created_at, sizeof(created_at), line + 9);
    }

    /* Read body (up to 2048 bytes, stripping FTN tearline) */
    char body[2048] = {0};
    size_t blen = 0;
    while (fgets(line, sizeof(line), f) && blen < sizeof(body) - 2) {
      /* Skip FTN tearline and seen-by/path lines */
      if (strncmp(line, "---", 3) == 0) continue;
      if (strncmp(line, "SEEN-BY:", 8) == 0) continue;
      if (strncmp(line, "\001PATH:", 6) == 0) continue;
      size_t ll = strlen(line);
      if (blen + ll < sizeof(body) - 1) {
        memcpy(body + blen, line, ll);
        blen += ll;
      }
    }
    body[blen] = '\0';
    fclose(f);

    if (!echotag[0] || !subject[0]) {
      char bad_path[512];
      path_join(bad_dir, ent->d_name, bad_path, sizeof(bad_path));
      rename(filepath, bad_path);
      result->failed++;
      continue;
    }

    /* Find the echolink for this tag */
    DbFidoEcholink links[32];
    int lcount = db_fido_echolink_list(db, links, 32);
    int linked_area_id = 0;
    for (int i = 0; i < lcount; i++) {
      if (strcasecmp(links[i].echotag, echotag) == 0) {
        linked_area_id = links[i].area_id;
        break;
      }
    }

    if (!linked_area_id) {
      /* No echolink for this tag — discard gracefully */
      char bad_path[512];
      path_join(bad_dir, ent->d_name, bad_path, sizeof(bad_path));
      rename(filepath, bad_path);
      result->failed++;
      continue;
    }

    /* Post to the linked message area as a "gateway" post (user_id=0) */
    DbMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.area_id = linked_area_id;
    msg.user_id = 0;
    snprintf(msg.user_handle, sizeof(msg.user_handle), "FidoNet");
    fido_copy(msg.from_name, sizeof(msg.from_name), from_name[0] ? from_name : "FidoNet");
    fido_copy(msg.to_name, sizeof(msg.to_name), to_name[0] ? to_name : "All");
    fido_copy(msg.subject, sizeof(msg.subject), subject);
    fido_copy(msg.body, sizeof(msg.body), body);

    if (db_message_post_ex(db, &msg)) {
      result->imported++;
      char done_path[512];
      path_join(done_dir, ent->d_name, done_path, sizeof(done_path));
      rename(filepath, done_path);
    } else {
      result->failed++;
      char bad_path[512];
      path_join(bad_dir, ent->d_name, bad_path, sizeof(bad_path));
      rename(filepath, bad_path);
    }
  }

  closedir(d);
  return result->failed == 0;
}
