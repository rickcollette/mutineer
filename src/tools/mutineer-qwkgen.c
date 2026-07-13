#define _XOPEN_SOURCE 700
/*
 * mutineer-qwkgen - Generate QWK mail packets for offline reading
 *
 * Usage: mutineer-qwkgen [options] <username>
 *
 * Options:
 *   -c, --config <path>   Path to config file (default: conf/mutineer.conf)
 *   -o, --output <path>   Output QWK file path (default: <username>.QWK)
 *   -a, --areas <list>    Comma-separated area IDs (default: all accessible)
 *   -m, --max <n>         Max messages per area (default: 500)
 *   -n, --new-only        Only include messages since last QWK download
 *   -v, --verbose         Verbose output
 *   -h, --help            Show this help
 *
 * Exit codes:
 *   0 - Success
 *   1 - User not found
 *   2 - Database error
 *   3 - File error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include "bbs_db.h"
#include "bbs_config.h"
#include "bbs_acs.h"
#include "bbs_util.h"
#include "bbs_archive.h"

#define QWK_BLOCK_SIZE 128
#define DEFAULT_MAX_MSGS 500

typedef struct
{
    char status;
    char msgnum[7];
    char date[8];
    char time[5];
    char to[25];
    char from[25];
    char subject[25];
    char password[12];
    char refnum[8];
    char blocks[6];
    char active;
    char conf_num[2];
    char logical_num[2];
    char nettag;
} QwkMsgHeader;

static void pad_field(char *dest, const char *src, size_t len)
{
    memset(dest, ' ', len);
    size_t slen = src ? strlen(src) : 0;
    if (slen > len)
        slen = len;
    if (slen > 0)
        memcpy(dest, src, slen);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] <username>\n\n", prog);
    printf("Generate QWK mail packet for offline reading.\n\n");
    printf("Options:\n");
    printf("  -c, --config <path>   Path to config file (default: conf/mutineer.conf)\n");
    printf("  -o, --output <path>   Output QWK file path (default: <username>.QWK)\n");
    printf("  -a, --areas <list>    Comma-separated area IDs (default: all accessible)\n");
    printf("  -m, --max <n>         Max messages per area (default: %d)\n", DEFAULT_MAX_MSGS);
    printf("  -n, --new-only        Only include messages since last QWK download\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help\n");
    printf("\nExit codes:\n");
    printf("  0 - Success\n");
    printf("  1 - User not found\n");
    printf("  2 - Database error\n");
    printf("  3 - File error\n");
}

static bool write_control_dat(FILE *f, BbsConfig *cfg, DbUser *user, int num_areas, DbMsgArea *areas)
{
    fprintf(f, "%s\r\n", cfg->bbs_name[0] ? cfg->bbs_name : "Mutineer BBS");
    fprintf(f, "Unknown\r\n");
    fprintf(f, "000-000-0000\r\n");
    fprintf(f, "%s\r\n", cfg->sysop_name[0] ? cfg->sysop_name : "Sysop");
    fprintf(f, "00000,%s\r\n", "MUTINEER");

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    fprintf(f, "%02d-%02d-%04d,%02d:%02d:%02d\r\n",
            tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900,
            tm->tm_hour, tm->tm_min, tm->tm_sec);

    fprintf(f, "%s\r\n", user->handle);
    fprintf(f, "\r\n");
    fprintf(f, "0\r\n");
    fprintf(f, "0\r\n");
    fprintf(f, "0\r\n");
    fprintf(f, "%d\r\n", num_areas > 0 ? num_areas - 1 : 0);

    for (int i = 0; i < num_areas; i++)
    {
        fprintf(f, "%d\r\n", areas[i].id);
        fprintf(f, "%s\r\n", areas[i].name);
    }

    fprintf(f, "HELLO\r\n");
    fprintf(f, "NEWS\r\n");
    fprintf(f, "GOODBYE\r\n");

    return true;
}

static int write_messages_dat(FILE *f, BbsDb *db, DbUser *user, DbMsgArea *areas,
                              int num_areas, int max_per_area, bool new_only, bool verbose)
{
    int total_msgs = 0;

    char header_block[QWK_BLOCK_SIZE];
    memset(header_block, ' ', QWK_BLOCK_SIZE);
    memcpy(header_block, "Produced by Mutineer BBS", 24);
    fwrite(header_block, 1, QWK_BLOCK_SIZE, f);

    for (int a = 0; a < num_areas; a++)
    {
        if (!acs_check(areas[a].acs_read, user->level, user->flags, user->ac_flags))
        {
            continue;
        }

        DbMessage *msgs = calloc(max_per_area, sizeof(DbMessage));
        if (!msgs)
            continue;

        int mcount = db_messages_list(db, areas[a].id, msgs, max_per_area);
        int area_count = 0;

        for (int m = 0; m < mcount; m++)
        {
            if (new_only && user->last_qwk[0])
            {
                if (strcmp(msgs[m].created_at, user->last_qwk) <= 0)
                {
                    continue;
                }
            }

            QwkMsgHeader hdr;
            memset(&hdr, ' ', sizeof(hdr));

            hdr.status = (msgs[m].attr & 0x01) ? '*' : ' ';

            char numstr[16];
            snprintf(numstr, sizeof(numstr), "%d", msgs[m].id);
            pad_field(hdr.msgnum, numstr, 7);

            if (strlen(msgs[m].created_at) >= 10)
            {
                char datestr[9];
                snprintf(datestr, sizeof(datestr), "%c%c-%c%c-%c%c",
                         msgs[m].created_at[5], msgs[m].created_at[6],
                         msgs[m].created_at[8], msgs[m].created_at[9],
                         msgs[m].created_at[2], msgs[m].created_at[3]);
                pad_field(hdr.date, datestr, 8);

                if (strlen(msgs[m].created_at) >= 16)
                {
                    char timestr[6];
                    snprintf(timestr, sizeof(timestr), "%c%c:%c%c",
                             msgs[m].created_at[11], msgs[m].created_at[12],
                             msgs[m].created_at[14], msgs[m].created_at[15]);
                    pad_field(hdr.time, timestr, 5);
                }
            }

            pad_field(hdr.to, msgs[m].to_name[0] ? msgs[m].to_name : "All", 25);
            pad_field(hdr.from, msgs[m].from_name[0] ? msgs[m].from_name : msgs[m].user_handle, 25);
            pad_field(hdr.subject, msgs[m].subject, 25);
            pad_field(hdr.password, "", 12);

            snprintf(numstr, sizeof(numstr), "%d", msgs[m].reply_to);
            pad_field(hdr.refnum, numstr, 8);

            size_t body_len = strlen(msgs[m].body);
            int num_blocks = (int)((sizeof(QwkMsgHeader) + body_len + QWK_BLOCK_SIZE - 1) / QWK_BLOCK_SIZE);
            snprintf(numstr, sizeof(numstr), "%d", num_blocks);
            pad_field(hdr.blocks, numstr, 6);

            hdr.active = (char)225;
            hdr.conf_num[0] = (char)(areas[a].id & 0xFF);
            hdr.conf_num[1] = (char)((areas[a].id >> 8) & 0xFF);
            hdr.logical_num[0] = (char)(msgs[m].id & 0xFF);
            hdr.logical_num[1] = (char)((msgs[m].id >> 8) & 0xFF);
            hdr.nettag = ' ';

            fwrite(&hdr, 1, sizeof(hdr), f);

            size_t remaining = num_blocks * QWK_BLOCK_SIZE - sizeof(hdr);
            char *body_block = calloc(1, remaining);
            if (body_block)
            {
                memset(body_block, ' ', remaining);
                memcpy(body_block, msgs[m].body, body_len < remaining ? body_len : remaining);

                for (size_t i = 0; i < remaining && i < body_len; i++)
                {
                    if (body_block[i] == '\n')
                        body_block[i] = (char)227;
                }

                fwrite(body_block, 1, remaining, f);
                free(body_block);
            }

            area_count++;
            total_msgs++;
        }

        if (verbose && area_count > 0)
        {
            printf("  [%d] %s: %d messages\n", areas[a].id, areas[a].name, area_count);
        }

        free(msgs);
    }

    return total_msgs;
}

static void write_door_id(FILE *f, BbsConfig *cfg)
{
    fprintf(f, "DOOR = Mutineer\r\n");
    fprintf(f, "VERSION = 1.0\r\n");
    fprintf(f, "SYSTEM = %s\r\n", cfg->bbs_name[0] ? cfg->bbs_name : "Mutineer BBS");
    fprintf(f, "CONTROLNAME = MUTINEER\r\n");
    fprintf(f, "CONTROLTYPE = ADD\r\n");
    fprintf(f, "CONTROLTYPE = DROP\r\n");
}

static int parse_area_list(const char *areas_list, int *out, int max_out)
{
    if (!areas_list || !out || max_out <= 0)
        return 0;

    char buf[256];
    snprintf(buf, sizeof(buf), "%s", areas_list);
    char *token = NULL;
    char *saveptr = NULL;
    int count = 0;

    token = strtok_r(buf, ",", &saveptr);
    while (token && count < max_out)
    {
        str_trim(token);
        if (*token)
        {
            char *endptr = NULL;
            long value = strtol(token, &endptr, 10);
            if (endptr == token || *endptr != '\0' || value <= 0 || value > INT_MAX)
            {
                return -1;
            }
            out[count++] = (int)value;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    return count;
}

static int filter_msg_areas(BbsDb *db, const char *areas_list, DbMsgArea *out, int max_areas, bool verbose)
{
    if (!db || !out || max_areas <= 0)
        return 0;
    if (!areas_list || !*areas_list)
    {
        return db_msg_area_list(db, out, max_areas);
    }

    int ids[64];
    int id_count = parse_area_list(areas_list, ids, 64);
    if (id_count < 0)
    {
        if (verbose)
            fprintf(stderr, "Invalid area list: %s\n", areas_list);
        return -1;
    }

    int selected = 0;
    for (int i = 0; i < id_count && selected < max_areas; i++)
    {
        DbMsgArea area;
        if (db_msg_area_get(db, ids[i], &area))
        {
            out[selected++] = area;
        }
        else if (verbose)
        {
            fprintf(stderr, "Warning: message area %d not found\n", ids[i]);
        }
    }

    return selected;
}

int main(int argc, char **argv)
{
    const char *config_path = NULL;
    const char *output_path = NULL;
    const char *areas_list = NULL;
    int max_msgs = DEFAULT_MAX_MSGS;
    bool new_only = false;
    bool verbose = false;

    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"output", required_argument, 0, 'o'},
        {"areas", required_argument, 0, 'a'},
        {"max", required_argument, 0, 'm'},
        {"new-only", no_argument, 0, 'n'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "c:o:a:m:nvh", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'c':
            config_path = optarg;
            break;
        case 'o':
            output_path = optarg;
            break;
        case 'a':
            areas_list = optarg;
            break;
        case 'm':
            max_msgs = atoi(optarg);
            break;
        case 'n':
            new_only = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Error: Username required\n\n");
        print_usage(argv[0]);
        return 1;
    }

    const char *username = argv[optind];

    BbsConfig cfg;
    if (!cfg_load(config_path ? config_path : "conf/mutineer.conf", &cfg))
    {
        fprintf(stderr, "Failed to load config\n");
        return 2;
    }

    BbsDb *db = db_open(cfg.db_path);
    if (!db)
    {
        fprintf(stderr, "Failed to open database: %s\n", cfg.db_path);
        return 2;
    }

    DbUser user;
    if (!db_user_fetch(db, username, &user))
    {
        fprintf(stderr, "User not found: %s\n", username);
        db_close(db);
        return 1;
    }

    if (verbose)
    {
        printf("Generating QWK packet for: %s\n", user.handle);
    }

    DbMsgArea areas[64];
    int num_areas = filter_msg_areas(db, areas_list, areas, 64, verbose);
    if (num_areas < 0)
    {
        fprintf(stderr, "Invalid QWK area list: %s\n", areas_list ? areas_list : "");
        db_close(db);
        return 2;
    }
    if (num_areas == 0)
    {
        fprintf(stderr, "No message areas selected for QWK packet.\n");
        db_close(db);
        return 2;
    }

    char temp_dir[512];
    const char* tmpbase = getenv("TMPDIR");
    if (!tmpbase || !tmpbase[0]) tmpbase = cfg.data_path[0] ? cfg.data_path : "data";
    snprintf(temp_dir, sizeof(temp_dir), "%s/qwkgen_XXXXXX", tmpbase);
    if (!mkdtemp(temp_dir))
    {
        fprintf(stderr, "Failed to create temp directory\n");
        db_close(db);
        return 3;
    }

    char control_path[512], messages_path[512], door_path[512];
    snprintf(control_path, sizeof(control_path), "%s/CONTROL.DAT", temp_dir);
    snprintf(messages_path, sizeof(messages_path), "%s/MESSAGES.DAT", temp_dir);
    snprintf(door_path, sizeof(door_path), "%s/DOOR.ID", temp_dir);

    FILE *f = fopen(control_path, "w");
    if (f)
    {
        write_control_dat(f, &cfg, &user, num_areas, areas);
        fclose(f);
    }

    int msg_count = 0;
    f = fopen(messages_path, "wb");
    if (f)
    {
        msg_count = write_messages_dat(f, db, &user, areas, num_areas, max_msgs, new_only, verbose);
        fclose(f);
    }

    f = fopen(door_path, "w");
    if (f)
    {
        write_door_id(f, &cfg);
        fclose(f);
    }

    char qwk_path[512];
    if (output_path)
    {
        snprintf(qwk_path, sizeof(qwk_path), "%s", output_path);
    }
    else
    {
        snprintf(qwk_path, sizeof(qwk_path), "%s.QWK", user.handle);
    }

    char errbuf[256];
    if (!bbs_archive_create_zip_from_dir(temp_dir, qwk_path, errbuf, sizeof(errbuf)))
    {
        fprintf(stderr, "Failed to create QWK archive: %s\n", errbuf);
        bbs_remove_tree(temp_dir);
        db_close(db);
        return 3;
    }
    bbs_remove_tree(temp_dir);

    struct stat st;
    if (stat(qwk_path, &st) == 0)
    {
        if (verbose)
        {
            printf("Created: %s (%ld bytes, %d messages)\n", qwk_path, (long)st.st_size, msg_count);
        }
        else
        {
            printf("%s\n", qwk_path);
        }
    }

    db_close(db);
    return 0;
}
