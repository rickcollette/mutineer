/*
 * plankctl - PLANK Administrative Control Tool
 * Inspect and manage PLANK/COVE state, links, queues, and operations
 */

#include "plank/plank.h"
#include "bbs_db.h"
#include "bbs_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#ifdef HAVE_SQLITE
#include <sqlite3.h>
#endif

#define PLANKCTL_VERSION "1.0.0"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static BbsDb *g_db = NULL;
static plank_store_t *g_store = NULL;

/* ============================================================================
 * OUTPUT HELPERS
 * ============================================================================ */

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        printf("%02x", data[i]);
    }
}

static int db_count(BbsDb *db, const char *sql)
{
#ifdef HAVE_SQLITE
    return db_query_int(db, sql, 0);
#else
    (void)db;
    (void)sql;
    return 0;
#endif
}

#ifdef HAVE_SQLITE
typedef struct
{
    int rows;
} query_count_t;

static void count_row(void *ctx)
{
    if (ctx)
    {
        ((query_count_t *)ctx)->rows++;
    }
}

static const char *direction_name(int direction)
{
    switch (direction)
    {
    case PLANK_DIR_INBOUND:
        return "inbound";
    case PLANK_DIR_OUTBOUND:
        return "outbound";
    case PLANK_DIR_BOTH:
        return "both";
    default:
        return "unknown";
    }
}

static const char *link_state_name(int state)
{
    switch (state)
    {
    case PLANK_LINK_DISABLED:
        return "disabled";
    case PLANK_LINK_IDLE:
        return "idle";
    case PLANK_LINK_CONNECTING:
        return "connecting";
    case PLANK_LINK_TLS_OK:
        return "tls-ok";
    case PLANK_LINK_AUTH_OK:
        return "auth-ok";
    case PLANK_LINK_SYNCING:
        return "syncing";
    case PLANK_LINK_BACKOFF:
        return "backoff";
    case PLANK_LINK_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static bool link_row_cb(void *row, void *ctx)
{
    count_row(ctx);
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    int id = sqlite3_column_int(st, 0);
    const unsigned char *name = sqlite3_column_text(st, 1);
    const unsigned char *peer = sqlite3_column_text(st, 2);
    const unsigned char *host = sqlite3_column_text(st, 3);
    int port = sqlite3_column_int(st, 4);
    int direction = sqlite3_column_int(st, 5);
    int enabled = sqlite3_column_int(st, 6);
    int paused = sqlite3_column_int(st, 7);
    int state = sqlite3_column_int(st, 8);
    int retry_count = sqlite3_column_int(st, 9);
    const unsigned char *last_success = sqlite3_column_text(st, 10);
    const unsigned char *last_error = sqlite3_column_text(st, 11);

    printf("%4d  %-16s peer=%-28s %-22s:%-5d dir=%-8s %s%s state=%-10s retries=%d\n",
           id,
           name ? (const char *)name : "(unknown)",
           peer ? (const char *)peer : "(unknown)",
           host ? (const char *)host : "(unknown)",
           port,
           direction_name(direction),
           enabled ? "enabled" : "disabled",
           paused ? " paused" : "",
           link_state_name(state),
           retry_count);
    printf("      last_success=%s error=%s\n",
           last_success ? (const char *)last_success : "(never)",
           last_error ? (const char *)last_error : "");
    return true;
}

static bool peer_row_cb(void *row, void *ctx)
{
    count_row(ctx);
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    int id = sqlite3_column_int(st, 0);
    const void *node_id = sqlite3_column_blob(st, 1);
    int node_id_len = sqlite3_column_bytes(st, 1);
    const unsigned char *node_addr = sqlite3_column_text(st, 2);
    int trust = sqlite3_column_int(st, 3);
    int status = sqlite3_column_int(st, 4);
    const unsigned char *last_seen = sqlite3_column_text(st, 5);

    printf("%4d  %-36s trust=%d status=%d last_seen=%s node_id=",
           id,
           node_addr ? (const char *)node_addr : "(unknown)",
           trust,
           status,
           last_seen ? (const char *)last_seen : "(never)");
    if (node_id && node_id_len > 0)
    {
        print_hex(node_id, (size_t)node_id_len);
    }
    printf("\n");
    return true;
}

static bool area_row_cb(void *row, void *ctx)
{
    count_row(ctx);
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    int id = sqlite3_column_int(st, 0);
    const unsigned char *addr = sqlite3_column_text(st, 1);
    const unsigned char *title = sqlite3_column_text(st, 2);
    int mode = sqlite3_column_int(st, 3);
    int status = sqlite3_column_int(st, 4);

    printf("%4d  %-40s %-24s mode=%d status=%d\n",
           id,
           addr ? (const char *)addr : "(unknown)",
           title ? (const char *)title : "(untitled)",
           mode,
           status);
    return true;
}

static bool journal_row_cb(void *row, void *ctx)
{
    count_row(ctx);
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    int id = sqlite3_column_int(st, 0);
    const void *object_id = sqlite3_column_blob(st, 1);
    int object_id_len = sqlite3_column_bytes(st, 1);
    int object_class = sqlite3_column_int(st, 2);
    int source_kind = sqlite3_column_int(st, 3);
    int source_link_id = sqlite3_column_int(st, 4);
    int processing_state = sqlite3_column_int(st, 5);

    printf("%4d  object_class=%d source=%d link=%d state=%d id=", id,
           object_class, source_kind, source_link_id, processing_state);
    if (object_id && object_id_len > 0)
    {
        print_hex(object_id, (size_t)object_id_len);
    }
    printf("\n");
    return true;
}

static bool outbound_row_cb(void *row, void *ctx)
{
    count_row(ctx);
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    int id = sqlite3_column_int(st, 0);
    int link_id = sqlite3_column_int(st, 1);
    const unsigned char *link_name = sqlite3_column_text(st, 2);
    const void *object_id = sqlite3_column_blob(st, 3);
    int object_id_len = sqlite3_column_bytes(st, 3);
    int priority = sqlite3_column_int(st, 4);
    int status = sqlite3_column_int(st, 5);
    const void *bundle_id = sqlite3_column_blob(st, 6);
    int bundle_id_len = sqlite3_column_bytes(st, 6);
    const unsigned char *queued_at = sqlite3_column_text(st, 7);

    printf("%4d  link=%d/%s priority=%d status=%d queued=%s object_id=",
           id,
           link_id,
           link_name ? (const char *)link_name : "(unknown)",
           priority,
           status,
           queued_at ? (const char *)queued_at : "(unknown)");
    if (object_id && object_id_len > 0)
    {
        print_hex(object_id, (size_t)object_id_len);
    }
    if (bundle_id && bundle_id_len > 0)
    {
        printf(" bundle_id=");
        print_hex(bundle_id, (size_t)bundle_id_len);
    }
    printf("\n");
    return true;
}

static bool deadletter_row_cb(void *row, void *ctx)
{
    count_row(ctx);
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    int id = sqlite3_column_int(st, 0);
    int link_id = sqlite3_column_int(st, 1);
    const unsigned char *node_addr = sqlite3_column_text(st, 2);
    const unsigned char *object_ids = sqlite3_column_text(st, 3);
    const void *bundle_id = sqlite3_column_blob(st, 4);
    int bundle_id_len = sqlite3_column_bytes(st, 4);
    int error_code = sqlite3_column_int(st, 5);
    const unsigned char *error_text = sqlite3_column_text(st, 6);
    int retry_count = sqlite3_column_int(st, 7);
    int state = sqlite3_column_int(st, 8);
    const unsigned char *last_failure = sqlite3_column_text(st, 9);

    printf("%4d  link=%d node=%s state=%d retries=%d last_failure=%s error=%d text=%s\n",
           id,
           link_id,
           node_addr ? (const char *)node_addr : "(none)",
           state,
           retry_count,
           last_failure ? (const char *)last_failure : "(unknown)",
           error_code,
           error_text ? (const char *)error_text : "");
    printf("      objects=%s", object_ids ? (const char *)object_ids : "[]");
    printf(" bundle_id=");
    if (bundle_id && bundle_id_len > 0)
    {
        print_hex(bundle_id, (size_t)bundle_id_len);
    }
    printf("\n");
    return true;
}

static bool quarantine_row_cb(void *row, void *ctx)
{
    count_row(ctx);
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    int id = sqlite3_column_int(st, 0);
    const void *object_id = sqlite3_column_blob(st, 1);
    int object_id_len = sqlite3_column_bytes(st, 1);
    int object_class = sqlite3_column_int(st, 2);
    int source_link_id = sqlite3_column_int(st, 3);
    const unsigned char *source_node_addr = sqlite3_column_text(st, 4);
    int reason = sqlite3_column_int(st, 5);
    const unsigned char *reason_text = sqlite3_column_text(st, 6);

    printf("%4d  class=%d source_link=%d node=%s reason=%d text=%s\n",
           id,
           object_class,
           source_link_id,
           source_node_addr ? (const char *)source_node_addr : "(none)",
           reason,
           reason_text ? (const char *)reason_text : "");
    printf("      object_id=");
    if (object_id && object_id_len > 0)
    {
        print_hex(object_id, (size_t)object_id_len);
    }
    printf("\n");
    return true;
}

static bool audit_row_cb(void *row, void *ctx)
{
    count_row(ctx);
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    int id = sqlite3_column_int(st, 0);
    const unsigned char *event_type = sqlite3_column_text(st, 1);
    int link_id = sqlite3_column_int(st, 2);
    const unsigned char *node_addr = sqlite3_column_text(st, 3);
    const unsigned char *user_handle = sqlite3_column_text(st, 4);
    const unsigned char *details = sqlite3_column_text(st, 5);
    const void *object_id = sqlite3_column_blob(st, 6);
    int object_id_len = sqlite3_column_bytes(st, 6);

    printf("%4d  event=%s link=%d node=%s user=%s\n",
           id,
           event_type ? (const char *)event_type : "(unknown)",
           link_id,
           node_addr ? (const char *)node_addr : "(none)",
           user_handle ? (const char *)user_handle : "(none)");
    if (details)
    {
        printf("      details=%s\n", details);
    }
    if (object_id && object_id_len > 0)
    {
        printf("      object_id=");
        print_hex(object_id, (size_t)object_id_len);
        printf("\n");
    }
    return true;
}
#endif

/* ============================================================================
 * COMMANDS
 * ============================================================================ */

static int cmd_status(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("PLANK Status\n");
    printf("============\n\n");

    /* Get identity */
    plank_node_identity_t identity;
    if (plank_store_get_identity(g_store, &identity))
    {
        printf("Node Identity:\n");
        printf("  Name:     %s\n", identity.node_name);
        printf("  Network:  %s\n", identity.network_name);
        printf("  Address:  %s\n", identity.node_addr);
        printf("  Node ID:  ");
        print_hex(identity.node_id, PLANK_NODE_ID_SIZE);
        printf("\n");
        printf("  COVE:     %s\n", identity.is_cove ? "Yes" : "No");
        printf("  Software: %s %s\n", identity.software_name, identity.software_version);
        printf("\n");
    }
    else
    {
        printf("No node identity configured.\n");
        printf("Run 'plankctl init' to initialize.\n\n");
    }

    /* Count objects */
    printf("Statistics:\n");
    printf("  Objects:      %d\n", db_count(g_db, "SELECT COUNT(*) FROM plank_objects"));
    {
        char sql[128];
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM plank_objects WHERE object_class = %d", PLANK_CLASS_MESSAGE);
        printf("  Messages:     %d\n", db_count(g_db, sql));
    }
    {
        char sql[128];
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM plank_objects WHERE object_class = %d", PLANK_CLASS_ATTACHMENT_META);
        printf("  Attachments:  %d\n", db_count(g_db, sql));
    }
    printf("  Dead letters: %d\n", db_count(g_db, "SELECT COUNT(*) FROM plank_deadletters"));
    printf("  Quarantine:   %d\n", db_count(g_db, "SELECT COUNT(*) FROM plank_quarantine"));

    return 0;
}

static int cmd_init(int argc, char *argv[])
{
    const char *node_name = NULL;
    const char *network_name = "default";
    bool force = false;

    static struct option opts[] = {
        {"name", required_argument, 0, 'n'},
        {"network", required_argument, 0, 'N'},
        {"force", no_argument, 0, 'f'},
        {0, 0, 0, 0}};

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "n:N:f", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'n':
            node_name = optarg;
            break;
        case 'N':
            network_name = optarg;
            break;
        case 'f':
            force = true;
            break;
        default:
            return 1;
        }
    }

    if (!node_name)
    {
        fprintf(stderr, "Error: --name is required\n");
        return 1;
    }

    /* Check if already initialized */
    plank_node_identity_t existing;
    if (plank_store_get_identity(g_store, &existing) && !force)
    {
        fprintf(stderr, "Error: Node already initialized. Use --force to reinitialize.\n");
        return 1;
    }

    printf("Initializing PLANK node...\n");
    printf("  Node name: %s\n", node_name);
    printf("  Network:   %s\n", network_name);

    if (!plank_store_generate_identity(g_store, node_name, network_name))
    {
        fprintf(stderr, "Error: Failed to generate identity: %s\n", plank_last_error());
        return 1;
    }

    printf("Node initialized successfully.\n");
    return 0;
}

static int cmd_links(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Configured Links\n");
    printf("================\n\n");

#ifdef HAVE_SQLITE
    const char *sql =
        "SELECT l.id, l.link_name, p.node_addr, l.remote_host, l.remote_port, "
        "l.direction, l.enabled, l.paused, l.state, l.retry_count, l.last_success_at, l.last_error "
        "FROM plank_links l "
        "LEFT JOIN plank_peers p ON l.peer_id = p.id "
        "ORDER BY l.id";
    query_count_t count = {0};
    if (!db_query(g_db, sql, link_row_cb, &count))
    {
        fprintf(stderr, "Error querying links: %s\n", db_last_error(g_db));
        return 1;
    }
    if (count.rows == 0)
        printf("(No links configured)\n");
#else
    printf("(PLANK link query requires SQLite support)\n");
#endif

    return 0;
}

static int cmd_peers(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Configured Peers\n");
    printf("================\n\n");

#ifdef HAVE_SQLITE
    const char *sql =
        "SELECT id, node_id, node_addr, trust_level, status, last_seen_at "
        "FROM plank_peers ORDER BY node_addr";
    query_count_t count = {0};
    if (!db_query(g_db, sql, peer_row_cb, &count))
    {
        fprintf(stderr, "Error querying peers: %s\n", db_last_error(g_db));
        return 1;
    }
    if (count.rows == 0)
        printf("(No peers configured)\n");
#else
    printf("(PLANK peer query requires SQLite support)\n");
#endif

    return 0;
}

static int cmd_link_add(int argc, char *argv[])
{
    const char *name = NULL;
    const char *host = NULL;
    int port = 5150;
    int peer_id = -1;

    static struct option opts[] = {
        {"name", required_argument, 0, 'n'},
        {"host", required_argument, 0, 'H'},
        {"port", required_argument, 0, 'p'},
        {"peer-id", required_argument, 0, 'P'},
        {0, 0, 0, 0}};

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "n:H:p:P:", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'n':
            name = optarg;
            break;
        case 'H':
            host = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 'P':
            peer_id = atoi(optarg);
            break;
        default:
            return 1;
        }
    }

    if (!name || !host)
    {
        fprintf(stderr, "Error: --name and --host are required\n");
        return 1;
    }
    if (peer_id <= 0)
    {
        fprintf(stderr, "Error: --peer-id is required and must reference an existing plank_peers.id\n");
        return 1;
    }
    {
        char sql[128];
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM plank_peers WHERE id = %d", peer_id);
        if (db_count(g_db, sql) != 1)
        {
            fprintf(stderr, "Error: Peer ID %d not found. Use 'plankctl peers' to list configured peers.\n", peer_id);
            return 1;
        }
    }

    printf("Adding link '%s' to %s:%d\n", name, host, port);

    plank_link_t link;
    memset(&link, 0, sizeof(link));

    plank_crypto_random(link.link_id, PLANK_LINK_ID_SIZE);
    strncpy(link.link_name, name, sizeof(link.link_name) - 1);
    strncpy(link.remote_host, host, sizeof(link.remote_host) - 1);
    link.peer_id = peer_id;
    link.remote_port = port;
    link.direction = PLANK_DIR_OUTBOUND;
    link.enabled = true;
    link.state = PLANK_LINK_IDLE;
    link.retry_initial_sec = 60;
    link.retry_max_sec = 3600;
    link.retry_limit = 10;

    int link_id;
    if (!plank_store_link_add(g_store, &link, &link_id))
    {
        fprintf(stderr, "Error: Failed to add link: %s\n", plank_last_error());
        return 1;
    }

    printf("Link added with ID %d\n", link_id);
    return 0;
}

static int cmd_areas(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("PLANK Areas\n");
    printf("===========\n\n");

#ifdef HAVE_SQLITE
    const char *sql =
        "SELECT id, area_addr, title, distribution_mode, status FROM plank_areas ORDER BY id";
    query_count_t count = {0};
    if (!db_query(g_db, sql, area_row_cb, &count))
    {
        fprintf(stderr, "Error querying areas: %s\n", db_last_error(g_db));
        return 1;
    }
    if (count.rows == 0)
        printf("(No PLANK areas configured)\n");
#else
    printf("(PLANK area query requires SQLite support)\n");
#endif

    return 0;
}

static int cmd_queue(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Outbound Queue\n");
    printf("==============\n\n");

#ifdef HAVE_SQLITE
    const char *sql =
        "SELECT q.id, q.link_id, l.link_name, q.object_id, q.priority, q.status, q.bundle_id, q.queued_at "
        "FROM plank_outbound_queue q "
        "LEFT JOIN plank_links l ON q.link_id = l.id "
        "WHERE q.status = 0 "
        "ORDER BY q.priority DESC, q.id ASC LIMIT 100";
    query_count_t count = {0};
    if (!db_query(g_db, sql, outbound_row_cb, &count))
    {
        fprintf(stderr, "Error querying outbound queue: %s\n", db_last_error(g_db));
        return 1;
    }
    if (count.rows == 0)
        printf("(No pending outbound queue entries)\n");
#else
    printf("(Outbound queue query requires SQLite support)\n");
#endif

    return 0;
}

static int cmd_deadletters(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Dead Letters\n");
    printf("============\n\n");

#ifdef HAVE_SQLITE
    const char *sql =
        "SELECT id, target_link_id, target_node_addr, object_ids, bundle_id, "
        "last_error_code, last_error_text, retry_count, state, last_failure_at "
        "FROM plank_deadletters ORDER BY id";
    query_count_t count = {0};
    if (!db_query(g_db, sql, deadletter_row_cb, &count))
    {
        fprintf(stderr, "Error querying dead letters: %s\n", db_last_error(g_db));
        return 1;
    }
    if (count.rows == 0)
        printf("(No dead letters)\n");
#else
    printf("(Dead letters query requires SQLite support)\n");
#endif

    return 0;
}

static int cmd_quarantine(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Quarantine\n");
    printf("==========\n\n");

#ifdef HAVE_SQLITE
    const char *sql =
        "SELECT id, object_id, object_class, source_link_id, source_node_addr, "
        "quarantine_reason, quarantine_text FROM plank_quarantine ORDER BY id";
    query_count_t count = {0};
    if (!db_query(g_db, sql, quarantine_row_cb, &count))
    {
        fprintf(stderr, "Error querying quarantine: %s\n", db_last_error(g_db));
        return 1;
    }
    if (count.rows == 0)
        printf("(No quarantined items)\n");
#else
    printf("(Quarantine query requires SQLite support)\n");
#endif

    return 0;
}

static int cmd_audit(int argc, char *argv[])
{
    int limit = 50;

    static struct option opts[] = {
        {"limit", required_argument, 0, 'l'},
        {0, 0, 0, 0}};

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "l:", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'l':
            limit = atoi(optarg);
            break;
        default:
            return 1;
        }
    }

    printf("Audit Log (last %d entries)\n", limit);
    printf("===========================\n\n");

#ifdef HAVE_SQLITE
    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT id, event_type, link_id, node_addr, user_handle, details, object_id "
             "FROM plank_audit ORDER BY id DESC LIMIT %d",
             limit);
    query_count_t count = {0};
    if (!db_query(g_db, sql, audit_row_cb, &count))
    {
        fprintf(stderr, "Error querying audit log: %s\n", db_last_error(g_db));
        return 1;
    }
    if (count.rows == 0)
        printf("(No audit entries)\n");
#else
    printf("(Audit log query requires SQLite support)\n");
#endif

    return 0;
}

static int cmd_rescan(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Triggering rescan of journal...\n");

    const char *sql =
        "UPDATE plank_journal SET processing_state = 0 "
        "WHERE processing_state != 0";
    int updated = db_exec_simple(g_db, sql);
    if (updated < 0)
    {
        fprintf(stderr, "Error: Failed to reset journal state: %s\n", db_last_error(g_db));
        return 1;
    }

    printf("Rescan triggered. Reset %d journal entries to pending.\n", updated);
    return 0;
}

static int cmd_journal(int argc, char *argv[])
{
    int limit = 100;

    static struct option opts[] = {
        {"limit", required_argument, 0, 'l'},
        {0, 0, 0, 0}};

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "l:", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'l':
            limit = atoi(optarg);
            break;
        default:
            return 1;
        }
    }
    if (limit < 1)
    {
        fprintf(stderr, "Error: --limit must be greater than zero\n");
        return 1;
    }

    printf("Journal Entries (last %d)\n", limit);
    printf("==========================\n\n");

#ifdef HAVE_SQLITE
    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT local_seq, object_id, object_class, source_kind, source_link_id, processing_state "
             "FROM plank_journal ORDER BY local_seq DESC LIMIT %d",
             limit);
    query_count_t count = {0};
    if (!db_query(g_db, sql, journal_row_cb, &count))
    {
        fprintf(stderr, "Error querying journal: %s\n", db_last_error(g_db));
        return 1;
    }
    if (count.rows == 0)
        printf("(No journal entries)\n");
#else
    printf("(Journal query requires SQLite support)\n");
#endif

    return 0;
}

static int cmd_requeue(int argc, char *argv[])
{
    int deadletter_id = -1;
    bool all = false;

    static struct option opts[] = {
        {"id", required_argument, 0, 'i'},
        {"all", no_argument, 0, 'a'},
        {0, 0, 0, 0}};

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "i:a", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'i':
            deadletter_id = atoi(optarg);
            break;
        case 'a':
            all = true;
            break;
        default:
            return 1;
        }
    }

    if (!all && deadletter_id < 0)
    {
        fprintf(stderr, "Error: Specify --id or --all\n");
        return 1;
    }

    if (all)
    {
        printf("Requeuing all dead letters...\n");
        const char *sql =
            "UPDATE plank_deadletters SET state = 1, retry_count = retry_count + 1, "
            "last_failure_at = datetime('now') WHERE state = 0";
        int updated = db_exec_simple(g_db, sql);
        if (updated < 0)
        {
            fprintf(stderr, "Error: Failed to requeue dead letters: %s\n", db_last_error(g_db));
            return 1;
        }
        printf("Requeued %d dead letters.\n", updated);
    }
    else
    {
        printf("Requeuing dead letter %d...\n", deadletter_id);
        plank_router_t *router = plank_router_create(g_store);
        if (!router)
        {
            fprintf(stderr, "Error: Failed to create router\n");
            return 1;
        }
        if (plank_router_requeue_deadletter(router, deadletter_id))
        {
            printf("Requeued successfully.\n");
        }
        else
        {
            fprintf(stderr, "Error: Failed to requeue dead letter %d\n", deadletter_id);
            plank_router_free(router);
            return 1;
        }
        plank_router_free(router);
    }

    return 0;
}

static int cmd_verify(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Running integrity verification...\n\n");

    int total_objects = db_count(g_db, "SELECT COUNT(*) FROM plank_objects");
    int unverified_objects = db_count(g_db, "SELECT COUNT(*) FROM plank_objects WHERE verified = 0");
    printf("Verifying object metadata... ");
    if (total_objects < 0)
    {
        printf("failed\n");
        fprintf(stderr, "Error: %s\n", db_last_error(g_db));
        return 1;
    }
    if (unverified_objects == 0)
    {
        printf("ok (%d objects, all marked verified)\n", total_objects);
    }
    else
    {
        printf("warning (%d unverified objects of %d)\n", unverified_objects, total_objects);
    }

    printf("Verifying object IDs and journal links... ");
    int orphaned_journal = db_count(g_db,
                                    "SELECT COUNT(*) FROM plank_journal "
                                    "LEFT JOIN plank_objects ON plank_journal.object_id = plank_objects.object_id "
                                    "WHERE plank_objects.object_id IS NULL");
    if (orphaned_journal == 0)
    {
        printf("ok\n");
    }
    else
    {
        printf("failed (%d orphaned journal rows)\n", orphaned_journal);
    }

    printf("Checking dead letter and quarantine referential integrity... ");
    int deadletter_orphans = db_count(g_db,
                                      "SELECT COUNT(*) FROM plank_deadletters d "
                                      "LEFT JOIN plank_links l ON d.target_link_id = l.id "
                                      "WHERE l.id IS NULL");
    int quarantine_orphans = db_count(g_db,
                                      "SELECT COUNT(*) FROM plank_quarantine q "
                                      "LEFT JOIN plank_links l ON q.source_link_id = l.id "
                                      "WHERE q.source_link_id IS NOT NULL AND l.id IS NULL");
    if (deadletter_orphans == 0 && quarantine_orphans == 0)
    {
        printf("ok\n");
    }
    else
    {
        printf("failed (%d dead letter link orphans, %d quarantine link orphans)\n",
               deadletter_orphans, quarantine_orphans);
    }

    printf("Checking outbound queue referential integrity... ");
    int outbound_orphans = db_count(g_db,
                                    "SELECT COUNT(*) FROM plank_outbound_queue q "
                                    "LEFT JOIN plank_links l ON q.link_id = l.id "
                                    "LEFT JOIN plank_objects o ON q.object_id = o.object_id "
                                    "WHERE l.id IS NULL OR o.object_id IS NULL");
    if (outbound_orphans == 0)
        printf("ok\n");
    else
        printf("failed (%d orphaned outbound queue rows)\n", outbound_orphans);

    printf("\nVerification complete.\n");
    return (orphaned_journal == 0 && deadletter_orphans == 0 &&
            quarantine_orphans == 0 && outbound_orphans == 0)
               ? 0
               : 1;
}

static int cmd_export_key(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    plank_node_identity_t identity;
    if (!plank_store_get_identity(g_store, &identity))
    {
        fprintf(stderr, "Error: No node identity configured\n");
        return 1;
    }

    printf("Node Public Key\n");
    printf("===============\n\n");
    printf("Node ID:     ");
    print_hex(identity.node_id, PLANK_NODE_ID_SIZE);
    printf("\n");
    printf("Node Addr:   %s\n", identity.node_addr);
    printf("Signing Key: ");
    print_hex(identity.signing_key_pub, PLANK_PUBKEY_SIZE);
    printf("\n");

    /* Fingerprint */
    uint8_t fingerprint[PLANK_OBJECT_ID_SIZE];
    if (plank_crypto_key_fingerprint(identity.signing_key_pub, fingerprint))
    {
        printf("Fingerprint: ");
        print_hex(fingerprint, 8);
        printf("\n");
    }

    return 0;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options] <command> [command-options]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -c, --config FILE    Configuration file\n");
    fprintf(stderr, "  -d, --database FILE  Database file\n");
    fprintf(stderr, "  -h, --help           Show this help\n");
    fprintf(stderr, "  -V, --version        Show version\n");
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  status               Show node status\n");
    fprintf(stderr, "  init                 Initialize node identity\n");
    fprintf(stderr, "  peers                List configured peers\n");
    fprintf(stderr, "  links                List configured links\n");
    fprintf(stderr, "  link-add             Add a new link\n");
    fprintf(stderr, "  areas                List PLANK areas\n");
    fprintf(stderr, "  queue                Show outbound queue\n");
    fprintf(stderr, "  journal              Show object journal entries\n");
    fprintf(stderr, "  deadletters          Show dead letters\n");
    fprintf(stderr, "  quarantine           Show quarantined items\n");
    fprintf(stderr, "  audit                Show audit log\n");
    fprintf(stderr, "  rescan               Trigger journal rescan\n");
    fprintf(stderr, "  requeue              Requeue dead letters\n");
    fprintf(stderr, "  verify               Run integrity verification\n");
    fprintf(stderr, "  export-key           Export node public key\n");
}

int main(int argc, char *argv[])
{
    const char *config_file = "/etc/mutineer/mutineer.conf";
    const char *db_file = NULL;

    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"database", required_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "+c:d:hV", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'c':
            config_file = optarg;
            break;
        case 'd':
            db_file = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'V':
            printf("plankctl %s (PLANK protocol %d)\n",
                   PLANKCTL_VERSION, PLANK_PROTOCOL_VERSION);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc)
    {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[optind];

    /* Initialize PLANK */
    if (!plank_init())
    {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }

    /* Load configuration */
    BbsConfig config;
    if (!cfg_load(config_file, &config))
    {
        fprintf(stderr, "Warning: Failed to load configuration: %s\n", config_file);
    }

    /* Determine database path */
    if (!db_file)
    {
        db_file = config.db_path;
    }

    /* Open database */
    g_db = db_open(db_file);
    if (!g_db)
    {
        fprintf(stderr, "Failed to open database: %s\n", db_file);
        plank_shutdown();
        return 1;
    }

    /* Initialize store */
    g_store = plank_store_open(g_db);
    if (!g_store)
    {
        fprintf(stderr, "Failed to initialize PLANK store\n");
        db_close(g_db);
        plank_shutdown();
        return 1;
    }

    /* Dispatch command */
    int result = 1;
    int cmd_argc = argc - optind;
    char **cmd_argv = argv + optind;

    if (strcmp(command, "status") == 0)
    {
        result = cmd_status(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "init") == 0)
    {
        result = cmd_init(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "links") == 0)
    {
        result = cmd_links(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "peers") == 0)
    {
        result = cmd_peers(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "link-add") == 0)
    {
        result = cmd_link_add(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "areas") == 0)
    {
        result = cmd_areas(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "queue") == 0)
    {
        result = cmd_queue(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "deadletters") == 0)
    {
        result = cmd_deadletters(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "quarantine") == 0)
    {
        result = cmd_quarantine(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "audit") == 0)
    {
        result = cmd_audit(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "rescan") == 0)
    {
        result = cmd_rescan(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "journal") == 0)
    {
        result = cmd_journal(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "requeue") == 0)
    {
        result = cmd_requeue(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "verify") == 0)
    {
        result = cmd_verify(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "export-key") == 0)
    {
        result = cmd_export_key(cmd_argc, cmd_argv);
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
    }

    /* Cleanup */
    plank_store_close(g_store);
    db_close(g_db);
    plank_shutdown();

    return result;
}
