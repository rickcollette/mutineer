/*
 * bucc.c - Buccaneer command-line interface
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Main entry point for the bucc toolchain.
 */

#define _POSIX_C_SOURCE 200809L
#include "../include/bucc_lexer.h"
#include "../include/bucc_parser.h"
#include "../include/bucc_ast.h"
#include "../include/bucc_semantic.h"
#include "../include/bucc_emit.h"
#include "../include/bucc_module.h"
#include "../include/bucc_vm.h"
#include "../include/bucc_diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

#define BUCC_VERSION "0.1.0"

extern int bucc_format_file(const char* input_path, const char* output_path,
                           bool uppercase, int indent_size);
extern int bucc_lint_file(const char* input_path, bool json_output);
extern int bucc_simulate(const char* module_path, const char* user_name,
                        int user_security, bool debug);

static bool make_temp_module_path(char* path, size_t path_size) {
    if (!path || path_size == 0) return false;
    const char* tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
    int n = snprintf(path, path_size, "%s/bucc_XXXXXX", tmpdir);
    if (n < 0 || (size_t)n >= path_size) return false;
    int fd = mkstemp(path);
    if (fd < 0) return false;
    close(fd);
    return true;
}

static void print_version(void) {
    printf("bucc %s - Buccaneer Language Toolchain\n", BUCC_VERSION);
    printf("Part of the Mutineer BBS system\n");
}

static void print_usage(const char* prog) {
    printf("Usage: %s <command> [options] <file>\n\n", prog);
    printf("Commands:\n");
    printf("  compile   Compile .bucc source to .bc bytecode\n");
    printf("  run       Compile and run a .bucc file\n");
    printf("  exec      Execute a .bc bytecode module\n");
    printf("  check     Check .bucc source for errors\n");
    printf("  format    Format .bucc source code\n");
    printf("  lint      Lint .bucc source for style issues\n");
    printf("  disasm    Disassemble a .bc bytecode module\n");
    printf("  version   Show version information\n");
    printf("  help      Show this help message\n");
    printf("\nOptions:\n");
    printf("  -o, --output <file>   Output file path\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -d, --debug           Enable debug mode\n");
    printf("  -u, --user <name>     Set user name for simulation\n");
    printf("  -s, --security <n>    Set security level for simulation\n");
    printf("  --json                Output in JSON format (lint)\n");
    printf("  --uppercase           Use uppercase keywords (format)\n");
    printf("  --indent <n>          Indentation size (format, default 4)\n");
    printf("\nExamples:\n");
    printf("  %s compile hello.bucc -o hello.bc\n", prog);
    printf("  %s run hello.bucc\n", prog);
    printf("  %s exec hello.bc --user TestUser\n", prog);
    printf("  %s format hello.bucc --uppercase\n", prog);
    printf("  %s lint hello.bucc --json\n", prog);
}

static char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);
    
    if (out_len) *out_len = read;
    return content;
}

static bucc_module_t* compile_source(const char* source, size_t len,
                                     const char* filename, bool verbose,
                                     bool debug, bucc_debug_map_t** debug_map_out) {
    bucc_diag_t* diag = bucc_diag_new();
    if (!diag) return NULL;
    
    if (verbose) {
        printf("Lexing %s...\n", filename);
    }
    
    uint32_t file_id = bucc_diag_add_file(diag, filename, source, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, source, len, file_id, diag);
    
    if (verbose) {
        printf("Parsing...\n");
    }
    
    bucc_parser_t parser;
    bucc_parser_init(&parser, &lexer, diag);
    
    bucc_node_t* ast = bucc_parse_module(&parser);
    
    if (bucc_diag_has_errors(diag)) {
        bucc_diag_print(diag, stderr);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    
    if (verbose) {
        printf("Semantic analysis...\n");
    }
    
    bucc_semantic_t* sem = bucc_semantic_new(diag);
    if (!sem) {
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    bucc_semantic_analyze(sem, ast);
    
    if (bucc_diag_has_errors(diag)) {
        bucc_diag_print(diag, stderr);
        bucc_semantic_free(sem);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    
    if (verbose) {
        printf("Emitting bytecode...\n");
    }
    
    bucc_emitter_t* emitter = bucc_emitter_new(sem, diag);
    if (!emitter) {
        bucc_semantic_free(sem);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    
    if (debug) {
        bucc_emitter_set_debug(emitter, true, filename);
    }
    
    bucc_emit_module(emitter, ast);
    
    if (bucc_diag_has_errors(diag)) {
        bucc_diag_print(diag, stderr);
        bucc_emitter_free(emitter);
        bucc_semantic_free(sem);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    
    if (verbose) {
        printf("Creating module...\n");
    }
    
    bucc_module_t* module = bucc_emitter_to_module(emitter);
    
    if (debug && debug_map_out) {
        *debug_map_out = bucc_emitter_get_debug_map(emitter);
    }
    
    bucc_emitter_free(emitter);
    bucc_semantic_free(sem);
    bucc_node_free(ast);
    bucc_diag_free(diag);
    
    return module;
}

static int cmd_compile(int argc, char** argv) {
    const char* output = NULL;
    bool verbose = false;
    bool debug = false;
    
    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"verbose", no_argument, 0, 'v'},
        {"debug", no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };
    
    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "o:vd", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o': output = optarg; break;
            case 'v': verbose = true; break;
            case 'd': debug = true; break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    const char* input = argv[optind];
    
    size_t len;
    char* source = read_file(input, &len);
    if (!source) {
        fprintf(stderr, "Error: Cannot read file: %s\n", input);
        return 1;
    }
    
    bucc_debug_map_t* debug_map = NULL;
    bucc_module_t* module = compile_source(source, len, input, verbose, debug, &debug_map);
    free(source);
    
    if (!module) {
        return 1;
    }
    
    char output_path[256];
    if (output) {
        strncpy(output_path, output, sizeof(output_path) - 1);
        output_path[sizeof(output_path) - 1] = '\0';
    } else {
        size_t input_len = strlen(input);
        if (input_len > 5 && strcmp(input + input_len - 5, ".bucc") == 0) {
            snprintf(output_path, sizeof(output_path), "%.*s.bc", (int)(input_len - 5), input);
        } else {
            snprintf(output_path, sizeof(output_path), "%s.bc", input);
        }
    }
    
    if (verbose) {
        printf("Writing %s...\n", output_path);
    }
    
    if (!bucc_module_save(module, output_path)) {
        fprintf(stderr, "Error: Cannot write output file: %s\n", output_path);
        bucc_module_free(module);
        bucc_debug_map_free(debug_map);
        return 1;
    }
    
    if (debug && debug_map) {
        char bmap_path[260];
        size_t out_len = strlen(output_path);
        if (out_len > 3 && strcmp(output_path + out_len - 3, ".bc") == 0) {
            snprintf(bmap_path, sizeof(bmap_path), "%.*s.bmap", (int)(out_len - 3), output_path);
        } else {
            snprintf(bmap_path, sizeof(bmap_path), "%s.bmap", output_path);
        }
        
        if (verbose) {
            printf("Writing debug map %s...\n", bmap_path);
        }
        
        if (!bucc_debug_map_save(debug_map, bmap_path)) {
            fprintf(stderr, "Warning: Cannot write debug map: %s\n", bmap_path);
        }
    }
    
    if (verbose) {
        printf("Compilation successful: %s\n", output_path);
    }
    
    bucc_module_free(module);
    bucc_debug_map_free(debug_map);
    return 0;
}

static int cmd_run(int argc, char** argv) {
    const char* user = "TestUser";
    int security = 100;
    bool debug = false;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"user", required_argument, 0, 'u'},
        {"security", required_argument, 0, 's'},
        {"debug", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    
    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "u:s:dv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'u': user = optarg; break;
            case 's': security = atoi(optarg); break;
            case 'd': debug = true; break;
            case 'v': verbose = true; break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    const char* input = argv[optind];
    
    size_t len;
    char* source = read_file(input, &len);
    if (!source) {
        fprintf(stderr, "Error: Cannot read file: %s\n", input);
        return 1;
    }
    
    bucc_debug_map_t* debug_map = NULL;
    bucc_module_t* module = compile_source(source, len, input, verbose, debug, &debug_map);
    free(source);
    
    if (!module) {
        return 1;
    }
    
    char temp_path[512];
    if (!make_temp_module_path(temp_path, sizeof(temp_path))) {
        fprintf(stderr, "Error: Cannot create temporary module path: %s\n", strerror(errno));
        bucc_module_free(module);
        bucc_debug_map_free(debug_map);
        return 1;
    }
    
    if (!bucc_module_save(module, temp_path)) {
        fprintf(stderr, "Error: Cannot write temporary module\n");
        unlink(temp_path);
        bucc_module_free(module);
        bucc_debug_map_free(debug_map);
        return 1;
    }
    
    (void)debug_map;
    
    bucc_module_free(module);
    
    int result = bucc_simulate(temp_path, user, security, debug);
    
    unlink(temp_path);
    
    return result;
}

static int cmd_exec(int argc, char** argv) {
    const char* user = "TestUser";
    int security = 100;
    bool debug = false;
    
    static struct option long_options[] = {
        {"user", required_argument, 0, 'u'},
        {"security", required_argument, 0, 's'},
        {"debug", no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };
    
    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "u:s:d", long_options, NULL)) != -1) {
        switch (opt) {
            case 'u': user = optarg; break;
            case 's': security = atoi(optarg); break;
            case 'd': debug = true; break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    return bucc_simulate(argv[optind], user, security, debug);
}

static int cmd_check(int argc, char** argv) {
    bool verbose = false;
    
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    
    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "v", long_options, NULL)) != -1) {
        switch (opt) {
            case 'v': verbose = true; break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    const char* input = argv[optind];
    
    size_t len;
    char* source = read_file(input, &len);
    if (!source) {
        fprintf(stderr, "Error: Cannot read file: %s\n", input);
        return 1;
    }
    
    bucc_diag_t* diag = bucc_diag_new();
    if (!diag) {
        free(source);
        return 1;
    }
    
    uint32_t file_id = bucc_diag_add_file(diag, input, source, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, source, len, file_id, diag);
    
    bucc_parser_t parser;
    bucc_parser_init(&parser, &lexer, diag);
    
    bucc_node_t* ast = bucc_parse_module(&parser);
    
    if (!bucc_diag_has_errors(diag)) {
        bucc_semantic_t* sem = bucc_semantic_new(diag);
        if (sem) {
            bucc_semantic_analyze(sem, ast);
            bucc_semantic_free(sem);
        }
    }
    
    uint32_t error_count = bucc_diag_error_count(diag);
    
    if (error_count > 0 || verbose) {
        bucc_diag_print(diag, stderr);
    }
    
    if (error_count == 0) {
        printf("%s: OK\n", input);
    }
    
    int result = error_count > 0 ? 1 : 0;
    
    bucc_node_free(ast);
    bucc_diag_free(diag);
    free(source);
    
    return result;
}

static int cmd_format(int argc, char** argv) {
    const char* output = NULL;
    bool uppercase = false;
    int indent = 4;
    
    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"uppercase", no_argument, 0, 'U'},
        {"indent", required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };
    
    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "o:Ui:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o': output = optarg; break;
            case 'U': uppercase = true; break;
            case 'i': indent = atoi(optarg); break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    return bucc_format_file(argv[optind], output, uppercase, indent);
}

static int cmd_lint(int argc, char** argv) {
    bool json = false;
    
    static struct option long_options[] = {
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };
    
    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "j", long_options, NULL)) != -1) {
        switch (opt) {
            case 'j': json = true; break;
            default: return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    return bucc_lint_file(argv[optind], json);
}

static int cmd_disasm(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
    }
    
    const char* input = argv[1];
    
    bucc_module_t* module = bucc_module_load(input);
    if (!module) {
        fprintf(stderr, "Error: Cannot load module: %s\n", input);
        return 1;
    }
    
    bucc_module_disasm(module, stdout);
    
    bucc_module_free(module);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char* cmd = argv[1];
    
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 ||
        strcmp(cmd, "-V") == 0) {
        print_version();
        return 0;
    }
    
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 ||
        strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (strcmp(cmd, "compile") == 0) {
        return cmd_compile(argc - 1, argv + 1);
    }
    
    if (strcmp(cmd, "run") == 0) {
        return cmd_run(argc - 1, argv + 1);
    }
    
    if (strcmp(cmd, "exec") == 0) {
        return cmd_exec(argc - 1, argv + 1);
    }
    
    if (strcmp(cmd, "check") == 0) {
        return cmd_check(argc - 1, argv + 1);
    }
    
    if (strcmp(cmd, "format") == 0 || strcmp(cmd, "fmt") == 0) {
        return cmd_format(argc - 1, argv + 1);
    }
    
    if (strcmp(cmd, "lint") == 0) {
        return cmd_lint(argc - 1, argv + 1);
    }
    
    if (strcmp(cmd, "disasm") == 0 || strcmp(cmd, "dis") == 0) {
        return cmd_disasm(argc - 1, argv + 1);
    }
    
    fprintf(stderr, "Unknown command: %s\n", cmd);
    fprintf(stderr, "Run '%s help' for usage information\n", argv[0]);
    return 1;
}
