#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "bbs_config.h"
#include "bbs_db.h"
#include "bbs_log.h"
#include "bbs_hash.h"
#include "bbs_scheduler.h"
#include "bbs_session.h"
#include "bbs_startup.h"
#include "bbs_chat.h"
#include "bbs_plugin_loader.h"
#include "bbs_console_service.h"

int net_run_listener(const struct BbsConfig* cfg, struct BbsDb* db, volatile sig_atomic_t* stop_flag);
void broadcast_check(const char* data_path);

volatile sig_atomic_t g_stop = 0;
volatile sig_atomic_t g_broadcast_pending = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }
static void on_sigusr1(int sig) { (void)sig; g_broadcast_pending = 1; }

static void usage(const char* argv0) {
  fprintf(stderr, "Usage: %s [-c|--config conf/mutineer.conf]\n", argv0);
}

int main(int argc, char** argv) {
  const char* cfg_path = "conf/mutineer.conf";
  for (int i = 1; i < argc; i++) {
    if ((!strcmp(argv[i], "--config") || !strcmp(argv[i], "-c")) && i + 1 < argc) {
      cfg_path = argv[++i];
    } else if (!strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  BbsConfig cfg;
  if (!cfg_load(cfg_path, &cfg)) {
    fprintf(stderr, "Failed to load config: %s\n", cfg_path);
    return 2;
  }

  log_init(cfg.logs_path);
  log_info("mutineer starting with config %s", cfg_path);

  signal(SIGINT, on_sigint);
  signal(SIGTERM, on_sigint);
  signal(SIGUSR1, on_sigusr1);  /* Trigger broadcast file check */
  signal(SIGPIPE, SIG_IGN);  /* Ignore SIGPIPE to prevent crash on client disconnect */

  /* Run startup sanity checks */
  if (!startup_sanity_check(&cfg)) {
    fprintf(stderr, "\nStartup sanity checks failed.\n");
    fprintf(stderr, "Cannot start BBS with missing required files.\n");
    log_error("Startup sanity checks failed - exiting");
    log_close();
    return 2;
  }

  BbsDb* db = db_open(cfg.db_path);
  if (!db) {
    fprintf(stderr, "DB open failed: %s\n", cfg.db_path);
    fprintf(stderr, "Error: %s\n", db_last_error(NULL));
    fprintf(stderr, "\nRun 'mutineer-initbbs' to create the database.\n");
    log_error("DB open failed: %s", cfg.db_path);
    log_close();
    return 2;
  }

  /* Check database has required schema and seed data */
  if (!startup_init_database(db, &cfg)) {
    fprintf(stderr, "\nDatabase not properly initialized.\n");
    fprintf(stderr, "Cannot start BBS without required database setup.\n");
    log_error("Database initialization check failed - exiting");
    db_close(db);
    log_close();
    return 2;
  }

  int locked_nodes[256];
  int locked_count = db_node_lock_list(db, locked_nodes, 256);
  for (int i = 0; i < locked_count; i++) {
    online_set_node_locked(locked_nodes[i], true);
  }
  if (locked_count > 0) {
    log_info("restored %d persistent node locks", locked_count);
  }

  fprintf(stdout, "mutineer listening on %s:%d\n", cfg.bind, cfg.port);
  log_info("listening on %s:%d", cfg.bind, cfg.port);
  fflush(stdout);

  /* Initialize teleconference system */
  teleconf_init();
  if (db) {
    teleconf_set_db(db);
  }

  /* Initialize plugin loader */
  if (!plugin_loader_init(&cfg)) {
    log_warn("plugin loader initialization failed");
  }

  /* start remote console and scheduler threads */
  console_service_start(&cfg, db);
  scheduler_start(&cfg, db);

  int rc = net_run_listener(&cfg, db, &g_stop);

  online_broadcast("\r\nSystem shutting down.\r\n");
  scheduler_stop();
  console_service_stop();
  plugin_loader_shutdown();
  db_close(db);
  log_info("shutdown");
  log_close();
  return rc;
}
