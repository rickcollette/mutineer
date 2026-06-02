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
    int processed = 0;
    
    /* Query pending fanout items */
    /* This would need proper query implementation */
    
    /* For each pending item:
     * 1. Load the object
     * 2. Determine target links based on subscriptions
     * 3. Queue for outbound to each target
     * 4. Mark fanout complete
     */
    
    return processed;
}

static int process_upstream_queue(void) {
    int processed = 0;
    
    /* Query items that need to be sent upstream */
    /* This would need proper query implementation */
    
    return processed;
}

/* ============================================================================
 * SUBSCRIPTION MANAGEMENT
 * ============================================================================ */

static bool process_subscription_request(const plank_object_t* obj) {
    if (!obj || obj->object_class != PLANK_CLASS_SUBSCRIPTION) {
        return false;
    }
    
    /* Decode subscription body */
    /* This would need proper implementation */
    
    plank_log(PLANK_LOG_INFO, "coved", "Processing subscription request");
    
    return true;
}

/* ============================================================================
 * AREA AUTHORITY
 * ============================================================================ */

static bool is_area_authority(const char* area_addr) {
    if (!area_addr) return false;
    
    /* Check if this COVE is the authority for the area */
    /* This would need proper query implementation */
    
    return false;
}

static bool process_area_definition(const plank_object_t* obj) {
    if (!obj || obj->object_class != PLANK_CLASS_AREA_DEFINITION) {
        return false;
    }
    
    /* Decode and store area definition */
    /* This would need proper implementation */
    
    plank_log(PLANK_LOG_INFO, "coved", "Processing area definition");
    
    return true;
}

/* ============================================================================
 * MODERATION DISTRIBUTION
 * ============================================================================ */

static bool distribute_moderation(const plank_object_t* obj) {
    if (!obj || obj->object_class != PLANK_CLASS_MODERATION) {
        return false;
    }
    
    /* Distribute moderation event to all subscribers of the affected area */
    /* This would need proper implementation */
    
    plank_log(PLANK_LOG_INFO, "coved", "Distributing moderation event");
    
    return true;
}

/* ============================================================================
 * JOURNAL PROCESSING
 * ============================================================================ */

static int process_journal(void) {
    int processed = 0;
    
    /* Query unprocessed journal entries */
    /* For each entry:
     * 1. Load the object
     * 2. Validate against policy
     * 3. If message: determine fanout targets
     * 4. If subscription: update subscription state
     * 5. If moderation: apply and distribute
     * 6. Mark as processed
     */
    
    /* Query: SELECT * FROM plank_journal WHERE processing_state = 0 ORDER BY id LIMIT batch_size */
    
    return processed;
}

/* ============================================================================
 * HEALTH MONITORING
 * ============================================================================ */

static void check_downstream_health(void) {
    /* Query downstream links and check their health */
    /* This would need proper query implementation */
    
    /* For each downstream:
     * - Check last successful exchange time
     * - Check pending queue size
     * - Check error rate
     * - Update health status
     */
}

static void check_upstream_health(void) {
    /* Query upstream links and check their health */
    /* This would need proper query implementation */
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
    
    /* Clean up completed fanout entries */
    /* This would need proper query implementation */
    
    /* Archive old audit logs */
    /* This would need proper query implementation */
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
