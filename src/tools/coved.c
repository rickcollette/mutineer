/*
 * coved - COVE (Central Offline Vertex Exchange) Service
 * Hub-style redistribution daemon for PLANK networks
 */

#include "plank/plank.h"
#include "bbs_db.h"
#include "bbs_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>

#define COVED_VERSION "1.0.0"
#define FANOUT_BATCH_SIZE 100
#define PROCESS_INTERVAL_SEC 5

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_reload = 0;

static BbsConfig g_config;
static BbsDb* g_db = NULL;
static plank_store_t* g_store = NULL;
static plank_router_t* g_router = NULL;
static plank_policy_t* g_policy = NULL;

/* ============================================================================
 * SIGNAL HANDLERS
 * ============================================================================ */

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    } else if (sig == SIGHUP) {
        g_reload = 1;
    }
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    
    signal(SIGPIPE, SIG_IGN);
}

/* ============================================================================
 * LOGGING
 * ============================================================================ */

static void log_callback(plank_log_level_t level, const char* component,
                         const char* message, void* ctx) {
    (void)ctx;
    
    const char* level_str;
    switch (level) {
        case PLANK_LOG_DEBUG: level_str = "DEBUG"; break;
        case PLANK_LOG_INFO:  level_str = "INFO";  break;
        case PLANK_LOG_WARN:  level_str = "WARN";  break;
        case PLANK_LOG_ERROR: level_str = "ERROR"; break;
        default: level_str = "???"; break;
    }
    
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);
    
    fprintf(stderr, "[%s] [%s] [%s] %s\n", timestamp, level_str, component, message);
}

/* ============================================================================
 * COVE FANOUT PROCESSING
 * ============================================================================ */

static int process_fanout_queue(void) {
    if (!g_db) return 0;
    int before = db_changes(g_db);
    db_exec(g_db,
        "INSERT INTO plank_outbound_queue (link_id, object_id, priority) "
        "SELECT fq.target_link_id, fq.object_id, 0 "
        "FROM cove_fanout_queue fq "
        "WHERE fq.status = 0 "
        "AND NOT EXISTS (SELECT 1 FROM plank_outbound_queue oq "
        "                WHERE oq.link_id = fq.target_link_id AND oq.object_id = fq.object_id)");
    db_exec(g_db,
        "UPDATE cove_fanout_queue SET status = 1, sent_at = datetime('now') "
        "WHERE status = 0 AND EXISTS (SELECT 1 FROM plank_outbound_queue oq "
        "                            WHERE oq.link_id = cove_fanout_queue.target_link_id "
        "                              AND oq.object_id = cove_fanout_queue.object_id)");
    db_exec(g_db,
        "UPDATE cove_downstream SET backlog_count = "
        "(SELECT COUNT(*) FROM cove_fanout_queue fq "
        " WHERE fq.target_link_id = cove_downstream.link_id AND fq.status = 0), "
        "last_sync_at = datetime('now') "
        "WHERE link_id IN (SELECT DISTINCT target_link_id FROM cove_fanout_queue)");
    return db_changes(g_db) - before;
}

static int process_upstream_queue(void) {
    if (!g_db) return 0;
    int before = db_changes(g_db);
    db_exec(g_db,
        "INSERT INTO plank_outbound_queue (link_id, object_id, priority) "
        "SELECT cu.link_id, j.object_id, 1 "
        "FROM cove_upstream cu JOIN plank_journal j "
        "WHERE cu.status = 1 AND j.source_kind != 2 AND j.processing_state = 1 "
        "AND NOT EXISTS (SELECT 1 FROM plank_outbound_queue oq "
        "                WHERE oq.link_id = cu.link_id AND oq.object_id = j.object_id)");
    db_exec(g_db,
        "UPDATE cove_upstream SET last_sync_at = datetime('now') "
        "WHERE status = 1 AND link_id IN (SELECT DISTINCT link_id FROM plank_outbound_queue)");
    return db_changes(g_db) - before;
}

/* ============================================================================
 * JOURNAL PROCESSING
 * ============================================================================ */

static int process_journal(void) {
    if (!g_db) return 0;
    int before = db_changes(g_db);

    db_exec(g_db,
        "INSERT INTO cove_fanout_queue (object_id, area_addr, target_link_id) "
        "SELECT j.object_id, pm.area_addr, ps.link_id "
        "FROM plank_journal j "
        "JOIN plank_messages pm ON pm.object_id = j.object_id "
        "JOIN plank_areas pa ON pa.area_addr = pm.area_addr "
        "JOIN plank_subscriptions ps ON ps.area_id = pa.id "
        "JOIN plank_links l ON l.id = ps.link_id "
        "WHERE j.processing_state = 0 AND pm.area_addr IS NOT NULL "
        "AND ps.action IN (1,4) AND l.enabled = 1 AND l.paused = 0 "
        "AND (j.source_link_id IS NULL OR ps.link_id != j.source_link_id) "
        "AND NOT EXISTS (SELECT 1 FROM cove_fanout_queue fq "
        "                WHERE fq.object_id = j.object_id AND fq.target_link_id = ps.link_id)");

    db_exec(g_db,
        "INSERT INTO cove_downstream (link_id, subscribed_areas, status) "
        "SELECT DISTINCT ps.link_id, '[]', 1 FROM plank_subscriptions ps "
        "WHERE ps.action IN (1,4) "
        "ON CONFLICT(link_id) DO UPDATE SET status = 1");

    db_exec(g_db,
        "INSERT INTO plank_audit (event_type, object_id, details) "
        "SELECT 'cove_journal_processed', j.object_id, 'journal fanout queued' "
        "FROM plank_journal j WHERE j.processing_state = 0");

    db_exec(g_db,
        "UPDATE plank_journal SET processing_state = 1 "
        "WHERE processing_state = 0");

    return db_changes(g_db) - before;
}

/* ============================================================================
 * HEALTH MONITORING
 * ============================================================================ */

static void check_downstream_health(void) {
    if (!g_db) return;
    db_exec(g_db,
        "UPDATE cove_downstream SET backlog_count = "
        "(SELECT COUNT(*) FROM cove_fanout_queue fq "
        " WHERE fq.target_link_id = cove_downstream.link_id AND fq.status = 0)");
    db_exec(g_db,
        "INSERT INTO plank_audit (event_type, link_id, details) "
        "SELECT 'cove_downstream_health', link_id, "
        "'pending=' || backlog_count || ',status=' || status FROM cove_downstream");
}

static void check_upstream_health(void) {
    if (!g_db) return;
    db_exec(g_db,
        "INSERT INTO plank_audit (event_type, link_id, details) "
        "SELECT 'cove_upstream_health', link_id, 'status=' || status FROM cove_upstream");
}

/* ============================================================================
 * CLEANUP AND MAINTENANCE
 * ============================================================================ */

static void run_maintenance(void) {
    plank_log(PLANK_LOG_INFO, "coved", "Running maintenance tasks");
    
    /* Prune old dedupe records */
    int dedupe_days = plank_store_config_get_int(g_store, "bundle_dedupe_days", 30);
    int pruned = plank_store_dedupe_prune(g_store, dedupe_days);
    if (pruned > 0) {
        plank_log(PLANK_LOG_INFO, "coved", "Pruned %d dedupe records", pruned);
    }
    
    db_exec(g_db, "DELETE FROM cove_fanout_queue WHERE status = 1 AND sent_at < datetime('now','-7 days')");
    db_exec(g_db, "DELETE FROM plank_audit WHERE event_time < datetime('now','-90 days')");
    db_exec(g_db, "UPDATE plank_deadletters SET state = 2 WHERE state = 0 AND first_failure_at < datetime('now','-30 days')");
    db_exec(g_db, "UPDATE plank_quarantine SET resolution = 2, reviewed_at = datetime('now'), reviewed_by = 'coved' "
                  "WHERE resolution = 0 AND quarantined_at < datetime('now','-30 days')");
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

static void run_daemon(void) {
    plank_log(PLANK_LOG_INFO, "coved", "Starting COVE service");
    
    time_t last_process = 0;
    time_t last_maintenance = 0;
    
    while (g_running) {
        if (g_reload) {
            plank_log(PLANK_LOG_INFO, "coved", "Reloading configuration");
            /* Reload configuration */
            g_reload = 0;
        }
        
        time_t now = time(NULL);
        
        /* Process journal and fanout periodically */
        if (now - last_process >= PROCESS_INTERVAL_SEC) {
            int journal_count = process_journal();
            int fanout_count = process_fanout_queue();
            int upstream_count = process_upstream_queue();
            
            if (journal_count > 0 || fanout_count > 0 || upstream_count > 0) {
                plank_log(PLANK_LOG_DEBUG, "coved",
                          "Processed: journal=%d, fanout=%d, upstream=%d",
                          journal_count, fanout_count, upstream_count);
            }
            
            last_process = now;
        }
        
        /* Run maintenance hourly */
        if (now - last_maintenance >= 3600) {
            run_maintenance();
            check_downstream_health();
            check_upstream_health();
            last_maintenance = now;
        }
        
        /* Sleep briefly */
        usleep(100000);  /* 100ms */
    }
    
    plank_log(PLANK_LOG_INFO, "coved", "Shutting down");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c, --config FILE    Configuration file (default: /etc/mutineer/mutineer.conf)\n");
    fprintf(stderr, "  -d, --database FILE  Database file (default: from config)\n");
    fprintf(stderr, "  -f, --foreground     Run in foreground\n");
    fprintf(stderr, "  -v, --verbose        Verbose logging\n");
    fprintf(stderr, "  -h, --help           Show this help\n");
    fprintf(stderr, "  -V, --version        Show version\n");
}

int main(int argc, char* argv[]) {
    const char* config_file = "/etc/mutineer/mutineer.conf";
    const char* db_file = NULL;
    bool foreground = false;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"config",     required_argument, 0, 'c'},
        {"database",   required_argument, 0, 'd'},
        {"foreground", no_argument,       0, 'f'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:d:fvhV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            case 'd':
                db_file = optarg;
                break;
            case 'f':
                foreground = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'V':
                printf("coved %s (PLANK protocol %d)\n",
                       COVED_VERSION, PLANK_PROTOCOL_VERSION);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    /* Initialize PLANK */
    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }
    
    /* Set up logging */
    plank_set_log_callback(log_callback, NULL);
    plank_set_log_level(verbose ? PLANK_LOG_DEBUG : PLANK_LOG_INFO);
    
    /* Load configuration */
    if (!cfg_load(config_file, &g_config)) {
        fprintf(stderr, "Failed to load configuration: %s\n", config_file);
        plank_shutdown();
        return 1;
    }
    
    /* Determine database path */
    if (!db_file) {
        db_file = g_config.db_path;
    }
    
    /* Open database */
    g_db = db_open(db_file);
    if (!g_db) {
        fprintf(stderr, "Failed to open database: %s\n", db_file);
        plank_shutdown();
        return 1;
    }
    
    /* Initialize PLANK store */
    g_store = plank_store_open(g_db);
    if (!g_store) {
        fprintf(stderr, "Failed to initialize PLANK store\n");
        db_close(g_db);
        plank_shutdown();
        return 1;
    }
    
    /* Initialize router and policy */
    g_router = plank_router_create(g_store);
    g_policy = plank_policy_create(g_store);
    
    if (!g_router || !g_policy) {
        fprintf(stderr, "Failed to initialize router/policy\n");
        plank_store_close(g_store);
        db_close(g_db);
        plank_shutdown();
        return 1;
    }
    
    /* Verify this node is configured as COVE */
    plank_node_identity_t identity;
    if (plank_store_get_identity(g_store, &identity) && !identity.is_cove) {
        plank_log(PLANK_LOG_WARN, "coved",
                  "This node is not configured as COVE - running in limited mode");
    }
    
    /* Daemonize if not foreground */
    if (!foreground) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Fork failed\n");
            return 1;
        }
        if (pid > 0) {
            return 0;
        }
        
        setsid();
        freopen("/dev/null", "r", stdin);
        freopen("/var/log/coved.log", "a", stdout);
        freopen("/var/log/coved.log", "a", stderr);
    }
    
    /* Set up signals */
    setup_signals();
    
    plank_log(PLANK_LOG_INFO, "coved", "COVE Service starting (version %s)",
              COVED_VERSION);
    
    /* Run main loop */
    run_daemon();
    
    /* Cleanup */
    plank_policy_free(g_policy);
    plank_router_free(g_router);
    plank_store_close(g_store);
    db_close(g_db);
    plank_shutdown();
    
    return 0;
}
