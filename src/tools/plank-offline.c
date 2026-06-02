/*
 * plank-offline - PLANK Offline Packet Tool
 * Export offline user packets, import user reply packets
 */

#include "plank/plank.h"
#include "bbs_db.h"
#include "bbs_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#define PLANK_OFFLINE_VERSION "1.0.0"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static BbsDb *g_db = NULL;
static plank_store_t *g_store = NULL;

/* ============================================================================
 * EXPORT COMMAND
 * ============================================================================ */

static int cmd_export(int argc, char *argv[])
{
    const char *output = NULL;
    int user_id = -1;
    const char *username = NULL;
    bool include_read = false;
    int max_messages = 500;

    static struct option opts[] = {
        {"output", required_argument, 0, 'o'},
        {"user-id", required_argument, 0, 'u'},
        {"username", required_argument, 0, 'U'},
        {"include-read", no_argument, 0, 'r'},
        {"max", required_argument, 0, 'm'},
        {0, 0, 0, 0}};

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:u:U:rm:", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'o':
            output = optarg;
            break;
        case 'u':
            user_id = atoi(optarg);
            break;
        case 'U':
            username = optarg;
            break;
        case 'r':
            include_read = true;
            break;
        case 'm':
            max_messages = atoi(optarg);
            if (max_messages < 1)
            {
                fprintf(stderr, "Error: --max must be greater than zero\n");
                return 1;
            }
            break;
        default:
            return 1;
        }
    }

    if (!output)
    {
        fprintf(stderr, "Error: --output is required\n");
        return 1;
    }

    if (user_id < 0 && !username)
    {
        fprintf(stderr, "Error: --user-id or --username is required\n");
        return 1;
    }

    /* Look up user if username provided */
    if (username)
    {
        DbUser user;
        if (!db_user_fetch(g_db, username, &user))
        {
            fprintf(stderr, "Error: User not found: %s\n", username);
            return 1;
        }
        user_id = user.id;
    }

    printf("Exporting offline packet for user %d\n", user_id);
    printf("  Output: %s\n", output);
    printf("  Include read: %s\n", include_read ? "yes" : "no");
    printf("  Max messages: %d\n", max_messages);

    /* Get node identity */
    plank_node_identity_t identity;
    if (!plank_store_get_identity(g_store, &identity))
    {
        fprintf(stderr, "Error: Node not initialized\n");
        return 1;
    }

    /* For now, export all area posts and direct mail (no area filter) */
    /* In a full implementation, would query user's subscribed areas from BBS DB */
    uint8_t export_id[PLANK_EXPORT_ID_SIZE];
    int message_count = 0;

    if (!plank_bundle_export_user_packet_ex(g_store, user_id,
                                            NULL, 0, /* no area filter = all areas */
                                            true,    /* include direct mail for this user */
                                            include_read,
                                            max_messages,
                                            output,
                                            identity.signing_key_priv,
                                            export_id,
                                            &message_count))
    {
        fprintf(stderr, "Error: Failed to export packet: %s\n", plank_last_error());
        return 1;
    }

    printf("\nExport complete.\n");
    printf("  Export ID: ");
    for (int i = 0; i < PLANK_EXPORT_ID_SIZE; i++)
        printf("%02x", export_id[i]);
    printf("\n");
    printf("  Messages: %d\n", message_count);
    printf("  Output file: %s\n", output);

    return 0;
}

/* ============================================================================
 * IMPORT COMMAND
 * ============================================================================ */

static int cmd_import(int argc, char *argv[])
{
    const char *input = NULL;
    int user_id = -1;
    const char *username = NULL;

    static struct option opts[] = {
        {"input", required_argument, 0, 'i'},
        {"user-id", required_argument, 0, 'u'},
        {"username", required_argument, 0, 'U'},
        {0, 0, 0, 0}};

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "i:u:U:", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'i':
            input = optarg;
            break;
        case 'u':
            user_id = atoi(optarg);
            break;
        case 'U':
            username = optarg;
            break;
        default:
            return 1;
        }
    }

    if (!input)
    {
        fprintf(stderr, "Error: --input is required\n");
        return 1;
    }

    if (user_id < 0 && !username)
    {
        fprintf(stderr, "Error: --user-id or --username is required\n");
        return 1;
    }

    /* Look up user if username provided */
    if (username)
    {
        DbUser user;
        if (!db_user_fetch(g_db, username, &user))
        {
            fprintf(stderr, "Error: User not found: %s\n", username);
            return 1;
        }
        user_id = user.id;
    }

    printf("Importing reply packet for user %d\n", user_id);
    printf("  Input: %s\n", input);

    /* Open and verify bundle */
    plank_bundle_reader_t *reader = plank_bundle_reader_open(input);
    if (!reader)
    {
        fprintf(stderr, "Error: Failed to open bundle: %s\n", plank_last_error());
        return 1;
    }

    const plank_bundle_manifest_t *manifest = plank_bundle_reader_manifest(reader);

    /* Verify bundle type */
    if (manifest->bundle_type != PLANK_BUNDLE_USER_REPLY)
    {
        fprintf(stderr, "Error: Not a reply bundle (type=%d)\n", manifest->bundle_type);
        plank_bundle_reader_close(reader);
        return 1;
    }

    printf("  Bundle type: REPLY\n");
    printf("  Objects: %u\n", manifest->object_count);

    /* Import bundle */
    plank_reply_import_result_t result;
    memset(&result, 0, sizeof(result));
    if (!plank_bundle_import_reply(g_store, input, user_id, NULL, &result))
    {
        fprintf(stderr, "Error: Import failed: %s\n", result.error);
        plank_bundle_reader_close(reader);
        return 1;
    }

    printf("\nImport complete.\n");
    printf("  Messages imported: %d\n", result.messages_imported);
    printf("  Duplicates: %d\n", result.messages_duplicate);
    printf("  Rejected: %d\n", result.messages_rejected);

    plank_bundle_reader_close(reader);
    return 0;
}

/* ============================================================================
 * LIST COMMAND
 * ============================================================================ */

static int cmd_list(int argc, char *argv[])
{
    int user_id = -1;
    const char *username = NULL;

    static struct option opts[] = {
        {"user-id", required_argument, 0, 'u'},
        {"username", required_argument, 0, 'U'},
        {0, 0, 0, 0}};

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "u:U:", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'u':
            user_id = atoi(optarg);
            break;
        case 'U':
            username = optarg;
            break;
        default:
            return 1;
        }
    }

    if (user_id < 0 && !username)
    {
        /* List all users with pending exports */
        printf("Users with pending offline packets:\n");
        printf("===================================\n\n");

#ifdef HAVE_SQLITE
        const char *count_sql = "SELECT COUNT(*) FROM plank_user_exports WHERE status = 0";
        int pending_count = db_query_int(g_db, count_sql, 0);
        if (pending_count > 0)
        {
            printf("  Found %d pending export(s)\n", pending_count);
        }
        else
        {
            printf("  (No pending exports)\n");
        }
#else
        printf("  (PLANK offline query requires SQLite support)\n");
#endif
    }
    else
    {
        /* List exports for specific user */
        if (username)
        {
            DbUser user;
            if (!db_user_fetch(g_db, username, &user))
            {
                fprintf(stderr, "Error: User not found: %s\n", username);
                return 1;
            }
            user_id = user.id;
        }

        printf("Offline packet history for user %d:\n", user_id);
        printf("====================================\n\n");

#ifdef HAVE_SQLITE
        char sql[512];
        snprintf(sql, sizeof(sql),
                 "SELECT COUNT(*) FROM plank_user_exports WHERE user_id = %d",
                 user_id);
        int count = db_query_int(g_db, sql, 0);
        printf("Exports: %d total\n", count);

        snprintf(sql, sizeof(sql),
                 "SELECT COUNT(*) FROM plank_user_replies WHERE user_id = %d",
                 user_id);
        int reply_count = db_query_int(g_db, sql, 0);
        printf("Replies imported: %d total\n", reply_count);
#else
        printf("  (PLANK offline query requires SQLite support)\n");
#endif
    }

    return 0;
}

/* ============================================================================
 * STATUS COMMAND
 * ============================================================================ */

static int cmd_status(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Offline Packet Status\n");
    printf("=====================\n\n");

#ifdef HAVE_SQLITE
    /* Query pending exports */
    const char *pending_sql = "SELECT COUNT(*) FROM plank_user_exports WHERE status = 0";
    int pending = db_query_int(g_db, pending_sql, 0);
    printf("Pending exports:  %d\n", pending);

    /* Query total exports */
    const char *total_exp_sql = "SELECT COUNT(*) FROM plank_user_exports WHERE status > 0";
    int total_exports = db_query_int(g_db, total_exp_sql, 0);
    printf("Total exports:    %d\n", total_exports);

    /* Query total imports */
    const char *total_imp_sql = "SELECT COUNT(*) FROM plank_user_replies WHERE import_result = 0";
    int total_imports = db_query_int(g_db, total_imp_sql, 0);
    printf("Total imports:    %d\n", total_imports);

    /* Query failed imports */
    const char *fail_imp_sql = "SELECT COUNT(*) FROM plank_user_replies WHERE import_result != 0";
    int failed_imports = db_query_int(g_db, fail_imp_sql, 0);
    printf("Failed imports:   %d\n", failed_imports);

    printf("\n");

    /* Summary stats */
    int total_messages = db_query_int(g_db,
                                      "SELECT COALESCE(SUM(message_count), 0) FROM plank_user_exports",
                                      0);
    printf("Total messages exported: %d\n", total_messages);

    int total_reply_messages = db_query_int(g_db,
                                            "SELECT COALESCE(SUM(message_count), 0) FROM plank_user_replies",
                                            0);
    printf("Total messages imported: %d\n", total_reply_messages);
#else
    printf("Pending exports:  (requires SQLite support)\n");
    printf("Total exports:    (requires SQLite support)\n");
    printf("Total imports:    (requires SQLite support)\n");
    printf("Failed imports:   (requires SQLite support)\n");
#endif

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
    fprintf(stderr, "  export   Export offline packet for user\n");
    fprintf(stderr, "  import   Import reply packet from user\n");
    fprintf(stderr, "  list     List pending/completed exports\n");
    fprintf(stderr, "  status   Show offline packet statistics\n");
    fprintf(stderr, "\nExport options:\n");
    fprintf(stderr, "  -o, --output FILE    Output bundle file\n");
    fprintf(stderr, "  -u, --user-id ID     User ID\n");
    fprintf(stderr, "  -U, --username NAME  Username\n");
    fprintf(stderr, "  -r, --include-read   Include already-read messages\n");
    fprintf(stderr, "  -m, --max COUNT      Maximum messages (default: 500)\n");
    fprintf(stderr, "\nImport options:\n");
    fprintf(stderr, "  -i, --input FILE     Input bundle file\n");
    fprintf(stderr, "  -u, --user-id ID     User ID\n");
    fprintf(stderr, "  -U, --username NAME  Username\n");
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
            printf("plank-offline %s (PLANK protocol %d)\n",
                   PLANK_OFFLINE_VERSION, PLANK_PROTOCOL_VERSION);
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

    if (strcmp(command, "export") == 0)
    {
        result = cmd_export(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "import") == 0)
    {
        result = cmd_import(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "list") == 0)
    {
        result = cmd_list(cmd_argc, cmd_argv);
    }
    else if (strcmp(command, "status") == 0)
    {
        result = cmd_status(cmd_argc, cmd_argv);
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
