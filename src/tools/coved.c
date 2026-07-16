#define _GNU_SOURCE
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
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SQLITE
#include <sqlite3.h>
#endif

#define COVED_VERSION "1.0.0"
#define FANOUT_BATCH_SIZE 100
#define PROCESS_INTERVAL_SEC 5
#define HUB_HTTP_MAX 16384

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
static BbsDb* g_mgmt_db = NULL;
static BbsDb* g_auth_db = NULL;

typedef enum {
    COVE_MODE_SERVICE = 0,
    COVE_MODE_HUB = 1
} cove_mode_t;

typedef struct {
    char source_path[256];
    char message_base_path[256];
    char base_id[128];
    char auth_db_path[256];
    char management_db_path[256];
    char bind[64];
    int port;
    char management_bind[64];
    int management_port;
    char management_token[128];
    int foreground;
} cove_hub_config_t;

static cove_mode_t g_mode = COVE_MODE_SERVICE;
static cove_hub_config_t g_hub_config;
static int g_hub_listen_fd = -1;
static int g_mgmt_listen_fd = -1;

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
 * HUB CONFIGURATION AND MANAGEMENT DB
 * ============================================================================ */

static void safe_copy(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void trim(char* s) {
    if (!s) return;
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static int bool_value(const char* s) {
    if (!s) return 0;
    return !strcasecmp(s, "1") || !strcasecmp(s, "true") ||
           !strcasecmp(s, "yes") || !strcasecmp(s, "on");
}

static void hub_config_defaults(cove_hub_config_t* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    safe_copy(cfg->message_base_path, sizeof(cfg->message_base_path), "data/mutineer.db");
    safe_copy(cfg->base_id, sizeof(cfg->base_id), "default");
    safe_copy(cfg->auth_db_path, sizeof(cfg->auth_db_path), "data/cove-auth.db");
    safe_copy(cfg->management_db_path, sizeof(cfg->management_db_path), "data/cove-management.db");
    safe_copy(cfg->bind, sizeof(cfg->bind), "0.0.0.0");
    cfg->port = 5150;
    safe_copy(cfg->management_bind, sizeof(cfg->management_bind), "127.0.0.1");
    cfg->management_port = 5151;
    cfg->foreground = 1;
}

static bool hub_config_load(const char* path, cove_hub_config_t* cfg) {
    hub_config_defaults(cfg);
    safe_copy(cfg->source_path, sizeof(cfg->source_path), path);

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* k = line;
        char* v = eq + 1;
        trim(k);
        trim(v);

        if (!strcmp(k, "message_base_path") || !strcmp(k, "message_db_path") || !strcmp(k, "db_path")) {
            safe_copy(cfg->message_base_path, sizeof(cfg->message_base_path), v);
        } else if (!strcmp(k, "base_id") || !strcmp(k, "message_base_id")) {
            safe_copy(cfg->base_id, sizeof(cfg->base_id), v);
        } else if (!strcmp(k, "auth_db_path") || !strcmp(k, "auth_database_path")) {
            safe_copy(cfg->auth_db_path, sizeof(cfg->auth_db_path), v);
        } else if (!strcmp(k, "management_db_path") || !strcmp(k, "mgmt_db_path")) {
            safe_copy(cfg->management_db_path, sizeof(cfg->management_db_path), v);
        } else if (!strcmp(k, "bind") || !strcmp(k, "listen_bind")) {
            safe_copy(cfg->bind, sizeof(cfg->bind), v);
        } else if (!strcmp(k, "port") || !strcmp(k, "listen_port")) {
            cfg->port = atoi(v);
        } else if (!strcmp(k, "management_bind") || !strcmp(k, "mgmt_bind")) {
            safe_copy(cfg->management_bind, sizeof(cfg->management_bind), v);
        } else if (!strcmp(k, "management_port") || !strcmp(k, "mgmt_port")) {
            cfg->management_port = atoi(v);
        } else if (!strcmp(k, "management_token") || !strcmp(k, "api_token")) {
            safe_copy(cfg->management_token, sizeof(cfg->management_token), v);
        } else if (!strcmp(k, "foreground")) {
            cfg->foreground = bool_value(v);
        }
    }
    fclose(f);

    return cfg->message_base_path[0] && cfg->management_db_path[0] &&
           cfg->port > 0 && cfg->management_port > 0;
}

static bool auth_init_schema(void) {
    if (!g_auth_db) return false;
    if (!db_exec(g_auth_db,
        "CREATE TABLE IF NOT EXISTS cove_auth_tokens ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE,"
        "token TEXT NOT NULL UNIQUE,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "last_used_at TEXT);"
        "CREATE TABLE IF NOT EXISTS cove_node_credentials ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "node_addr TEXT NOT NULL UNIQUE,"
        "token TEXT NOT NULL,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "last_used_at TEXT);")) {
        return false;
    }
    if (g_hub_config.management_token[0]) {
        const DbBind binds[] = {
            DB_BIND_TEXT_VAL("bootstrap"),
            DB_BIND_TEXT_VAL(g_hub_config.management_token)
        };
        return db_exec_prepared(g_auth_db,
            "INSERT INTO cove_auth_tokens(name, token, enabled) VALUES (?, ?, 1) "
            "ON CONFLICT(name) DO UPDATE SET token=excluded.token, enabled=1",
            binds, 2);
    }
    return true;
}

static void mgmt_event(const char* actor, const char* action, const char* subject, const char* details) {
    if (!g_mgmt_db) return;
    const DbBind binds[] = {
        DB_BIND_TEXT_VAL(actor ? actor : "coved"),
        DB_BIND_TEXT_VAL(action ? action : "event"),
        DB_BIND_TEXT_VAL(subject ? subject : ""),
        DB_BIND_TEXT_VAL(details ? details : "")
    };
    db_exec_prepared(g_mgmt_db,
        "INSERT INTO cove_management_events(actor, action, subject, details) VALUES (?, ?, ?, ?)",
        binds, 4);
}

static bool mgmt_init_schema(void) {
    if (!g_mgmt_db) return false;
    return db_exec(g_mgmt_db,
        "CREATE TABLE IF NOT EXISTS cove_management_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "event_time TEXT NOT NULL DEFAULT (datetime('now')),"
        "actor TEXT NOT NULL,"
        "action TEXT NOT NULL,"
        "subject TEXT,"
        "details TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_cove_management_events_time "
        "ON cove_management_events(event_time);"
        "CREATE TABLE IF NOT EXISTS cove_hub_connections ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "remote_addr TEXT NOT NULL,"
        "remote_port INTEGER NOT NULL,"
        "accepted_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "status TEXT NOT NULL DEFAULT 'accepted',"
        "details TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_cove_hub_connections_time "
        "ON cove_hub_connections(accepted_at);"
        "CREATE TABLE IF NOT EXISTS cove_managed_nodes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "plank_peer_id INTEGER,"
        "plank_link_id INTEGER,"
        "node_addr TEXT NOT NULL UNIQUE,"
        "node_name TEXT,"
        "network_name TEXT,"
        "remote_host TEXT,"
        "remote_port INTEGER,"
        "status TEXT NOT NULL DEFAULT 'active',"
        "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "notes TEXT);");
}

/* ============================================================================
 * HUB LISTENERS AND MANAGEMENT API
 * ============================================================================ */

static bool setup_ipv4_listener(const char* bind_addr, int port, int* fd_out, const char* component) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        plank_log(PLANK_LOG_ERROR, component, "socket failed: %s", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (!bind_addr || !bind_addr[0] || !strcmp(bind_addr, "0.0.0.0")) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
        plank_log(PLANK_LOG_ERROR, component, "invalid bind address: %s", bind_addr);
        close(fd);
        return false;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        plank_log(PLANK_LOG_ERROR, component, "bind %s:%d failed: %s",
                  bind_addr ? bind_addr : "0.0.0.0", port, strerror(errno));
        close(fd);
        return false;
    }

    if (listen(fd, 32) < 0) {
        plank_log(PLANK_LOG_ERROR, component, "listen failed: %s", strerror(errno));
        close(fd);
        return false;
    }

    *fd_out = fd;
    plank_log(PLANK_LOG_INFO, component, "listening on %s:%d",
              bind_addr ? bind_addr : "0.0.0.0", port);
    return true;
}

static void json_escape(FILE* out, const char* s) {
    fputc('"', out);
    for (const unsigned char* p = (const unsigned char*)(s ? s : ""); *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', out);
            fputc(*p, out);
        } else if (*p == '\n') {
            fputs("\\n", out);
        } else if (*p == '\r') {
            fputs("\\r", out);
        } else if (*p == '\t') {
            fputs("\\t", out);
        } else if (*p < 0x20) {
            fprintf(out, "\\u%04x", *p);
        } else {
            fputc(*p, out);
        }
    }
    fputc('"', out);
}

typedef struct {
    FILE* out;
    int count;
} json_rows_t;

#ifdef HAVE_SQLITE
static bool node_json_row(void* row, void* ctx) {
    sqlite3_stmt* st = (sqlite3_stmt*)row;
    json_rows_t* rows = (json_rows_t*)ctx;
    if (rows->count++ > 0) fputc(',', rows->out);
    fprintf(rows->out, "{\"id\":%d,\"node_addr\":", sqlite3_column_int(st, 0));
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 1));
    fputs(",\"node_name\":", rows->out);
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 2));
    fputs(",\"network_name\":", rows->out);
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 3));
    fputs(",\"remote_host\":", rows->out);
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 4));
    fprintf(rows->out, ",\"remote_port\":%d,\"status\":", sqlite3_column_int(st, 5));
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 6));
    fputc('}', rows->out);
    return true;
}

static bool area_json_row(void* row, void* ctx) {
    sqlite3_stmt* st = (sqlite3_stmt*)row;
    json_rows_t* rows = (json_rows_t*)ctx;
    if (rows->count++ > 0) fputc(',', rows->out);
    fprintf(rows->out, "{\"id\":%d,\"area_addr\":", sqlite3_column_int(st, 0));
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 1));
    fputs(",\"title\":", rows->out);
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 2));
    fprintf(rows->out, ",\"status\":%d}", sqlite3_column_int(st, 3));
    return true;
}

static bool event_json_row(void* row, void* ctx) {
    sqlite3_stmt* st = (sqlite3_stmt*)row;
    json_rows_t* rows = (json_rows_t*)ctx;
    if (rows->count++ > 0) fputc(',', rows->out);
    fprintf(rows->out, "{\"id\":%d,\"event_time\":", sqlite3_column_int(st, 0));
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 1));
    fputs(",\"actor\":", rows->out);
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 2));
    fputs(",\"action\":", rows->out);
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 3));
    fputs(",\"subject\":", rows->out);
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 4));
    fputs(",\"details\":", rows->out);
    json_escape(rows->out, (const char*)sqlite3_column_text(st, 5));
    fputc('}', rows->out);
    return true;
}
#endif

static void http_send(int fd, int status, const char* reason, const char* body) {
    char header[256];
    size_t len = strlen(body ? body : "");
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        status, reason, len);
    if (n > 0 && write(fd, header, (size_t)n) < 0) return;
    if (len > 0 && write(fd, body, len) < 0) return;
}

static bool extract_header_token(const char* req, char* out, size_t out_size) {
    if (!req || !out || out_size == 0) return false;
    out[0] = '\0';
    const char* p = strcasestr(req, "\nAuthorization:");
    if (p) {
        p += 15;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncasecmp(p, "Bearer ", 7)) {
            p += 7;
            size_t i = 0;
            while (*p && *p != '\r' && *p != '\n' && i + 1 < out_size) out[i++] = *p++;
            out[i] = '\0';
            return out[0] != '\0';
        }
    }
    p = strcasestr(req, "\nX-COVE-Token:");
    if (p) {
        p += 14;
        while (*p == ' ' || *p == '\t') p++;
        size_t i = 0;
        while (*p && *p != '\r' && *p != '\n' && i + 1 < out_size) out[i++] = *p++;
        out[i] = '\0';
        return out[0] != '\0';
    }
    return false;
}

static bool management_authorized(const char* req) {
    char token[256];
    if (!g_auth_db || !extract_header_token(req, token, sizeof(token))) {
        mgmt_event("api", "auth.failed", "management", "missing token");
        return false;
    }
    const DbBind binds[] = { DB_BIND_TEXT_VAL(token) };
    int ok = db_query_int_prepared(g_auth_db,
        "SELECT id FROM cove_auth_tokens WHERE token = ? AND enabled != 0 LIMIT 1",
        binds, 1, 0);
    if (ok <= 0) {
        mgmt_event("api", "auth.failed", "management", "bad token");
        return false;
    }
    db_exec_prepared(g_auth_db,
        "UPDATE cove_auth_tokens SET last_used_at = datetime('now') WHERE id = ?",
        (DbBind[]){ DB_BIND_INT_VAL(ok) }, 1);
    return true;
}

static bool path_id(const char* path, const char* prefix, int* out_id) {
    size_t n = strlen(prefix);
    if (strncmp(path, prefix, n) != 0) return false;
    const char* p = path + n;
    if (!isdigit((unsigned char)*p)) return false;
    *out_id = atoi(p);
    return *out_id > 0;
}

static const char* find_param(const char* body, const char* key, char* out, size_t out_size) {
    if (!body || !key || !out || out_size == 0) return NULL;
    size_t key_len = strlen(key);
    const char* p = body;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == body || p[-1] == '&' || p[-1] == '?' || p[-1] == '"' || p[-1] == '{' || isspace((unsigned char)p[-1])) &&
            (p[key_len] == '=' || p[key_len] == ':' || p[key_len] == '"')) {
            p += key_len;
            while (*p == '"' || *p == ':' || *p == '=' || isspace((unsigned char)*p)) p++;
            size_t i = 0;
            while (*p && *p != '&' && *p != '"' && *p != ',' && *p != '}' && i + 1 < out_size) {
                out[i++] = (*p == '+') ? ' ' : *p;
                p++;
            }
            out[i] = '\0';
            return out;
        }
        p += key_len;
    }
    out[0] = '\0';
    return NULL;
}

static bool add_managed_node(const char* body, char* err, size_t err_size) {
    char node_addr[128], node_name[64], network_name[64], remote_host[128], port_buf[32], notes[256];
    find_param(body, "node_addr", node_addr, sizeof(node_addr));
    find_param(body, "node_name", node_name, sizeof(node_name));
    find_param(body, "network_name", network_name, sizeof(network_name));
    find_param(body, "remote_host", remote_host, sizeof(remote_host));
    find_param(body, "remote_port", port_buf, sizeof(port_buf));
    find_param(body, "notes", notes, sizeof(notes));
    int remote_port = port_buf[0] ? atoi(port_buf) : g_hub_config.port;

    if (!node_addr[0] || !node_name[0] || !network_name[0] || !remote_host[0] || remote_port <= 0) {
        snprintf(err, err_size, "node_addr, node_name, network_name, remote_host, and remote_port are required");
        return false;
    }

    const DbBind peer_binds[] = {
        DB_BIND_TEXT_VAL(node_name),
        DB_BIND_TEXT_VAL(network_name),
        DB_BIND_TEXT_VAL(node_addr),
        DB_BIND_TEXT_VAL(notes)
    };
    if (!db_exec_prepared(g_db,
        "INSERT OR IGNORE INTO plank_peers "
        "(node_id, node_name, network_name, node_addr, signing_key_pub, trust_level, status, notes) "
        "VALUES (randomblob(16), ?, ?, ?, randomblob(32), 1, 1, ?)",
        peer_binds, 4)) {
        snprintf(err, err_size, "failed to insert peer: %s", db_last_error(g_db));
        return false;
    }

    const DbBind peer_id_bind[] = { DB_BIND_TEXT_VAL(node_addr) };
    int peer_id = db_query_int_prepared(g_db, "SELECT id FROM plank_peers WHERE node_addr = ?", peer_id_bind, 1, 0);
    if (peer_id <= 0) {
        snprintf(err, err_size, "failed to resolve peer id");
        return false;
    }

    const DbBind link_id_binds[] = {
        DB_BIND_INT_VAL(peer_id),
        DB_BIND_TEXT_VAL(remote_host),
        DB_BIND_INT_VAL(remote_port)
    };
    int link_id = db_query_int_prepared(g_db,
        "SELECT id FROM plank_links WHERE peer_id = ? AND remote_host = ? AND remote_port = ? ORDER BY id DESC LIMIT 1",
        link_id_binds, 3, 0);

    if (link_id <= 0) {
        const DbBind link_binds[] = {
            DB_BIND_TEXT_VAL(node_name),
            DB_BIND_INT_VAL(peer_id),
            DB_BIND_TEXT_VAL(remote_host),
            DB_BIND_INT_VAL(remote_port)
        };
        if (!db_exec_prepared(g_db,
            "INSERT INTO plank_links "
            "(link_id, link_name, peer_id, remote_host, remote_port, direction, enabled, state) "
            "VALUES (randomblob(16), ?, ?, ?, ?, 3, 1, 0)",
            link_binds, 4)) {
            snprintf(err, err_size, "failed to insert link: %s", db_last_error(g_db));
            return false;
        }
        link_id = db_query_int_prepared(g_db,
            "SELECT id FROM plank_links WHERE peer_id = ? AND remote_host = ? AND remote_port = ? ORDER BY id DESC LIMIT 1",
            link_id_binds, 3, 0);
    }

    const DbBind mgmt_binds[] = {
        DB_BIND_INT_VAL(peer_id),
        DB_BIND_INT_VAL(link_id),
        DB_BIND_TEXT_VAL(node_addr),
        DB_BIND_TEXT_VAL(node_name),
        DB_BIND_TEXT_VAL(network_name),
        DB_BIND_TEXT_VAL(remote_host),
        DB_BIND_INT_VAL(remote_port),
        DB_BIND_TEXT_VAL(notes)
    };
    if (!db_exec_prepared(g_mgmt_db,
        "INSERT INTO cove_managed_nodes "
        "(plank_peer_id, plank_link_id, node_addr, node_name, network_name, remote_host, remote_port, notes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(node_addr) DO UPDATE SET "
        "plank_peer_id=excluded.plank_peer_id, plank_link_id=excluded.plank_link_id, "
        "node_name=excluded.node_name, network_name=excluded.network_name, "
        "remote_host=excluded.remote_host, remote_port=excluded.remote_port, "
        "notes=excluded.notes, updated_at=datetime('now')",
        mgmt_binds, 8)) {
        snprintf(err, err_size, "failed to record managed node: %s", db_last_error(g_mgmt_db));
        return false;
    }

    mgmt_event("api", "node.add", node_addr, remote_host);
    return true;
}

static bool set_managed_node_status(int id, const char* status, char* err, size_t err_size) {
    const DbBind binds[] = {
        DB_BIND_TEXT_VAL(status),
        DB_BIND_INT_VAL(id)
    };
    if (!db_exec_prepared(g_mgmt_db,
        "UPDATE cove_managed_nodes SET status = ?, updated_at = datetime('now') WHERE id = ?",
        binds, 2)) {
        snprintf(err, err_size, "failed to update node: %s", db_last_error(g_mgmt_db));
        return false;
    }
    if (db_changes(g_mgmt_db) <= 0) {
        snprintf(err, err_size, "node not found");
        return false;
    }
    mgmt_event("api", !strcmp(status, "active") ? "node.enable" : "node.disable", "", status);
    return true;
}

static bool delete_managed_node(int id, char* err, size_t err_size) {
    const DbBind binds[] = { DB_BIND_INT_VAL(id) };
    if (!db_exec_prepared(g_mgmt_db, "DELETE FROM cove_managed_nodes WHERE id = ?", binds, 1)) {
        snprintf(err, err_size, "failed to delete node: %s", db_last_error(g_mgmt_db));
        return false;
    }
    mgmt_event("api", "node.delete", "", "");
    return true;
}

static void handle_management_client(int fd) {
    char req[HUB_HTTP_MAX + 1];
    ssize_t n = read(fd, req, HUB_HTTP_MAX);
    if (n <= 0) return;
    req[n] = '\0';

    char method[16] = {0};
    char path[256] = {0};
    sscanf(req, "%15s %255s", method, path);
    char* body = strstr(req, "\r\n\r\n");
    body = body ? body + 4 : (char*)"";

    char response[32768];
    FILE* out = fmemopen(response, sizeof(response), "w");
    if (!out) {
        http_send(fd, 500, "Internal Server Error", "{\"error\":\"response allocation failed\"}");
        return;
    }

    bool mutating = !strcmp(method, "POST") || !strcmp(method, "DELETE");
    if (mutating && !management_authorized(req)) {
        fclose(out);
        http_send(fd, 401, "Unauthorized", "{\"error\":\"unauthorized\"}");
        return;
    }

    if (!strcmp(method, "GET") && !strcmp(path, "/health")) {
        fprintf(out, "{\"ok\":true,\"mode\":\"hub\",\"base_id\":");
        json_escape(out, g_hub_config.base_id);
        fprintf(out, ",\"listen_port\":%d,\"management_bind\":\"127.0.0.1\",\"management_port\":%d}",
                g_hub_config.port, g_hub_config.management_port);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/config")) {
        fprintf(out, "{\"message_base_path\":");
        json_escape(out, g_hub_config.message_base_path);
        fputs(",\"base_id\":", out);
        json_escape(out, g_hub_config.base_id);
        fputs(",\"auth_db_path\":\"<redacted>\"", out);
        fputs(",\"management_db_path\":", out);
        json_escape(out, g_hub_config.management_db_path);
        fprintf(out, ",\"port\":%d,\"management_port\":%d}", g_hub_config.port, g_hub_config.management_port);
    } else if (!strcmp(method, "GET") && (!strcmp(path, "/nodes") || !strncmp(path, "/nodes?", 7))) {
        fputs("{\"nodes\":[", out);
#ifdef HAVE_SQLITE
        json_rows_t rows = {out, 0};
        db_query(g_mgmt_db,
            "SELECT id,node_addr,node_name,network_name,remote_host,remote_port,status "
            "FROM cove_managed_nodes ORDER BY node_addr",
            node_json_row, &rows);
#endif
        fputs("]}", out);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/nodes")) {
        char err[256];
        if (add_managed_node(body, err, sizeof(err))) {
            fputs("{\"ok\":true}", out);
        } else {
            fclose(out);
            snprintf(response, sizeof(response), "{\"error\":");
            size_t off = strlen(response);
            FILE* err_out = fmemopen(response + off, sizeof(response) - off, "w");
            json_escape(err_out, err);
            fclose(err_out);
            strncat(response, "}", sizeof(response) - strlen(response) - 1);
            http_send(fd, 400, "Bad Request", response);
            return;
        }
    } else if (!strcmp(method, "GET") && !strncmp(path, "/nodes/", 7)) {
        int id = 0;
        if (!path_id(path, "/nodes/", &id)) {
            fclose(out);
            http_send(fd, 400, "Bad Request", "{\"error\":\"invalid node id\"}");
            return;
        }
        fputs("{\"nodes\":[", out);
#ifdef HAVE_SQLITE
        json_rows_t rows = {out, 0};
        char sql[256];
        snprintf(sql, sizeof(sql),
            "SELECT id,node_addr,node_name,network_name,remote_host,remote_port,status "
            "FROM cove_managed_nodes WHERE id = %d", id);
        db_query(g_mgmt_db, sql, node_json_row, &rows);
#endif
        fputs("]}", out);
    } else if (!strcmp(method, "POST") && !strncmp(path, "/nodes/", 7) && strstr(path, "/disable")) {
        int id = 0;
        char err[256];
        if (!path_id(path, "/nodes/", &id) || !set_managed_node_status(id, "disabled", err, sizeof(err))) {
            fclose(out);
            http_send(fd, 400, "Bad Request", "{\"error\":\"node update failed\"}");
            return;
        }
        fputs("{\"ok\":true}", out);
    } else if (!strcmp(method, "POST") && !strncmp(path, "/nodes/", 7) && strstr(path, "/enable")) {
        int id = 0;
        char err[256];
        if (!path_id(path, "/nodes/", &id) || !set_managed_node_status(id, "active", err, sizeof(err))) {
            fclose(out);
            http_send(fd, 400, "Bad Request", "{\"error\":\"node update failed\"}");
            return;
        }
        fputs("{\"ok\":true}", out);
    } else if (!strcmp(method, "DELETE") && !strncmp(path, "/nodes/", 7)) {
        int id = 0;
        char err[256];
        if (!path_id(path, "/nodes/", &id) || !delete_managed_node(id, err, sizeof(err))) {
            fclose(out);
            http_send(fd, 400, "Bad Request", "{\"error\":\"node delete failed\"}");
            return;
        }
        fputs("{\"ok\":true}", out);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/links/health")) {
        fputs("{\"links\":[", out);
#ifdef HAVE_SQLITE
        json_rows_t rows = {out, 0};
        db_query(g_mgmt_db,
            "SELECT id,node_addr,node_name,network_name,remote_host,remote_port,status "
            "FROM cove_managed_nodes ORDER BY updated_at DESC",
            node_json_row, &rows);
#endif
        fputs("]}", out);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/connections")) {
        fputs("{\"connections\":[", out);
#ifdef HAVE_SQLITE
        json_rows_t rows = {out, 0};
        db_query(g_mgmt_db,
            "SELECT id,accepted_at,'network' AS actor,status AS action,remote_addr AS subject,details "
            "FROM cove_hub_connections ORDER BY id DESC LIMIT 100",
            event_json_row, &rows);
#endif
        fputs("]}", out);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/queue")) {
        fprintf(out, "{\"pending\":%d,\"deadletters\":%d}",
            db_query_int(g_db, "SELECT COUNT(*) FROM plank_outbound_queue", 0),
            db_query_int(g_db, "SELECT COUNT(*) FROM plank_deadletters WHERE state = 0", 0));
    } else if (!strcmp(method, "POST") && !strncmp(path, "/deadletters/", 13) && strstr(path, "/retry")) {
        int id = 0;
        if (!path_id(path, "/deadletters/", &id) || !plank_router_requeue_deadletter(g_router, id)) {
            fclose(out);
            http_send(fd, 400, "Bad Request", "{\"error\":\"retry failed\"}");
            return;
        }
        mgmt_event("api", "deadletter.retry", path, "");
        fputs("{\"ok\":true}", out);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/areas")) {
        fputs("{\"areas\":[", out);
#ifdef HAVE_SQLITE
        json_rows_t rows = {out, 0};
        db_query(g_db, "SELECT id,area_addr,title,status FROM plank_areas ORDER BY area_addr", area_json_row, &rows);
#endif
        fputs("]}", out);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/events")) {
        fputs("{\"events\":[", out);
#ifdef HAVE_SQLITE
        json_rows_t rows = {out, 0};
        db_query(g_mgmt_db,
            "SELECT id,event_time,actor,action,subject,details "
            "FROM cove_management_events ORDER BY id DESC LIMIT 100",
            event_json_row, &rows);
#endif
        fputs("]}", out);
    } else {
        fclose(out);
        http_send(fd, 404, "Not Found", "{\"error\":\"not found\"}");
        return;
    }

    fclose(out);
    http_send(fd, 200, "OK", response);
}

typedef struct {
    int fd;
    char remote[64];
    int port;
} hub_client_t;

static void* hub_client_thread(void* arg) {
    hub_client_t* hc = (hub_client_t*)arg;
    plank_link_callbacks_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    plank_link_session_t* session = plank_link_session_create_inbound(g_store, hc->fd, &callbacks);
    if (!session) {
        close(hc->fd);
        mgmt_event("network", "connection.error", hc->remote, "failed to create PLANK inbound session");
        free(hc);
        return NULL;
    }
    mgmt_event("network", "connection.session.start", hc->remote, "PLANK inbound session started");
    time_t deadline = time(NULL) + 300;
    while (g_running && time(NULL) < deadline && plank_link_session_state(session) != PLANK_LINK_IDLE) {
        if (!plank_link_session_read_frame(session)) break;
    }
    mgmt_event("network", "connection.session.end", hc->remote, plank_link_session_error(session));
    plank_link_session_free(session);
    free(hc);
    return NULL;
}

static void accept_hub_connection(void) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int fd = accept(g_hub_listen_fd, (struct sockaddr*)&addr, &len);
    if (fd < 0) return;

    char remote[64];
    inet_ntop(AF_INET, &addr.sin_addr, remote, sizeof(remote));
    const DbBind binds[] = {
        DB_BIND_TEXT_VAL(remote),
        DB_BIND_INT_VAL(ntohs(addr.sin_port)),
        DB_BIND_TEXT_VAL("PLANK inbound session accepted")
    };
    db_exec_prepared(g_mgmt_db,
        "INSERT INTO cove_hub_connections(remote_addr, remote_port, details) VALUES (?, ?, ?)",
        binds, 3);
    mgmt_event("network", "connection.accept", remote, "hub listener accepted connection");
    hub_client_t* hc = calloc(1, sizeof(*hc));
    if (!hc) {
        close(fd);
        return;
    }
    hc->fd = fd;
    hc->port = ntohs(addr.sin_port);
    safe_copy(hc->remote, sizeof(hc->remote), remote);
    pthread_t th;
    if (pthread_create(&th, NULL, hub_client_thread, hc) == 0) {
        pthread_detach(th);
    } else {
        close(fd);
        free(hc);
    }
}

static void accept_management_connection(void) {
    int fd = accept(g_mgmt_listen_fd, NULL, NULL);
    if (fd < 0) return;
    handle_management_client(fd);
    close(fd);
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

    /*
     * The listeners are created before this loop.  Treat daemon entry as the
     * initial successful processing/maintenance point so an open listener is
     * immediately responsive.  Starting these at zero made the first loop run
     * all periodic database and health work before accepting a queued client;
     * callers could connect successfully and then time out waiting for HTTP.
     */
    time_t last_process = time(NULL);
    time_t last_maintenance = last_process;
    
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
        
        if (g_mode == COVE_MODE_HUB && (g_hub_listen_fd >= 0 || g_mgmt_listen_fd >= 0)) {
            struct pollfd pfds[2];
            int pfd_count = 0;
            if (g_hub_listen_fd >= 0) {
                pfds[pfd_count].fd = g_hub_listen_fd;
                pfds[pfd_count].events = POLLIN;
                pfd_count++;
            }
            if (g_mgmt_listen_fd >= 0) {
                pfds[pfd_count].fd = g_mgmt_listen_fd;
                pfds[pfd_count].events = POLLIN;
                pfd_count++;
            }
            int ret = poll(pfds, pfd_count, 100);
            if (ret > 0) {
                int idx = 0;
                if (g_hub_listen_fd >= 0) {
                    if (pfds[idx].revents & POLLIN) accept_hub_connection();
                    idx++;
                }
                if (g_mgmt_listen_fd >= 0) {
                    if (pfds[idx].revents & POLLIN) accept_management_connection();
                }
            }
        } else {
            /* Sleep briefly */
            usleep(100000);  /* 100ms */
        }
    }
    
    plank_log(PLANK_LOG_INFO, "coved", "Shutting down");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "       %s -mode=hub CONFIGFILE\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -mode, --mode MODE   Run mode: service or hub\n");
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
    const char* mode = "service";
    bool foreground = false;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"mode",       required_argument, 0, 'm'},
        {"config",     required_argument, 0, 'c'},
        {"database",   required_argument, 0, 'd'},
        {"foreground", no_argument,       0, 'f'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long_only(argc, argv, "m:c:d:fvhV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'm':
                mode = optarg;
                break;
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

    if (optind < argc) {
        config_file = argv[optind];
    }

    if (!strcasecmp(mode, "hub")) {
        g_mode = COVE_MODE_HUB;
    } else if (!strcasecmp(mode, "service") || !strcasecmp(mode, "cove")) {
        g_mode = COVE_MODE_SERVICE;
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        print_usage(argv[0]);
        return 1;
    }
    
    /* Initialize PLANK */
    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }
    
    /* Set up logging */
    plank_set_log_callback(log_callback, NULL);
    plank_set_log_level(verbose ? PLANK_LOG_DEBUG : PLANK_LOG_INFO);
    
    if (g_mode == COVE_MODE_HUB) {
        if (!hub_config_load(config_file, &g_hub_config)) {
            fprintf(stderr, "Failed to load COVE hub configuration: %s\n", config_file);
            plank_shutdown();
            return 1;
        }
        db_file = db_file ? db_file : g_hub_config.message_base_path;
        foreground = foreground || g_hub_config.foreground;
    } else {
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
    }
    
    /* Open database */
    g_db = db_open(db_file);
    if (!g_db) {
        fprintf(stderr, "Failed to open database: %s\n", db_file);
        plank_shutdown();
        return 1;
    }
    /* COVE performs short concurrent queue and management transactions. WAL
       avoids writer starvation between the service loop and hub sessions;
       NORMAL retains crash consistency without an fsync for every API field. */
    db_exec(g_db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
    
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

    if (g_mode == COVE_MODE_HUB) {
        if (strcmp(g_hub_config.management_bind, "127.0.0.1") != 0 &&
            strcmp(g_hub_config.management_bind, "localhost") != 0) {
            plank_log(PLANK_LOG_WARN, "cove-mgmt",
                      "management API binding to non-loopback address %s; protect this port",
                      g_hub_config.management_bind);
        }
        g_auth_db = db_open(g_hub_config.auth_db_path);
        if (!g_auth_db) {
            fprintf(stderr, "Failed to open COVE auth database: %s (%s)\n",
                    g_hub_config.auth_db_path, db_last_error(NULL));
            plank_policy_free(g_policy);
            plank_router_free(g_router);
            plank_store_close(g_store);
            db_close(g_db);
            plank_shutdown();
            return 1;
        }
        db_exec(g_auth_db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
        if (!auth_init_schema()) {
            fprintf(stderr, "Failed to initialize COVE auth database: %s\n",
                    db_last_error(g_auth_db));
            db_close(g_auth_db);
            plank_policy_free(g_policy);
            plank_router_free(g_router);
            plank_store_close(g_store);
            db_close(g_db);
            plank_shutdown();
            return 1;
        }
        g_mgmt_db = db_open(g_hub_config.management_db_path);
        if (!g_mgmt_db) {
            fprintf(stderr, "Failed to open COVE management database: %s (%s)\n",
                    g_hub_config.management_db_path, db_last_error(NULL));
            db_close(g_auth_db);
            plank_policy_free(g_policy);
            plank_router_free(g_router);
            plank_store_close(g_store);
            db_close(g_db);
            plank_shutdown();
            return 1;
        }
        db_exec(g_mgmt_db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
        if (!mgmt_init_schema()) {
            fprintf(stderr, "Failed to initialize COVE management database: %s\n",
                    db_last_error(g_mgmt_db));
            db_close(g_mgmt_db);
            db_close(g_auth_db);
            plank_policy_free(g_policy);
            plank_router_free(g_router);
            plank_store_close(g_store);
            db_close(g_db);
            plank_shutdown();
            return 1;
        }
        if (!setup_ipv4_listener(g_hub_config.bind, g_hub_config.port, &g_hub_listen_fd, "cove-hub") ||
            !setup_ipv4_listener(g_hub_config.management_bind, g_hub_config.management_port, &g_mgmt_listen_fd, "cove-mgmt")) {
            if (g_hub_listen_fd >= 0) close(g_hub_listen_fd);
            if (g_mgmt_listen_fd >= 0) close(g_mgmt_listen_fd);
            db_close(g_mgmt_db);
            db_close(g_auth_db);
            plank_policy_free(g_policy);
            plank_router_free(g_router);
            plank_store_close(g_store);
            db_close(g_db);
            plank_shutdown();
            return 1;
        }
        mgmt_event("coved", "hub.start", g_hub_config.base_id, "COVE hub mode initialized");
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
        
        if (setsid() < 0) {
            fprintf(stderr, "setsid failed\n");
            return 1;
        }
        if (!freopen("/dev/null", "r", stdin) ||
            !freopen("/var/log/coved.log", "a", stdout) ||
            !freopen("/var/log/coved.log", "a", stderr)) {
            return 1;
        }
    }
    
    /* Set up signals */
    setup_signals();
    
    plank_log(PLANK_LOG_INFO, "coved", "COVE %s starting (version %s)",
              g_mode == COVE_MODE_HUB ? "hub" : "service", COVED_VERSION);
    
    /* Run main loop */
    run_daemon();
    
    /* Cleanup */
    if (g_hub_listen_fd >= 0) close(g_hub_listen_fd);
    if (g_mgmt_listen_fd >= 0) close(g_mgmt_listen_fd);
    if (g_mgmt_db) {
        mgmt_event("coved", "hub.stop", g_hub_config.base_id, "COVE hub mode stopped");
        db_close(g_mgmt_db);
    }
    if (g_auth_db) db_close(g_auth_db);
    plank_policy_free(g_policy);
    plank_router_free(g_router);
    plank_store_close(g_store);
    db_close(g_db);
    plank_shutdown();
    
    return 0;
}
