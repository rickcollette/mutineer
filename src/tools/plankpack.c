/*
 * plankpack - PLANK Bundle Tool
 * Create, inspect, extract, verify, and import .plb and .plp bundles
 */

#include "plank/plank.h"
#include "bbs_db.h"
#include "bbs_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#define PLANKPACK_VERSION "1.0.0"

/* ============================================================================
 * OUTPUT HELPERS
 * ============================================================================ */

static void print_hex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
}

static void print_hex_short(const uint8_t* data, size_t len) {
    size_t show = len > 8 ? 8 : len;
    for (size_t i = 0; i < show; i++) {
        printf("%02x", data[i]);
    }
    if (len > 8) printf("...");
}

static const char* bundle_type_name(plank_bundle_type_t type) {
    switch (type) {
        case PLANK_BUNDLE_LINK_SYNC: return "LINK_SYNC";
        case PLANK_BUNDLE_USER_EXPORT: return "USER_EXPORT";
        case PLANK_BUNDLE_USER_REPLY: return "USER_REPLY";
        case PLANK_BUNDLE_ADMIN_TRANSFER: return "ADMIN_TRANSFER";
        default: return "UNKNOWN";
    }
}

static const char* record_type_name(plank_record_type_t type) {
    switch (type) {
        case PLANK_RECORD_OBJECT: return "OBJECT";
        case PLANK_RECORD_ATTACHMENT: return "ATTACHMENT";
        case PLANK_RECORD_CHECKPOINT: return "CHECKPOINT";
        default: return "UNKNOWN";
    }
}

/* ============================================================================
 * COMMANDS
 * ============================================================================ */

static int cmd_create(int argc, char* argv[]) {
    const char* output = NULL;
    const char* input_dir = NULL;
    plank_bundle_type_t type = PLANK_BUNDLE_LINK_SYNC;
    bool compress = true;
    
    static struct option opts[] = {
        {"output",    required_argument, 0, 'o'},
        {"input",     required_argument, 0, 'i'},
        {"type",      required_argument, 0, 't'},
        {"no-compress", no_argument,     0, 'C'},
        {0, 0, 0, 0}
    };
    
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:i:t:C", opts, NULL)) != -1) {
        switch (opt) {
            case 'o': output = optarg; break;
            case 'i': input_dir = optarg; break;
            case 't':
                if (strcmp(optarg, "sync") == 0) type = PLANK_BUNDLE_LINK_SYNC;
                else if (strcmp(optarg, "user") == 0) type = PLANK_BUNDLE_USER_EXPORT;
                else if (strcmp(optarg, "reply") == 0) type = PLANK_BUNDLE_USER_REPLY;
                else {
                    fprintf(stderr, "Unknown bundle type: %s\n", optarg);
                    return 1;
                }
                break;
            case 'C': compress = false; break;
            default: return 1;
        }
    }
    
    if (!output || !input_dir) {
        fprintf(stderr, "Error: --output and --input are required\n");
        return 1;
    }
    
    printf("Creating bundle: %s\n", output);
    printf("  Type: %s\n", bundle_type_name(type));
    printf("  Input: %s\n", input_dir);
    printf("  Compression: %s\n", compress ? "enabled" : "disabled");
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        output, type, node_id, "local@local");
    
    if (!writer) {
        fprintf(stderr, "Error: Failed to create bundle writer: %s\n", plank_last_error());
        return 1;
    }
    
    DIR* dir = opendir(input_dir);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open input directory: %s\n", strerror(errno));
        plank_bundle_writer_close(writer);
        return 1;
    }
    
    int file_count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", input_dir, entry->d_name);
        
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        
        uint8_t* data = malloc(st.st_size);
        if (!data) {
            fclose(f);
            continue;
        }
        
        size_t read_len = fread(data, 1, st.st_size, f);
        fclose(f);
        
        if (read_len != (size_t)st.st_size) {
            free(data);
            continue;
        }
        
        const char* ext = strrchr(entry->d_name, '.');
        if (ext && strcmp(ext, ".cbor") == 0) {
            plank_object_t* obj = plank_object_decode(data, read_len);
            if (obj) {
                if (plank_bundle_writer_add_object(writer, obj)) {
                    printf("  Added object: %s\n", entry->d_name);
                    file_count++;
                }
                plank_object_free(obj);
            }
        } else {
            uint8_t att_id[PLANK_ATTACHMENT_ID_SIZE];
            plank_crypto_sha256(data, read_len, att_id);
            
            if (plank_bundle_writer_add_attachment(writer, att_id, data, read_len, compress)) {
                printf("  Added attachment: %s\n", entry->d_name);
                file_count++;
            }
        }
        
        free(data);
    }
    
    closedir(dir);
    
    if (!plank_bundle_writer_finalize(writer, NULL)) {
        fprintf(stderr, "Error: Failed to finalize bundle: %s\n", plank_last_error());
        plank_bundle_writer_close(writer);
        return 1;
    }
    
    uint8_t bundle_id[PLANK_BUNDLE_ID_SIZE];
    plank_bundle_writer_get_id(writer, bundle_id);
    
    printf("\nBundle created successfully.\n");
    printf("  Bundle ID: ");
    print_hex(bundle_id, PLANK_BUNDLE_ID_SIZE);
    printf("\n");
    printf("  Files: %d\n", file_count);
    
    plank_bundle_writer_close(writer);
    return 0;
}

static int cmd_inspect(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: Bundle file required\n");
        return 1;
    }
    
    const char* path = argv[1];
    bool verbose = false;
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }
    
    plank_bundle_reader_t* reader = plank_bundle_reader_open(path);
    if (!reader) {
        fprintf(stderr, "Error: Failed to open bundle: %s\n", plank_last_error());
        return 1;
    }
    
    const plank_bundle_manifest_t* manifest = plank_bundle_reader_manifest(reader);
    
    printf("Bundle: %s\n", path);
    printf("========================================\n\n");
    
    printf("Manifest:\n");
    printf("  Type:        %s\n", bundle_type_name(manifest->bundle_type));
    printf("  Bundle ID:   ");
    print_hex(manifest->bundle_id, PLANK_BUNDLE_ID_SIZE);
    printf("\n");
    printf("  Source Node: ");
    print_hex(manifest->source_node_id, PLANK_NODE_ID_SIZE);
    printf("\n");
    printf("  Source Addr: %s\n", manifest->source_node_addr);
    printf("  Created:     %llu\n", (unsigned long long)manifest->created_at);
    printf("  Objects:     %u\n", manifest->object_count);
    printf("  Attachments: %u\n", manifest->attachment_count);
    printf("  Records:     %u\n", manifest->record_count);
    
    uint32_t record_count = plank_bundle_reader_record_count(reader);
    printf("\nEntries (%u):\n", record_count);
    
    for (uint32_t i = 0; i < record_count; i++) {
        plank_bundle_record_t record;
        if (!plank_bundle_reader_get_record(reader, i, &record)) continue;
        
        printf("  [%u] %s ", i, record_type_name(record.record_type));
        print_hex_short(record.record_id, 24);
        printf(" offset=%llu len=%llu",
               (unsigned long long)record.offset, (unsigned long long)record.encoded_len);
        
        if (record.decoded_len != record.encoded_len) {
            printf(" compressed=%llu->%llu", 
                   (unsigned long long)record.encoded_len,
                   (unsigned long long)record.decoded_len);
        }
        
        printf("\n");
        
        if (verbose && record.record_type == PLANK_RECORD_OBJECT) {
            uint8_t* payload = NULL;
            size_t payload_len = 0;
            
            if (plank_bundle_reader_load_payload(reader, i, &payload, &payload_len)) {
                plank_object_t* obj = plank_object_decode(payload, payload_len);
                if (obj) {
                    printf("       Class: %s\n", plank_object_class_name(obj->object_class));
                    printf("       Origin: %s\n", obj->origin_node_addr);
                    printf("       Created: %llu\n", (unsigned long long)obj->created_at);
                    plank_object_free(obj);
                }
                free(payload);
            }
        }
    }
    
    plank_bundle_reader_close(reader);
    return 0;
}

static int cmd_extract(int argc, char* argv[]) {
    const char* bundle_path = NULL;
    const char* output_dir = NULL;
    
    static struct option opts[] = {
        {"output", required_argument, 0, 'o'},
        {0, 0, 0, 0}
    };
    
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:", opts, NULL)) != -1) {
        switch (opt) {
            case 'o': output_dir = optarg; break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: Bundle file required\n");
        return 1;
    }
    bundle_path = argv[optind];
    
    if (!output_dir) {
        fprintf(stderr, "Error: --output directory required\n");
        return 1;
    }
    
    if (mkdir(output_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Cannot create output directory: %s\n", strerror(errno));
        return 1;
    }
    
    plank_bundle_reader_t* reader = plank_bundle_reader_open(bundle_path);
    if (!reader) {
        fprintf(stderr, "Error: Failed to open bundle: %s\n", plank_last_error());
        return 1;
    }
    
    printf("Extracting bundle to %s\n", output_dir);
    
    int extracted = 0;
    uint32_t record_count = plank_bundle_reader_record_count(reader);
    
    for (uint32_t i = 0; i < record_count; i++) {
        plank_bundle_record_t record;
        if (!plank_bundle_reader_get_record(reader, i, &record)) continue;
        
        uint8_t* payload = NULL;
        size_t payload_len = 0;
        
        if (!plank_bundle_reader_load_payload(reader, i, &payload, &payload_len)) {
            fprintf(stderr, "Warning: Failed to load entry %u\n", i);
            continue;
        }
        
        char filename[128];
        char id_hex[65];
        plank_crypto_to_hex(record.record_id, 24, id_hex);
        id_hex[48] = '\0';
        
        if (record.record_type == PLANK_RECORD_OBJECT) {
            snprintf(filename, sizeof(filename), "%s/%s.cbor", output_dir, id_hex);
        } else {
            snprintf(filename, sizeof(filename), "%s/%s.bin", output_dir, id_hex);
        }
        
        FILE* f = fopen(filename, "wb");
        if (f) {
            fwrite(payload, 1, payload_len, f);
            fclose(f);
            printf("  Extracted: %s\n", filename);
            extracted++;
        } else {
            fprintf(stderr, "Warning: Cannot write %s: %s\n", filename, strerror(errno));
        }
        
        free(payload);
    }
    
    printf("\nExtracted %d entries.\n", extracted);
    
    plank_bundle_reader_close(reader);
    return 0;
}

static int cmd_verify(int argc, char* argv[]) {
    const char* bundle_path = NULL;
    const char* pubkey_hex = NULL;
    
    static struct option opts[] = {
        {"key", required_argument, 0, 'k'},
        {0, 0, 0, 0}
    };
    
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "k:", opts, NULL)) != -1) {
        switch (opt) {
            case 'k': pubkey_hex = optarg; break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: Bundle file required\n");
        return 1;
    }
    bundle_path = argv[optind];
    
    plank_bundle_reader_t* reader = plank_bundle_reader_open(bundle_path);
    if (!reader) {
        fprintf(stderr, "Error: Failed to open bundle: %s\n", plank_last_error());
        return 1;
    }
    
    printf("Verifying bundle: %s\n", bundle_path);
    
    if (pubkey_hex) {
        uint8_t pubkey[PLANK_PUBKEY_SIZE];
        if (strlen(pubkey_hex) != PLANK_PUBKEY_SIZE * 2) {
            fprintf(stderr, "Error: Invalid public key length\n");
            plank_bundle_reader_close(reader);
            return 1;
        }
        
        plank_crypto_from_hex(pubkey_hex, pubkey, PLANK_PUBKEY_SIZE);
        
        printf("  Bundle signature: ");
        if (plank_bundle_reader_verify(reader, pubkey)) {
            printf("VALID\n");
        } else {
            printf("INVALID\n");
            plank_bundle_reader_close(reader);
            return 1;
        }
    } else {
        printf("  Bundle signature: (no key provided, skipping)\n");
    }
    
    int valid_count = 0;
    int invalid_count = 0;
    uint32_t record_count = plank_bundle_reader_record_count(reader);
    
    for (uint32_t i = 0; i < record_count; i++) {
        plank_bundle_record_t record;
        if (!plank_bundle_reader_get_record(reader, i, &record)) continue;
        
        if (record.record_type != PLANK_RECORD_OBJECT) continue;
        
        uint8_t* payload = NULL;
        size_t payload_len = 0;
        
        if (!plank_bundle_reader_load_payload(reader, i, &payload, &payload_len)) {
            invalid_count++;
            continue;
        }
        
        plank_object_t* obj = plank_object_decode(payload, payload_len);
        if (!obj) {
            invalid_count++;
            free(payload);
            continue;
        }
        
        if (plank_object_verify_id(obj)) {
            valid_count++;
        } else {
            printf("  Object %u: ID verification FAILED\n", i);
            invalid_count++;
        }
        
        plank_object_free(obj);
        free(payload);
    }
    
    printf("\nVerification complete.\n");
    printf("  Valid objects:   %d\n", valid_count);
    printf("  Invalid objects: %d\n", invalid_count);
    
    plank_bundle_reader_close(reader);
    return invalid_count > 0 ? 1 : 0;
}

static int cmd_import(int argc, char* argv[]) {
    const char* bundle_path = NULL;
    const char* db_path = NULL;
    
    static struct option opts[] = {
        {"database", required_argument, 0, 'd'},
        {0, 0, 0, 0}
    };
    
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "d:", opts, NULL)) != -1) {
        switch (opt) {
            case 'd': db_path = optarg; break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: Bundle file required\n");
        return 1;
    }
    bundle_path = argv[optind];
    
    if (!db_path) {
        fprintf(stderr, "Error: --database is required\n");
        return 1;
    }
    
    BbsDb* db = db_open(db_path);
    if (!db) {
        fprintf(stderr, "Error: Failed to open database: %s\n", db_path);
        return 1;
    }
    
    plank_store_t* store = plank_store_open(db);
    if (!store) {
        fprintf(stderr, "Error: Failed to initialize store\n");
        db_close(db);
        return 1;
    }
    
    printf("Importing bundle: %s\n", bundle_path);
    
    plank_bundle_import_result_t result;
    memset(&result, 0, sizeof(result));
    
    if (!plank_bundle_import(store, bundle_path, 0, NULL, &result)) {
        fprintf(stderr, "Error: Import failed: %s\n", result.error);
        plank_store_close(store);
        db_close(db);
        return 1;
    }
    
    printf("\nImport complete.\n");
    printf("  Objects accepted:    %d\n", result.objects_accepted);
    printf("  Objects duplicate:   %d\n", result.objects_duplicate);
    printf("  Objects rejected:    %d\n", result.objects_rejected);
    printf("  Objects quarantined: %d\n", result.objects_quarantined);
    printf("  Attachments stored:  %d\n", result.attachments_stored);
    
    plank_store_close(store);
    db_close(db);
    return 0;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s <command> [options]\n", prog);
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  create   Create a new bundle from files\n");
    fprintf(stderr, "  inspect  Show bundle contents\n");
    fprintf(stderr, "  extract  Extract bundle contents to directory\n");
    fprintf(stderr, "  verify   Verify bundle integrity and signatures\n");
    fprintf(stderr, "  import   Import bundle into database\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -h, --help     Show this help\n");
    fprintf(stderr, "  -V, --version  Show version\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("plankpack %s (PLANK protocol %d)\n",
               PLANKPACK_VERSION, PLANK_PROTOCOL_VERSION);
        return 0;
    }
    
    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }
    
    const char* command = argv[1];
    int result = 1;
    
    if (strcmp(command, "create") == 0) {
        result = cmd_create(argc - 1, argv + 1);
    } else if (strcmp(command, "inspect") == 0) {
        result = cmd_inspect(argc - 1, argv + 1);
    } else if (strcmp(command, "extract") == 0) {
        result = cmd_extract(argc - 1, argv + 1);
    } else if (strcmp(command, "verify") == 0) {
        result = cmd_verify(argc - 1, argv + 1);
    } else if (strcmp(command, "import") == 0) {
        result = cmd_import(argc - 1, argv + 1);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
    }
    
    plank_shutdown();
    return result;
}
