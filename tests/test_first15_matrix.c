#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

static char* read_file(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = (char*)calloc(1, (size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  return buf;
}

static int contains(const char* haystack, const char* needle) {
  return haystack && needle && strstr(haystack, needle) != NULL;
}

int main(void) {
  char* msg = read_file("src/msg_cmds.c");
  char* file = read_file("src/file_cmds.c");
  char* qwk = read_file("src/qwk.c");
  char* db = read_file("src/db.c");
  char* session = read_file("src/session.c");
  char* net = read_file("src/net_listener.c");
  char* coved = read_file("src/tools/coved.c");
  char* parity = read_file("docs/PARITY.md");

  CHECK(msg && file && qwk && db && session && net && coved && parity, "read source files");

  CHECK(contains(msg, "static bool msg_area_can") &&
        contains(msg, "static bool msg_can_read") &&
        contains(msg, "static bool msg_can_reply"),
        "message commands use shared read/post authorization helpers");
  CHECK(contains(msg, "realpath(root, realroot)") &&
        contains(msg, "Attachment path is invalid or not accessible"),
        "attachment downloads canonicalize under attachment root");

  CHECK(contains(file, "static bool file_download_allowed") &&
        contains(file, "FILE_FLAG_NOTVAL") &&
        contains(file, "sl.dl_one_day") &&
        contains(file, "sl.dl_k_one_day"),
        "file download policy enforces validation and daily limits");
  CHECK(contains(session, "cmd_file_batch_download(s, NULL);"),
        "batchrun delegates to hardened batch download policy");

  CHECK(contains(qwk, "bbs_archive_create_zip_from_dir") &&
        contains(qwk, "bbs_archive_extract_to_dir") &&
        contains(qwk, "mkdtemp") &&
        !contains(qwk, "system(") &&
        !contains(qwk, "popen("),
        "QWK uses libarchive and safe temp dirs without shell execution");
  CHECK(contains(file, "bbs_archive_list_to_text") &&
        contains(file, "bbs_archive_test") &&
        contains(file, "bbs_archive_extract_to_dir") &&
        !contains(file, "popen(") &&
        !contains(file, "system("),
        "archive commands use libarchive without shell execution");

  CHECK(contains(db, "DELETE FROM fido_echomail_queue WHERE echolink_id = ?1") &&
        contains(db, "DELETE FROM qwk_area_links WHERE hub_id = ?1") &&
        contains(db, "DELETE FROM qwk_packet_queue WHERE hub_id = ?1"),
        "Fido and QWK delete helpers bind child cleanup statements");

  CHECK(contains(session, "split_chat_start(s, target)") &&
        contains(session, "online_send_user_id") &&
        contains(session, "online_send_node"),
        "split chat and online notification helpers use single safe paths");
  CHECK(contains(net, "if (!s->db)") &&
        contains(net, "System is temporarily unavailable"),
        "net listener handles per-session db_open failure");
  CHECK(contains(session, "g_recovery_attempts") &&
        contains(session, "Confirm new password") &&
        contains(session, "db_user_set_pw_with_timestamp") &&
        contains(session, "Password changed. Please log in again."),
        "password recovery is throttled, confirmed, timestamped, and forces fresh login");

  CHECK(!contains(coved, "would need proper") &&
        contains(coved, "INSERT INTO cove_fanout_queue") &&
        contains(coved, "INSERT INTO plank_outbound_queue") &&
        contains(coved, "cove_downstream_health") &&
        contains(coved, "plank_quarantine"),
        "coved processes journal/fanout/upstream/health/maintenance tables");

  CHECK(contains(parity, "Full green-themed ANSI art pack. **Status:** ✅") &&
        contains(parity, "Full menu tree") &&
        contains(parity, "Status:** ✅"),
        "parity docs mark menu and ANSI art status complete");

  free(msg);
  free(file);
  free(qwk);
  free(db);
  free(session);
  free(net);
  free(coved);
  free(parity);
  return 0;
}
