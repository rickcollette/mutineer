/*
 * plankd - PLANK Link Daemon
 * Maintains node-to-node sessions, performs authenticated exchange,
 * pushes and pulls bundles, manages retry, ack, dedupe, cursor advancement
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
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <time.h>

#define PLANKD_VERSION "1.0.0"
#define MAX_LINKS 64
#define MAX_SESSIONS 128
#define POLL_TIMEOUT_MS 1000

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

/* Link management */
typedef struct {
    int link_id;
    plank_link_t link_config;
    plank_link_session_t* session;
    time_t next_connect_time;
    int retry_count;
    bool active;
} managed_link_t;

static managed_link_t g_links[MAX_LINKS];
static int g_link_count = 0;

/* Listener socket */
static int g_listen_fd = -1;
static int g_listen_port = 0;

static bool plankd_make_temp_file(const char* prefix, char* path, size_t path_size, int* fd_out) {
    if (!path || path_size == 0 || !fd_out) return false;
    const char* tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
    int n = snprintf(path, path_size, "%s/%s_XXXXXX", tmpdir, prefix ? prefix : "plankd");
    if (n < 0 || (size_t)n >= path_size) return false;
    int fd = mkstemp(path);
    if (fd < 0) return false;
    *fd_out = fd;
    return true;
}

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
 * LINK SESSION CALLBACKS
 * ============================================================================ */

static void on_bundle_received(plank_link_session_t* session,
                               const uint8_t* bundle_id,
                               const uint8_t* data, size_t len,
                               void* ctx) {
    managed_link_t* link = (managed_link_t*)ctx;
    (void)session;
    
    if (len > 0 && data) {
        char temp_path[512];
        int fd = -1;
        if (!plankd_make_temp_file("plank_bundle", temp_path, sizeof(temp_path), &fd)) {
            plank_link_session_send_receipt(session, PLANK_RC_REJECTED,
                                            PLANK_TARGET_BUNDLE, bundle_id,
                                            0, 0, 1, 0, "temporary file creation failed");
            return;
        }

        size_t written = 0;
        while (written < len) {
            ssize_t n = write(fd, data + written, len - written);
            if (n <= 0) {
                close(fd);
                unlink(temp_path);
                plank_link_session_send_receipt(session, PLANK_RC_REJECTED,
                                                PLANK_TARGET_BUNDLE, bundle_id,
                                                0, 0, 1, 0, "temporary bundle write failed");
                return;
            }
            written += (size_t)n;
        }
        close(fd);

        plank_bundle_import_result_t result;
        memset(&result, 0, sizeof(result));

        if (plank_bundle_import(g_store, temp_path, link->link_id, NULL, &result)) {
            plank_log(PLANK_LOG_INFO, "plankd",
                      "Imported bundle: %d objects",
                      result.objects_accepted);

            plank_link_session_send_receipt(session, PLANK_RC_OK,
                                            PLANK_TARGET_BUNDLE, bundle_id,
                                            result.objects_accepted, result.objects_duplicate,
                                            result.objects_rejected, result.objects_quarantined, NULL);
        } else {
            plank_link_session_send_receipt(session, PLANK_RC_REJECTED,
                                            PLANK_TARGET_BUNDLE, bundle_id,
                                            0, 0, 1, 0, result.error);
        }

        unlink(temp_path);
    }
}

static void on_receipt(plank_link_session_t* session,
                       plank_receipt_code_t code,
                       const uint8_t* target_id,
                       int accepted, int duplicate, int rejected, int quarantine,
                       void* ctx) {
    (void)session; (void)target_id; (void)ctx;
    
    plank_log(PLANK_LOG_DEBUG, "plankd",
              "Received receipt: code=%d accepted=%d dup=%d rej=%d quar=%d",
              code, accepted, duplicate, rejected, quarantine);
}

static void on_error(plank_link_session_t* session,
                     plank_error_code_t code, bool fatal, const char* message,
                     void* ctx) {
    managed_link_t* link = (managed_link_t*)ctx;
    (void)session;
    
    plank_log(fatal ? PLANK_LOG_ERROR : PLANK_LOG_WARN, "plankd",
              "Link %d error %d: %s", link->link_id, code, message);
    
    if (fatal && link->session) {
        plank_link_session_free(link->session);
        link->session = NULL;
        
        plank_retry_policy_t policy;
        plank_router_get_retry_policy(g_router, &policy);
        int delay = plank_router_calc_retry_delay(g_router, link->retry_count);
        link->next_connect_time = time(NULL) + delay;
        link->retry_count++;
    }
}

/* ============================================================================
 * LINK MANAGEMENT
 * ============================================================================ */

static bool load_links(void) {
    plank_log(PLANK_LOG_INFO, "plankd", "Loading link configuration");
    g_link_count = 0;
    return true;
}

static bool connect_link(managed_link_t* link) {
    if (link->session) {
        plank_link_session_free(link->session);
        link->session = NULL;
    }
    
    plank_link_callbacks_t callbacks = {
        .on_bundle_received = on_bundle_received,
        .on_receipt = on_receipt,
        .on_error = on_error,
        .ctx = link
    };
    
    link->session = plank_link_session_create_outbound(g_store, link->link_id, &callbacks);
    if (!link->session) {
        plank_log(PLANK_LOG_ERROR, "plankd",
                  "Failed to create session for link %d", link->link_id);
        return false;
    }
    
    if (!plank_link_session_connect(link->session,
                                    link->link_config.remote_host,
                                    link->link_config.remote_port,
                                    30)) {
        plank_log(PLANK_LOG_ERROR, "plankd",
                  "Failed to connect link %d: %s",
                  link->link_id, plank_last_error());
        plank_link_session_free(link->session);
        link->session = NULL;
        return false;
    }
    
    if (!plank_link_session_tls_handshake(link->session)) {
        plank_log(PLANK_LOG_ERROR, "plankd",
                  "TLS handshake failed for link %d: %s",
                  link->link_id, plank_last_error());
        plank_link_session_free(link->session);
        link->session = NULL;
        return false;
    }
    
    if (!plank_link_session_send_hello(link->session)) {
        plank_log(PLANK_LOG_ERROR, "plankd",
                  "Failed to send HELLO for link %d", link->link_id);
        plank_link_session_free(link->session);
        link->session = NULL;
        return false;
    }
    
    return true;
}

/* ============================================================================
 * LISTENER
 * ============================================================================ */

static bool setup_listener(int port) {
    g_listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        plank_log(PLANK_LOG_ERROR, "plankd", "Failed to create listener socket");
        return false;
    }
    
    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    opt = 0;
    setsockopt(g_listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
    
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;
    
    if (bind(g_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        plank_log(PLANK_LOG_ERROR, "plankd", "Failed to bind to port %d: %s",
                  port, strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return false;
    }
    
    if (listen(g_listen_fd, 16) < 0) {
        plank_log(PLANK_LOG_ERROR, "plankd", "Failed to listen: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return false;
    }
    
    g_listen_port = port;
    plank_log(PLANK_LOG_INFO, "plankd", "Listening on port %d", port);
    
    return true;
}

static void accept_connection(void) {
    struct sockaddr_in6 client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(g_listen_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            plank_log(PLANK_LOG_ERROR, "plankd", "Accept failed: %s", strerror(errno));
        }
        return;
    }
    
    plank_log(PLANK_LOG_INFO, "plankd", "Accepted inbound connection");
    
    plank_link_callbacks_t callbacks = {
        .on_bundle_received = NULL,
        .on_receipt = NULL,
        .on_error = NULL,
        .ctx = NULL
    };
    
    plank_link_session_t* session = plank_link_session_create_inbound(g_store, client_fd, &callbacks);
    if (!session) {
        plank_log(PLANK_LOG_ERROR, "plankd", "Failed to create inbound session");
        close(client_fd);
        return;
    }
    
    if (!plank_link_session_tls_handshake(session)) {
        plank_log(PLANK_LOG_ERROR, "plankd", "Inbound TLS handshake failed");
        plank_link_session_free(session);
        return;
    }
    
    plank_link_session_free(session);
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

static void process_outbound_queue(void) {
}

static void run_daemon(void) {
    plank_log(PLANK_LOG_INFO, "plankd", "Starting main loop");
    
    while (g_running) {
        if (g_reload) {
            plank_log(PLANK_LOG_INFO, "plankd", "Reloading configuration");
            load_links();
            g_reload = 0;
        }
        
        time_t now = time(NULL);
        
        for (int i = 0; i < g_link_count; i++) {
            managed_link_t* link = &g_links[i];
            
            if (!link->active || !link->link_config.enabled) continue;
            
            if (link->link_config.direction == PLANK_DIR_OUTBOUND) {
                if (!link->session && now >= link->next_connect_time) {
                    plank_retry_policy_t policy;
                    plank_router_get_retry_policy(g_router, &policy);
                    
                    if (link->retry_count < policy.limit) {
                        plank_log(PLANK_LOG_INFO, "plankd",
                                  "Connecting link %d (attempt %d)",
                                  link->link_id, link->retry_count + 1);
                        connect_link(link);
                    }
                }
            }
        }
        
        struct pollfd pfds[MAX_SESSIONS + 1];
        int pfd_count = 0;
        
        if (g_listen_fd >= 0) {
            pfds[pfd_count].fd = g_listen_fd;
            pfds[pfd_count].events = POLLIN;
            pfd_count++;
        }
        
        for (int i = 0; i < g_link_count; i++) {
            if (g_links[i].session) {
                int fd = plank_link_session_fd(g_links[i].session);
                if (fd >= 0) {
                    pfds[pfd_count].fd = fd;
                    pfds[pfd_count].events = POLLIN;
                    pfd_count++;
                }
            }
        }
        
        int ret = poll(pfds, pfd_count, POLL_TIMEOUT_MS);
        if (ret < 0) {
            if (errno != EINTR) {
                plank_log(PLANK_LOG_ERROR, "plankd", "Poll error: %s", strerror(errno));
            }
            continue;
        }
        
        int pfd_idx = 0;
        if (g_listen_fd >= 0) {
            if (pfds[pfd_idx].revents & POLLIN) {
                accept_connection();
            }
            pfd_idx++;
        }
        
        for (int i = 0; i < g_link_count && pfd_idx < pfd_count; i++) {
            if (g_links[i].session) {
                if (pfds[pfd_idx].revents & POLLIN) {
                    plank_link_session_process(g_links[i].session);
                }
                pfd_idx++;
            }
        }
        
        process_outbound_queue();
    }
    
    plank_log(PLANK_LOG_INFO, "plankd", "Shutting down");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c, --config FILE    Configuration file (default: /etc/mutineer/mutineer.conf)\n");
    fprintf(stderr, "  -d, --database FILE  Database file (default: from config)\n");
    fprintf(stderr, "  -p, --port PORT      Listen port (default: 5150)\n");
    fprintf(stderr, "  -f, --foreground     Run in foreground\n");
    fprintf(stderr, "  -v, --verbose        Verbose logging\n");
    fprintf(stderr, "  -h, --help           Show this help\n");
    fprintf(stderr, "  -V, --version        Show version\n");
}

int main(int argc, char* argv[]) {
    const char* config_file = "/etc/mutineer/mutineer.conf";
    const char* db_file = NULL;
    int listen_port = 5150;
    bool foreground = false;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"config",     required_argument, 0, 'c'},
        {"database",   required_argument, 0, 'd'},
        {"port",       required_argument, 0, 'p'},
        {"foreground", no_argument,       0, 'f'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:d:p:fvhV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            case 'd':
                db_file = optarg;
                break;
            case 'p':
                listen_port = atoi(optarg);
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
                printf("plankd %s (PLANK protocol %d)\n",
                       PLANKD_VERSION, PLANK_PROTOCOL_VERSION);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }
    
    plank_set_log_callback(log_callback, NULL);
    plank_set_log_level(verbose ? PLANK_LOG_DEBUG : PLANK_LOG_INFO);
    
    if (!cfg_load(config_file, &g_config)) {
        fprintf(stderr, "Failed to load configuration: %s\n", config_file);
        plank_shutdown();
        return 1;
    }
    
    if (!db_file) {
        db_file = g_config.db_path;
    }
    
    g_db = db_open(db_file);
    if (!g_db) {
        fprintf(stderr, "Failed to open database: %s\n", db_file);
        plank_shutdown();
        return 1;
    }
    
    g_store = plank_store_open(g_db);
    if (!g_store) {
        fprintf(stderr, "Failed to initialize PLANK store\n");
        db_close(g_db);
        plank_shutdown();
        return 1;
    }
    
    g_router = plank_router_create(g_store);
    g_policy = plank_policy_create(g_store);
    
    if (!g_router || !g_policy) {
        fprintf(stderr, "Failed to initialize router/policy\n");
        plank_store_close(g_store);
        db_close(g_db);
        plank_shutdown();
        return 1;
    }
    
    if (!load_links()) {
        fprintf(stderr, "Failed to load link configuration\n");
    }
    
    if (listen_port > 0) {
        if (!setup_listener(listen_port)) {
            fprintf(stderr, "Failed to set up listener\n");
        }
    }
    
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
        freopen("/var/log/plankd.log", "a", stdout);
        freopen("/var/log/plankd.log", "a", stderr);
    }
    
    setup_signals();
    
    plank_log(PLANK_LOG_INFO, "plankd", "PLANK Link Daemon starting (version %s)",
              PLANKD_VERSION);
    
    run_daemon();
    
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
    }
    
    for (int i = 0; i < g_link_count; i++) {
        if (g_links[i].session) {
            plank_link_session_free(g_links[i].session);
        }
    }
    
    plank_policy_free(g_policy);
    plank_router_free(g_router);
    plank_store_close(g_store);
    db_close(g_db);
    plank_shutdown();
    
    return 0;
}
