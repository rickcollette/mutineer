/*
 * simulator.c - Buccaneer standalone simulator
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Runs .bc bytecode modules with simulated BBS environment.
 */

#define _POSIX_C_SOURCE 200809L
#include "../include/bucc_vm.h"
#include "../include/bucc_module.h"
#include "../include/bucc_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct sim_context {
    int term_width;
    int term_height;
    char* user_name;
    int user_id;
    int user_security;
    int time_left;
    bucc_map_t* kv_store;
    bool ansi_enabled;
} sim_context_t;

static void sim_print(void* ctx, const char* text) {
    (void)ctx;
    printf("%s", text);
    fflush(stdout);
}

static void sim_println(void* ctx, const char* text) {
    (void)ctx;
    printf("%s\n", text);
    fflush(stdout);
}

static void sim_cls(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    if (sim->ansi_enabled) {
        printf("\033[2J\033[H");
    } else {
        for (int i = 0; i < sim->term_height; i++) {
            printf("\n");
        }
    }
    fflush(stdout);
}

static void sim_color(void* ctx, int fg, int bg) {
    sim_context_t* sim = (sim_context_t*)ctx;
    if (!sim->ansi_enabled) return;
    
    if (fg >= 0 && fg <= 15) {
        if (fg < 8) {
            printf("\033[%dm", 30 + fg);
        } else {
            printf("\033[%d;1m", 30 + (fg - 8));
        }
    }
    if (bg >= 0 && bg <= 7) {
        printf("\033[%dm", 40 + bg);
    }
    fflush(stdout);
}

static void sim_gotoxy(void* ctx, int x, int y) {
    sim_context_t* sim = (sim_context_t*)ctx;
    if (sim->ansi_enabled) {
        printf("\033[%d;%dH", y, x);
        fflush(stdout);
    }
}

static char* sim_getkey(void* ctx) {
    (void)ctx;
    char c = getchar();
    char* result = malloc(2);
    result[0] = c;
    result[1] = '\0';
    return result;
}

static char* sim_input(void* ctx, const char* prompt, int maxlen) {
    (void)ctx;
    
    if (prompt && *prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }
    
    char* buf = malloc(maxlen + 1);
    if (!buf) return NULL;
    
    if (fgets(buf, maxlen + 1, stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        return buf;
    }
    
    free(buf);
    return strdup("");
}

static char* sim_input_password(void* ctx, const char* prompt) {
    return sim_input(ctx, prompt, 255);
}

static void sim_pause(void* ctx, const char* prompt) {
    if (prompt && *prompt) {
        printf("%s", prompt);
    } else {
        printf("Press any key to continue...");
    }
    fflush(stdout);
    
    char* key = sim_getkey(ctx);
    free(key);
    printf("\n");
}

static int sim_get_width(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return sim->term_width;
}

static int sim_get_height(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return sim->term_height;
}

static bool sim_supports_ansi(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return sim->ansi_enabled;
}

static const char* sim_user_name(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return sim->user_name;
}

static const char* sim_user_alias(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return sim->user_name;
}

static int64_t sim_user_id(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return sim->user_id;
}

static int sim_user_security(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return sim->user_security;
}

static int sim_user_time_left(void* ctx) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return sim->time_left;
}

static int sim_user_flags(void* ctx) {
    (void)ctx;
    return 0;
}

static char* sim_kv_get(void* ctx, const char* key, const char* default_val) {
    sim_context_t* sim = (sim_context_t*)ctx;
    
    bucc_value_t* val = bucc_map_get_cstr(sim->kv_store, key);
    if (val && val->kind == BUCC_VAL_STRING && val->as.str) {
        return strdup(val->as.str->data);
    }
    
    return strdup(default_val ? default_val : "");
}

static void sim_kv_set(void* ctx, const char* key, const char* value) {
    sim_context_t* sim = (sim_context_t*)ctx;
    bucc_map_set_cstr(sim->kv_store, key, bucc_make_string(value));
}

static void sim_kv_delete(void* ctx, const char* key) {
    sim_context_t* sim = (sim_context_t*)ctx;
    bucc_map_delete_cstr(sim->kv_store, key);
}

static bool sim_kv_exists(void* ctx, const char* key) {
    sim_context_t* sim = (sim_context_t*)ctx;
    return bucc_map_has_cstr(sim->kv_store, key);
}

static char* sim_text_read_all(void* ctx, const char* path) {
    (void)ctx;
    
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
    
    return content;
}

static bucc_array_t* sim_text_read_lines(void* ctx, const char* path) {
    char* content = sim_text_read_all(ctx, path);
    if (!content) return NULL;
    
    bucc_array_t* lines = bucc_array_new(16);
    
    char* line = strtok(content, "\n");
    while (line) {
        bucc_array_push(lines, bucc_make_string(line));
        line = strtok(NULL, "\n");
    }
    
    free(content);
    return lines;
}

static bool sim_text_write_all(void* ctx, const char* path, const char* content) {
    (void)ctx;
    
    FILE* f = fopen(path, "w");
    if (!f) return false;
    
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    
    return written == len;
}

static bool sim_text_append(void* ctx, const char* path, const char* content) {
    (void)ctx;
    
    FILE* f = fopen(path, "a");
    if (!f) return false;
    
    fprintf(f, "%s", content);
    fclose(f);
    
    return true;
}

static bool sim_text_exists(void* ctx, const char* path) {
    (void)ctx;
    return access(path, F_OK) == 0;
}

static bucc_term_api_t sim_term_api = {
    .print = sim_print,
    .println = sim_println,
    .cls = sim_cls,
    .color = sim_color,
    .gotoxy = sim_gotoxy,
    .getkey = sim_getkey,
    .input = sim_input,
    .input_password = sim_input_password,
    .pause = sim_pause,
    .get_width = sim_get_width,
    .get_height = sim_get_height,
    .supports_ansi = sim_supports_ansi
};

static bucc_user_api_t sim_user_api = {
    .get_name = sim_user_name,
    .get_alias = sim_user_alias,
    .get_id = sim_user_id,
    .get_security = sim_user_security,
    .get_time_left = sim_user_time_left,
    .get_flags = sim_user_flags
};

static bucc_kv_api_t sim_kv_api = {
    .get = sim_kv_get,
    .set = sim_kv_set,
    .delete_key = sim_kv_delete,
    .exists = sim_kv_exists
};

static bucc_text_api_t sim_text_api = {
    .read_all = sim_text_read_all,
    .read_lines = sim_text_read_lines,
    .write_all = sim_text_write_all,
    .append = sim_text_append,
    .exists = sim_text_exists
};

int bucc_simulate(const char* module_path, const char* user_name, 
                  int user_security, bool debug) {
    bucc_module_t* module = bucc_module_load(module_path);
    if (!module) {
        fprintf(stderr, "Error: Cannot load module: %s\n", module_path);
        return 1;
    }
    
    int term_width = 80;
    int term_height = 24;
    
    sim_context_t sim = {
        .term_width = term_width,
        .term_height = term_height,
        .user_name = strdup(user_name ? user_name : "TestUser"),
        .user_id = 1,
        .user_security = user_security,
        .time_left = 60,
        .kv_store = bucc_map_new(16),
        .ansi_enabled = isatty(STDOUT_FILENO)
    };
    
    bucc_host_context_t* host_ctx = bucc_host_context_new();
    bucc_host_set_term_api(host_ctx, &sim_term_api, &sim);
    bucc_host_set_user_api(host_ctx, &sim_user_api, &sim);
    bucc_host_set_kv_api(host_ctx, &sim_kv_api, &sim);
    bucc_host_set_text_api(host_ctx, &sim_text_api, &sim);
    
    bucc_vm_t* vm = bucc_vm_new(module);
    if (!vm) {
        fprintf(stderr, "Error: Cannot create VM\n");
        bucc_module_free(module);
        bucc_host_context_free(host_ctx);
        return 1;
    }
    
    bucc_vm_set_host_handler(vm, bucc_host_dispatch, host_ctx);
    vm->debug_enabled = debug;
    
    bucc_limits_t limits = {
        .max_instructions = 10000000,
        .max_stack_depth = 1024,
        .max_heap_bytes = 16 * 1024 * 1024,
        .max_string_bytes = 1024 * 1024,
        .max_host_calls = 100000
    };
    bucc_vm_set_limits(vm, &limits);
    
    if (debug) {
        printf("=== Buccaneer Simulator ===\n");
        printf("Module: %s\n", module_path);
        printf("User: %s (security %d)\n", sim.user_name, sim.user_security);
        printf("Terminal: %dx%d\n", sim.term_width, sim.term_height);
        printf("===========================\n\n");
    }
    
    bucc_vm_status_t status = bucc_vm_run(vm);
    
    if (status != VM_OK && status != VM_HALT && status != VM_CHAIN) {
        fprintf(stderr, "\nVM Error: %s\n", 
                vm->error_message ? vm->error_message : "Unknown error");
        
        if (debug) {
            printf("\nVM State:\n");
            printf("  IP: %u\n", vm->ip);
            printf("  SP: %u\n", vm->sp);
            printf("  FP: %u\n", vm->fp);
            printf("  Instructions: %lu\n", (unsigned long)vm->counters.instructions);
            printf("  Host calls: %lu\n", (unsigned long)vm->counters.host_calls);
        }
    }
    
    if (debug) {
        printf("\n=== Simulation Complete ===\n");
        printf("Instructions executed: %lu\n", (unsigned long)vm->counters.instructions);
        printf("Host calls: %lu\n", (unsigned long)vm->counters.host_calls);
        printf("Stack depth: %lu\n", (unsigned long)vm->counters.stack_depth);
    }
    
    bucc_vm_free(vm);
    bucc_module_free(module);
    bucc_host_context_free(host_ctx);
    bucc_map_release(sim.kv_store);
    free(sim.user_name);
    
    return (status == VM_OK || status == VM_HALT || status == VM_CHAIN) ? 0 : 1;
}
