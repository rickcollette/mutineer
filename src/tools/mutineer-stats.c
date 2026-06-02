/*
 * mutineer-stats - Display system statistics
 * 
 * Usage: mutineer-stats [options]
 * 
 * Options:
 *   -c, --config <path>   Path to config file (default: conf/mutineer.conf)
 *   -j, --json            Output in JSON format
 *   -s, --short           Short output (key=value pairs)
 *   -h, --help            Show this help
 * 
 * Exit codes:
 *   0 - Success
 *   2 - Database error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>

#include "bbs_db.h"
#include "bbs_config.h"

typedef enum {
    OUTPUT_NORMAL,
    OUTPUT_JSON,
    OUTPUT_SHORT
} OutputFormat;

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Display system statistics.\n\n");
    printf("Options:\n");
    printf("  -c, --config <path>   Path to config file (default: conf/mutineer.conf)\n");
    printf("  -j, --json            Output in JSON format\n");
    printf("  -s, --short           Short output (key=value pairs)\n");
    printf("  -h, --help            Show this help\n");
}

int main(int argc, char** argv) {
    const char* config_path = NULL;
    OutputFormat format = OUTPUT_NORMAL;
    
    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"json",   no_argument,       0, 'j'},
        {"short",  no_argument,       0, 's'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:jsh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 'j': format = OUTPUT_JSON; break;
            case 's': format = OUTPUT_SHORT; break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    BbsConfig cfg;
    if (!cfg_load(config_path ? config_path : "conf/mutineer.conf", &cfg)) {
        fprintf(stderr, "Failed to load config\n");
        return 2;
    }
    
    BbsDb* db = db_open(cfg.db_path);
    if (!db) {
        fprintf(stderr, "Failed to open database: %s\n", cfg.db_path);
        return 2;
    }
    
    int users = db_count_users(db);
    int messages = db_count_messages(db);
    int files = db_count_files(db);
    (void)db_stats_get_val(db, "calls");  /* calls tracked in daily_stats */
    int oneliners = db_oneliner_count(db);
    
    DbMsgArea msg_areas[64];
    int num_msg_areas = db_msg_area_list(db, msg_areas, 64);
    
    DbFileArea file_areas[64];
    int num_file_areas = db_file_area_list(db, file_areas, 64);
    
    DbDailyStats daily;
    db_daily_stats_get(db, &daily);
    
    DbSystemTotals totals;
    db_system_totals_get(db, &totals);
    
    switch (format) {
        case OUTPUT_JSON:
            printf("{\n");
            printf("  \"bbs_name\": \"%s\",\n", cfg.bbs_name);
            printf("  \"users\": %d,\n", users);
            printf("  \"messages\": %d,\n", messages);
            printf("  \"files\": %d,\n", files);
            printf("  \"message_areas\": %d,\n", num_msg_areas);
            printf("  \"file_areas\": %d,\n", num_file_areas);
            printf("  \"oneliners\": %d,\n", oneliners);
            printf("  \"today\": {\n");
            printf("    \"calls\": %d,\n", daily.calls);
            printf("    \"posts\": %d,\n", daily.posts);
            printf("    \"emails\": %d,\n", daily.emails);
            printf("    \"newusers\": %d,\n", daily.newusers);
            printf("    \"uploads\": %d,\n", daily.uploads);
            printf("    \"downloads\": %d\n", daily.downloads);
            printf("  },\n");
            printf("  \"totals\": {\n");
            printf("    \"calls\": %d,\n", totals.total_calls);
            printf("    \"posts\": %d,\n", totals.total_posts);
            printf("    \"uploads\": %d,\n", totals.total_uploads);
            printf("    \"downloads\": %d,\n", totals.total_downloads);
            printf("    \"days_online\": %d\n", totals.days_online);
            printf("  }\n");
            printf("}\n");
            break;
            
        case OUTPUT_SHORT:
            printf("users=%d\n", users);
            printf("messages=%d\n", messages);
            printf("files=%d\n", files);
            printf("msg_areas=%d\n", num_msg_areas);
            printf("file_areas=%d\n", num_file_areas);
            printf("oneliners=%d\n", oneliners);
            printf("today_calls=%d\n", daily.calls);
            printf("today_posts=%d\n", daily.posts);
            printf("today_uploads=%d\n", daily.uploads);
            printf("today_downloads=%d\n", daily.downloads);
            printf("total_calls=%d\n", totals.total_calls);
            printf("total_posts=%d\n", totals.total_posts);
            printf("days_online=%d\n", totals.days_online);
            break;
            
        case OUTPUT_NORMAL:
        default:
            printf("Mutineer BBS Statistics\n");
            printf("=======================\n");
            printf("BBS Name:       %s\n", cfg.bbs_name);
            printf("\n");
            printf("Database:\n");
            printf("  Total Users:    %d\n", users);
            printf("  Total Messages: %d\n", messages);
            printf("  Total Files:    %d\n", files);
            printf("  Message Areas:  %d\n", num_msg_areas);
            printf("  File Areas:     %d\n", num_file_areas);
            printf("  One-Liners:     %d\n", oneliners);
            printf("\n");
            printf("Today's Activity:\n");
            printf("  Calls:     %d\n", daily.calls);
            printf("  Posts:     %d\n", daily.posts);
            printf("  Emails:    %d\n", daily.emails);
            printf("  New Users: %d\n", daily.newusers);
            printf("  Uploads:   %d\n", daily.uploads);
            printf("  Downloads: %d\n", daily.downloads);
            printf("\n");
            printf("System Totals:\n");
            printf("  Total Calls:     %d\n", totals.total_calls);
            printf("  Total Posts:     %d\n", totals.total_posts);
            printf("  Total Uploads:   %d\n", totals.total_uploads);
            printf("  Total Downloads: %d\n", totals.total_downloads);
            printf("  Days Online:     %d\n", totals.days_online);
            break;
    }
    
    db_close(db);
    return 0;
}
