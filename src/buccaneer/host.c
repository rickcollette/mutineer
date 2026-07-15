/*
 * host.c - Buccaneer host bridge implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_host.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

static int strcasecmp_local(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bucc_host_function_t host_functions[] = {
    { "TERM", "PRINT",    "term.output", NULL, 1, 1, false },
    { "TERM", "PRINTLN",  "term.output", NULL, 0, 1, false },
    { "TERM", "CLS",      "term.output", NULL, 0, 0, false },
    { "TERM", "COLOR",    "term.output", NULL, 1, 2, false },
    { "TERM", "GOTOXY",   "term.output", NULL, 2, 2, false },
    { "TERM", "GETKEY",   "term.input",  NULL, 0, 0, false },
    { "TERM", "INPUT",    "term.input",  NULL, 0, 2, false },
    { "TERM", "INPUT_PASSWORD", "term.input", NULL, 0, 1, false },
    { "TERM", "PAUSE",    "term.input",  NULL, 0, 1, false },
    { "TERM", "WIDTH",    "term.query",  NULL, 0, 0, false },
    { "TERM", "HEIGHT",   "term.query",  NULL, 0, 0, false },
    { "TERM", "SUPPORTS_ANSI", "term.query", NULL, 0, 0, false },
    
    { "USER", "NAME",     "user.read",   NULL, 0, 0, false },
    { "USER", "ALIAS",    "user.read",   NULL, 0, 0, false },
    { "USER", "ID",       "user.read",   NULL, 0, 0, false },
    { "USER", "SECURITY", "user.read",   NULL, 0, 0, false },
    { "USER", "TIMELEFT", "user.read",   NULL, 0, 0, false },
    { "USER", "TIME_LEFT", "user.read",  NULL, 0, 0, false },
    { "USER", "TIME_REMAINING", "user.read", NULL, 0, 0, false },
    { "USER", "FLAGS",    "user.read",   NULL, 0, 0, false },
    
    { "DATA", "INSERT",   "data.write",  NULL, 2, 2, true  },
    { "DATA", "UPDATE",   "data.write",  NULL, 3, 3, true  },
    { "DATA", "DELETE",   "data.write",  NULL, 2, 2, true  },
    { "DATA", "GET",      "data.read",   NULL, 2, 2, false },
    { "DATA", "FIND",     "data.read",   NULL, 2, 5, false },
    { "DATA", "COUNT",    "data.read",   NULL, 2, 2, false },
    { "DATA", "BEGIN",    "data.write",  NULL, 0, 0, false },
    { "DATA", "COMMIT",   "data.write",  NULL, 0, 0, false },
    { "DATA", "ROLLBACK", "data.write",  NULL, 0, 0, false },
    
    { "KV", "GET",        "kv.read",     NULL, 1, 2, false },
    { "KV", "SET",        "kv.write",    NULL, 2, 2, true  },
    { "KV", "DELETE",     "kv.write",    NULL, 1, 1, true  },
    { "KV", "EXISTS",     "kv.read",     NULL, 1, 1, false },
    
    { "APP", "GET",       NULL,          NULL, 1, 2, false },
    { "APP", "SET",       NULL,          NULL, 2, 2, false },
    
    { "SHARED", "GET",    "shared.read", NULL, 1, 2, false },
    { "SHARED", "SET",    "shared.write",NULL, 2, 2, true  },
    { "SHARED", "CAS",    "shared.write",NULL, 3, 3, true  },
    
    { "TEXT", "READALL",  "text.read",   NULL, 1, 1, false },
    { "TEXT", "READLINES","text.read",   NULL, 1, 1, false },
    { "TEXT", "WRITEALL", "text.write",  NULL, 2, 2, true  },
    { "TEXT", "APPEND",   "text.write",  NULL, 2, 2, true  },
    { "TEXT", "EXISTS",   "text.read",   NULL, 1, 1, false },
    
    { "BBS", "SENDMSG",   "bbs.message", NULL, 2, 2, true  },
    { "BBS", "ONLINE",    "bbs.query",   NULL, 0, 0, false },
    { "BBS", "NODE",      "bbs.query",   NULL, 0, 0, false },

    { "USERS", "FIND",    "user.read",   NULL, 1, 2, true  },
    { "USERS", "GET",     "user.read",   NULL, 1, 1, false },

    { "MSG", "READ",      "data.read",   NULL, 2, 2, false },
    { "MSG", "LIST",      "data.read",   NULL, 1, 3, false },
    { "MSG", "POST",      "data.write",  NULL, 2, 2, true  },

    { "FILE", "LIST",     "data.read",   NULL, 1, 3, false },
    { "FILE", "INFO",     "data.read",   NULL, 2, 2, false },
    
    { "SYS", "NOW",       NULL,          NULL, 0, 0, false },
    { "SYS", "TODAY",     NULL,          NULL, 0, 0, false },
    { "SYS", "SLEEP",     NULL,          NULL, 1, 1, false },
    
    { "SESSION", "GET",   "session.read",NULL, 1, 2, false },
    { "SESSION", "SET",   "session.write",NULL, 2, 2, false },
    { "SESSION", "NODE",  "session.read",NULL, 0, 0, false },
    { "SESSION", "ELAPSED_MS", "session.read", NULL, 0, 0, false },
    
    { "DOOR", "EXIT",     NULL,          NULL, 0, 1, false },
    { "DOOR", "CHAIN",    "door.chain",  NULL, 1, 2, false },
    
    { NULL, NULL, NULL, NULL, 0, 0, false }
};

bucc_host_context_t* bucc_host_context_new(void) {
    bucc_host_context_t* ctx = calloc(1, sizeof(bucc_host_context_t));
    if (!ctx) return NULL;
    
    ctx->app_state = bucc_map_new(16);
    ctx->shared_state = bucc_map_new(16);
    ctx->session_state = bucc_map_new(16);
    ctx->allowed_capabilities = BUCC_CAP_ALL;
    ctx->throttle_limit = 1000;
    ctx->throttle_window_start = 0;
    ctx->throttle_count = 0;
    
    return ctx;
}

void bucc_host_context_free(bucc_host_context_t* ctx) {
    if (!ctx) return;
    
    if (ctx->app_state) {
        bucc_map_release(ctx->app_state);
    }
    if (ctx->shared_state) {
        bucc_map_release(ctx->shared_state);
    }
    if (ctx->session_state) {
        bucc_map_release(ctx->session_state);
    }
    
    free(ctx);
}

void bucc_host_set_allowed_capabilities(bucc_host_context_t* ctx, uint64_t capabilities) {
    if (!ctx) return;
    ctx->allowed_capabilities = capabilities;
}

void bucc_host_set_term_api(bucc_host_context_t* ctx, bucc_term_api_t* api, void* session_ctx) {
    if (!ctx) return;
    ctx->term = api;
    ctx->session_ctx = session_ctx;
}

void bucc_host_set_user_api(bucc_host_context_t* ctx, bucc_user_api_t* api, void* user_ctx) {
    if (!ctx) return;
    ctx->user = api;
    ctx->user_ctx = user_ctx;
}

void bucc_host_set_data_api(bucc_host_context_t* ctx, bucc_data_api_t* api, void* data_ctx) {
    if (!ctx) return;
    ctx->data = api;
    ctx->data_ctx = data_ctx;
}

void bucc_host_set_kv_api(bucc_host_context_t* ctx, bucc_kv_api_t* api, void* kv_ctx) {
    if (!ctx) return;
    ctx->kv = api;
    ctx->kv_ctx = kv_ctx;
}

void bucc_host_set_text_api(bucc_host_context_t* ctx, bucc_text_api_t* api, void* text_ctx) {
    if (!ctx) return;
    ctx->text = api;
    ctx->text_ctx = text_ctx;
}

void bucc_host_set_bbs_api(bucc_host_context_t* ctx, bucc_bbs_api_t* api, void* bbs_ctx) {
    if (!ctx) return;
    ctx->bbs = api;
    ctx->bbs_ctx = bbs_ctx;
}

void bucc_host_set_users_api(bucc_host_context_t* ctx, bucc_users_api_t* api, void* users_ctx) {
    if (!ctx) return;
    ctx->users = api;
    ctx->users_ctx = users_ctx;
}

void bucc_host_set_msg_api(bucc_host_context_t* ctx, bucc_msg_api_t* api, void* msg_ctx) {
    if (!ctx) return;
    ctx->msg = api;
    ctx->msg_ctx = msg_ctx;
}

void bucc_host_set_file_api(bucc_host_context_t* ctx, bucc_file_api_t* api, void* file_ctx) {
    if (!ctx) return;
    ctx->file = api;
    ctx->file_ctx = file_ctx;
}

static bucc_host_function_t* find_host_function(const char* ns, const char* fn) {
    for (int i = 0; host_functions[i].ns != NULL; i++) {
        if (strcasecmp_local(host_functions[i].ns, ns) == 0 &&
            strcasecmp_local(host_functions[i].name, fn) == 0) {
            return &host_functions[i];
        }
    }
    return NULL;
}

static uint64_t capability_from_string(const char* cap) {
    if (!cap) return 0;

    if (strcmp(cap, "term.output") == 0) return BUCC_CAP_TERM_OUTPUT;
    if (strcmp(cap, "term.input") == 0) return BUCC_CAP_TERM_INPUT;
    if (strcmp(cap, "term.query") == 0) return BUCC_CAP_TERM_QUERY;
    if (strcmp(cap, "user.read") == 0) return BUCC_CAP_USER_READ;
    if (strcmp(cap, "data.read") == 0) return BUCC_CAP_DATA_READ;
    if (strcmp(cap, "data.write") == 0) return BUCC_CAP_DATA_WRITE;
    if (strcmp(cap, "kv.read") == 0) return BUCC_CAP_KV_READ;
    if (strcmp(cap, "kv.write") == 0) return BUCC_CAP_KV_WRITE;
    if (strcmp(cap, "text.read") == 0) return BUCC_CAP_TEXT_READ;
    if (strcmp(cap, "text.write") == 0) return BUCC_CAP_TEXT_WRITE;
    if (strcmp(cap, "bbs.message") == 0) return BUCC_CAP_BBS_MESSAGE;
    if (strcmp(cap, "bbs.query") == 0) return BUCC_CAP_BBS_QUERY;
    if (strcmp(cap, "shared.read") == 0) return BUCC_CAP_SHARED_READ;
    if (strcmp(cap, "shared.write") == 0) return BUCC_CAP_SHARED_WRITE;
    if (strcmp(cap, "session.read") == 0) return BUCC_CAP_SESSION_READ;
    if (strcmp(cap, "session.write") == 0) return BUCC_CAP_SESSION_WRITE;
    if (strcmp(cap, "door.chain") == 0) return BUCC_CAP_DOOR_CHAIN;

    return 0;
}

bucc_value_t bucc_host_term_print(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->term || !ctx->term->print) {
        return bucc_make_error("TERM.PRINT not available");
    }
    if (argc < 1) {
        return bucc_make_error("TERM.PRINT requires 1 argument");
    }
    
    char* str = bucc_value_to_cstring(args[0]);
    ctx->term->print(ctx->session_ctx, str);
    free(str);
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_term_println(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->term || !ctx->term->println) {
        return bucc_make_error("TERM.PRINTLN not available");
    }
    
    if (argc == 0) {
        ctx->term->println(ctx->session_ctx, "");
    } else {
        char* str = bucc_value_to_cstring(args[0]);
        ctx->term->println(ctx->session_ctx, str);
        free(str);
    }
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_term_cls(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->term || !ctx->term->cls) {
        return bucc_make_error("TERM.CLS not available");
    }
    
    ctx->term->cls(ctx->session_ctx);
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_term_color(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->term || !ctx->term->color) {
        return bucc_make_error("TERM.COLOR not available");
    }
    if (argc < 1) {
        return bucc_make_error("TERM.COLOR requires at least 1 argument");
    }
    
    int fg = (int)bucc_value_to_int(args[0]);
    int bg = (argc > 1) ? (int)bucc_value_to_int(args[1]) : -1;
    
    ctx->term->color(ctx->session_ctx, fg, bg);
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_term_getkey(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->term || !ctx->term->getkey) {
        return bucc_make_error("TERM.GETKEY not available");
    }
    
    char* key = ctx->term->getkey(ctx->session_ctx);
    if (!key) return bucc_make_string("");
    
    bucc_value_t result = bucc_make_string(key);
    free(key);
    return result;
}

bucc_value_t bucc_host_term_input(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->term || !ctx->term->input) {
        return bucc_make_error("TERM.INPUT not available");
    }
    
    const char* prompt = "";
    int maxlen = 255;
    
    if (argc > 0 && BUCC_IS_STRING(args[0])) {
        prompt = args[0].as.str->data;
    }
    if (argc > 1) {
        maxlen = (int)bucc_value_to_int(args[1]);
    }
    
    char* input = ctx->term->input(ctx->session_ctx, prompt, maxlen);
    if (!input) return bucc_make_string("");
    
    bucc_value_t result = bucc_make_string(input);
    free(input);
    return result;
}

bucc_value_t bucc_host_term_gotoxy(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->term || !ctx->term->gotoxy) {
        return bucc_make_error("TERM.GOTOXY not available");
    }
    if (argc < 2) {
        return bucc_make_error("TERM.GOTOXY requires x and y");
    }
    ctx->term->gotoxy(ctx->session_ctx, (int)bucc_value_to_int(args[0]),
                      (int)bucc_value_to_int(args[1]));
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_term_input_password(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->term || !ctx->term->input_password) {
        return bucc_make_error("TERM.INPUT_PASSWORD not available");
    }
    const char* prompt = "";
    if (argc > 0 && BUCC_IS_STRING(args[0])) prompt = args[0].as.str->data;
    char* input = ctx->term->input_password(ctx->session_ctx, prompt);
    if (!input) return bucc_make_string("");
    bucc_value_t result = bucc_make_string(input);
    free(input);
    return result;
}

bucc_value_t bucc_host_term_pause(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->term || !ctx->term->pause) {
        return bucc_make_error("TERM.PAUSE not available");
    }
    const char* prompt = NULL;
    if (argc > 0 && BUCC_IS_STRING(args[0])) prompt = args[0].as.str->data;
    ctx->term->pause(ctx->session_ctx, prompt);
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_term_width(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->term || !ctx->term->get_width) {
        return BUCC_I64_VAL(80);
    }
    
    return BUCC_I64_VAL(ctx->term->get_width(ctx->session_ctx));
}

bucc_value_t bucc_host_term_height(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->term || !ctx->term->get_height) {
        return BUCC_I64_VAL(24);
    }
    
    return BUCC_I64_VAL(ctx->term->get_height(ctx->session_ctx));
}

bucc_value_t bucc_host_term_supports_ansi(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    if (!ctx || !ctx->term || !ctx->term->supports_ansi) {
        return BUCC_BOOL_VAL(true);
    }
    return BUCC_BOOL_VAL(ctx->term->supports_ansi(ctx->session_ctx));
}

bucc_value_t bucc_host_user_name(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->user || !ctx->user->get_name) {
        return bucc_make_string("Guest");
    }
    
    const char* name = ctx->user->get_name(ctx->user_ctx);
    return bucc_make_string(name ? name : "Guest");
}

bucc_value_t bucc_host_user_alias(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->user || !ctx->user->get_alias) {
        return bucc_make_string("");
    }
    
    const char* alias = ctx->user->get_alias(ctx->user_ctx);
    return bucc_make_string(alias ? alias : "");
}

bucc_value_t bucc_host_user_id(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->user || !ctx->user->get_id) {
        return BUCC_I64_VAL(0);
    }
    
    return BUCC_I64_VAL(ctx->user->get_id(ctx->user_ctx));
}

bucc_value_t bucc_host_user_security(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->user || !ctx->user->get_security) {
        return BUCC_I64_VAL(0);
    }
    
    return BUCC_I64_VAL(ctx->user->get_security(ctx->user_ctx));
}

bucc_value_t bucc_host_user_time_left(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    
    if (!ctx || !ctx->user || !ctx->user->get_time_left) {
        return BUCC_I64_VAL(60);
    }
    
    return BUCC_I64_VAL(ctx->user->get_time_left(ctx->user_ctx));
}

bucc_value_t bucc_host_user_flags(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;

    if (!ctx || !ctx->user || !ctx->user->get_flags) {
        return BUCC_I64_VAL(0);
    }

    return BUCC_I64_VAL(ctx->user->get_flags(ctx->user_ctx));
}

bucc_value_t bucc_host_data_insert(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->data || !ctx->data->insert) {
        return bucc_make_error("DATA.INSERT not available");
    }
    if (argc < 2) {
        return bucc_make_error("DATA.INSERT requires 2 arguments");
    }
    if (!BUCC_IS_STRING(args[0]) || !BUCC_IS_MAP(args[1])) {
        return bucc_make_error("DATA.INSERT: invalid argument types");
    }
    
    const char* dataset = args[0].as.str->data;
    bucc_map_t* record = args[1].as.map;
    
    int64_t id = ctx->data->insert(ctx->data_ctx, dataset, record);
    return BUCC_I64_VAL(id);
}

bucc_value_t bucc_host_data_update(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->data || !ctx->data->update) {
        return bucc_make_error("DATA.UPDATE not available");
    }
    if (argc < 3) {
        return bucc_make_error("DATA.UPDATE requires 3 arguments");
    }
    if (!BUCC_IS_STRING(args[0]) || !BUCC_IS_MAP(args[2])) {
        return bucc_make_error("DATA.UPDATE: invalid argument types");
    }
    
    const char* dataset = args[0].as.str->data;
    int64_t id = bucc_value_to_int(args[1]);
    bucc_map_t* fields = args[2].as.map;
    
    int result = ctx->data->update(ctx->data_ctx, dataset, id, fields);
    return BUCC_I64_VAL(result);
}

bucc_value_t bucc_host_data_delete(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->data || !ctx->data->delete_record) {
        return bucc_make_error("DATA.DELETE not available");
    }
    if (argc < 2) {
        return bucc_make_error("DATA.DELETE requires 2 arguments");
    }
    if (!BUCC_IS_STRING(args[0])) {
        return bucc_make_error("DATA.DELETE: invalid argument types");
    }
    
    const char* dataset = args[0].as.str->data;
    int64_t id = bucc_value_to_int(args[1]);
    
    int result = ctx->data->delete_record(ctx->data_ctx, dataset, id);
    return BUCC_I64_VAL(result);
}

bucc_value_t bucc_host_data_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->data || !ctx->data->get) {
        return bucc_make_error("DATA.GET not available");
    }
    if (argc < 2) {
        return bucc_make_error("DATA.GET requires 2 arguments");
    }
    if (!BUCC_IS_STRING(args[0])) {
        return bucc_make_error("DATA.GET: invalid argument types");
    }
    
    const char* dataset = args[0].as.str->data;
    int64_t id = bucc_value_to_int(args[1]);
    
    bucc_map_t* record = ctx->data->get(ctx->data_ctx, dataset, id);
    if (!record) return BUCC_NULL_VAL;
    
    bucc_value_t result;
    result.kind = BUCC_VAL_MAP;
    result.as.map = record;
    return result;
}

bucc_value_t bucc_host_data_find(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->data || !ctx->data->find) {
        return bucc_make_error("DATA.FIND not available");
    }
    if (argc < 2) {
        return bucc_make_error("DATA.FIND requires at least 2 arguments");
    }
    if (!BUCC_IS_STRING(args[0]) || !BUCC_IS_MAP(args[1])) {
        return bucc_make_error("DATA.FIND: invalid argument types");
    }
    
    const char* dataset = args[0].as.str->data;
    bucc_map_t* query = args[1].as.map;
    const char* order = (argc > 2 && BUCC_IS_STRING(args[2])) ? args[2].as.str->data : NULL;
    int limit = (argc > 3) ? (int)bucc_value_to_int(args[3]) : 100;
    int offset = (argc > 4) ? (int)bucc_value_to_int(args[4]) : 0;
    
    bucc_array_t* results = ctx->data->find(ctx->data_ctx, dataset, query, order, limit, offset);
    if (!results) {
        results = bucc_array_new(8);
    }
    
    bucc_value_t result;
    result.kind = BUCC_VAL_ARRAY;
    result.as.array = results;
    return result;
}

bucc_value_t bucc_host_kv_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->kv || !ctx->kv->get) {
        return bucc_make_error("KV.GET not available");
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("KV.GET requires a string key");
    }
    
    const char* key = args[0].as.str->data;
    const char* def = (argc > 1 && BUCC_IS_STRING(args[1])) ? args[1].as.str->data : "";
    
    char* value = ctx->kv->get(ctx->kv_ctx, key, def);
    if (!value) return bucc_make_string(def);
    
    bucc_value_t result = bucc_make_string(value);
    free(value);
    return result;
}

bucc_value_t bucc_host_kv_set(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->kv || !ctx->kv->set) {
        return bucc_make_error("KV.SET not available");
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("KV.SET requires key and value");
    }
    
    const char* key = args[0].as.str->data;
    char* value = bucc_value_to_cstring(args[1]);
    
    ctx->kv->set(ctx->kv_ctx, key, value);
    free(value);
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_kv_delete(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->kv || !ctx->kv->delete_key) {
        return bucc_make_error("KV.DELETE not available");
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("KV.DELETE requires a string key");
    }
    
    const char* key = args[0].as.str->data;
    ctx->kv->delete_key(ctx->kv_ctx, key);
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_app_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->app_state) {
        return BUCC_NULL_VAL;
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("APP.GET requires a string key");
    }
    
    const char* key = args[0].as.str->data;
    bucc_value_t* val = bucc_map_get_cstr(ctx->app_state, key);
    
    if (!val) {
        if (argc > 1) return args[1];
        return BUCC_NULL_VAL;
    }
    
    return *val;
}

bucc_value_t bucc_host_app_set(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->app_state) {
        return bucc_make_error("APP.SET not available");
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("APP.SET requires key and value");
    }
    
    const char* key = args[0].as.str->data;
    bucc_map_set_cstr(ctx->app_state, key, args[1]);
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_shared_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->shared_state) {
        if (argc > 1) return args[1];
        return BUCC_NULL_VAL;
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("SHARED.GET requires a string key");
    }
    
    const char* key = args[0].as.str->data;
    bucc_value_t* val = bucc_map_get_cstr(ctx->shared_state, key);
    
    if (!val) {
        if (argc > 1) return args[1];
        return BUCC_NULL_VAL;
    }
    
    return *val;
}

bucc_value_t bucc_host_shared_set(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx) {
        return bucc_make_error("SHARED.SET not available");
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("SHARED.SET requires key and value");
    }
    
    if (!ctx->shared_state) {
        ctx->shared_state = bucc_map_new(16);
    }
    
    const char* key = args[0].as.str->data;
    bucc_map_set_cstr(ctx->shared_state, key, args[1]);
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_shared_cas(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx) {
        return BUCC_BOOL_VAL(false);
    }
    if (argc < 3 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("SHARED.CAS requires key, expected, and new value");
    }
    
    if (!ctx->shared_state) {
        ctx->shared_state = bucc_map_new(16);
    }
    
    const char* key = args[0].as.str->data;
    bucc_value_t* current = bucc_map_get_cstr(ctx->shared_state, key);
    
    bool matches = false;
    if (!current && BUCC_IS_NULL(args[1])) {
        matches = true;
    } else if (current) {
        matches = bucc_value_equal(current, &args[1]);
    }
    
    if (matches) {
        bucc_map_set_cstr(ctx->shared_state, key, args[2]);
        return BUCC_BOOL_VAL(true);
    }
    
    return BUCC_BOOL_VAL(false);
}

bucc_value_t bucc_host_text_read_all(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->text || !ctx->text->read_all) {
        return bucc_make_error("TEXT.READALL not available");
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("TEXT.READALL requires a path");
    }
    
    const char* path = args[0].as.str->data;
    char* content = ctx->text->read_all(ctx->text_ctx, path);
    
    if (!content) return BUCC_NULL_VAL;
    
    bucc_value_t result = bucc_make_string(content);
    free(content);
    return result;
}

bucc_value_t bucc_host_text_write_all(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->text || !ctx->text->write_all) {
        return bucc_make_error("TEXT.WRITEALL not available");
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("TEXT.WRITEALL requires path and content");
    }
    
    const char* path = args[0].as.str->data;
    char* content = bucc_value_to_cstring(args[1]);
    
    bool ok = ctx->text->write_all(ctx->text_ctx, path, content);
    free(content);
    
    return BUCC_BOOL_VAL(ok);
}

bucc_value_t bucc_host_text_exists(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->text || !ctx->text->exists) {
        return BUCC_BOOL_VAL(false);
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("TEXT.EXISTS requires a path");
    }
    
    const char* path = args[0].as.str->data;
    return BUCC_BOOL_VAL(ctx->text->exists(ctx->text_ctx, path));
}

bucc_value_t bucc_host_sys_now(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)ctx;
    (void)args;
    (void)argc;
    
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    
    bucc_datetime_t dt;
    dt.year = (int16_t)(tm->tm_year + 1900);
    dt.month = (uint8_t)(tm->tm_mon + 1);
    dt.day = (uint8_t)tm->tm_mday;
    dt.hour = (uint8_t)tm->tm_hour;
    dt.minute = (uint8_t)tm->tm_min;
    dt.second = (uint8_t)tm->tm_sec;
    dt._pad = 0;
    
    return BUCC_DATETIME_VAL(dt);
}

bucc_value_t bucc_host_sys_today(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)ctx;
    (void)args;
    (void)argc;
    
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    
    bucc_date_t d;
    d.year = (int16_t)(tm->tm_year + 1900);
    d.month = (uint8_t)(tm->tm_mon + 1);
    d.day = (uint8_t)tm->tm_mday;
    
    return BUCC_DATE_VAL(d);
}

bucc_value_t bucc_host_door_exit(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->vm) {
        return BUCC_NULL_VAL;
    }
    
    int code = 0;
    if (argc > 0) {
        code = (int)bucc_value_to_int(args[0]);
    }
    
    ctx->vm->exit_code = code;
    ctx->vm->status = VM_HALT;
    ctx->vm->running = false;
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_door_chain(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->vm) {
        return bucc_make_error("DOOR.CHAIN not available");
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("DOOR.CHAIN requires a target door name");
    }

    free(ctx->vm->chain_target);
    ctx->vm->chain_target = strdup(args[0].as.str->data);

    bucc_value_release(&ctx->vm->chain_args);
    if (argc > 1) {
        ctx->vm->chain_args = args[1];
        bucc_value_retain(&ctx->vm->chain_args);
    } else {
        ctx->vm->chain_args = BUCC_NULL_VAL;
    }

    ctx->vm->status = VM_CHAIN;
    ctx->vm->running = false;
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_data_count(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->data || !ctx->data->count) {
        return BUCC_I64_VAL(0);
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0]) || !BUCC_IS_MAP(args[1])) {
        return bucc_make_error("DATA.COUNT requires dataset and query");
    }
    
    const char* dataset = args[0].as.str->data;
    bucc_map_t* query = args[1].as.map;
    
    int64_t count = ctx->data->count(ctx->data_ctx, dataset, query);
    return BUCC_I64_VAL(count);
}

bucc_value_t bucc_host_data_begin(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    if (!ctx || !ctx->data || !ctx->data->begin_tx) {
        return BUCC_BOOL_VAL(false);
    }
    return BUCC_BOOL_VAL(ctx->data->begin_tx(ctx->data_ctx));
}

bucc_value_t bucc_host_data_commit(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    if (!ctx || !ctx->data || !ctx->data->commit_tx) {
        return BUCC_BOOL_VAL(false);
    }
    return BUCC_BOOL_VAL(ctx->data->commit_tx(ctx->data_ctx));
}

bucc_value_t bucc_host_data_rollback(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    if (!ctx || !ctx->data || !ctx->data->rollback_tx) {
        return BUCC_BOOL_VAL(false);
    }
    return BUCC_BOOL_VAL(ctx->data->rollback_tx(ctx->data_ctx));
}

bucc_value_t bucc_host_kv_exists(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->kv || !ctx->kv->exists) {
        return BUCC_BOOL_VAL(false);
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("KV.EXISTS requires a string key");
    }
    
    const char* key = args[0].as.str->data;
    return BUCC_BOOL_VAL(ctx->kv->exists(ctx->kv_ctx, key));
}

bucc_value_t bucc_host_text_read_lines(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->text || !ctx->text->read_lines) {
        return bucc_make_error("TEXT.READLINES not available");
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("TEXT.READLINES requires a path");
    }
    
    const char* path = args[0].as.str->data;
    bucc_array_t* lines = ctx->text->read_lines(ctx->text_ctx, path);
    
    if (!lines) {
        lines = bucc_array_new(8);
    }
    
    bucc_value_t result;
    result.kind = BUCC_VAL_ARRAY;
    result.as.array = lines;
    return result;
}

bucc_value_t bucc_host_text_append(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->text || !ctx->text->append) {
        return bucc_make_error("TEXT.APPEND not available");
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("TEXT.APPEND requires path and content");
    }
    
    const char* path = args[0].as.str->data;
    char* content = bucc_value_to_cstring(args[1]);
    
    bool ok = ctx->text->append(ctx->text_ctx, path, content);
    free(content);
    
    return BUCC_BOOL_VAL(ok);
}

bucc_value_t bucc_host_sys_sleep(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)ctx;
    if (argc < 1) {
        return BUCC_NULL_VAL;
    }
    
    int64_t ms = bucc_value_to_int(args[0]);
    if (ms > 0 && ms < 60000) {
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (ms % 1000) * 1000000;
        nanosleep(&ts, NULL);
    }
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_session_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->session_state) {
        if (argc > 1) return args[1];
        return BUCC_NULL_VAL;
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("SESSION.GET requires a string key");
    }
    
    const char* key = args[0].as.str->data;
    bucc_value_t* val = bucc_map_get_cstr(ctx->session_state, key);
    
    if (!val) {
        if (argc > 1) return args[1];
        return BUCC_NULL_VAL;
    }
    
    return *val;
}

bucc_value_t bucc_host_session_set(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx) {
        return bucc_make_error("SESSION.SET not available");
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("SESSION.SET requires key and value");
    }
    
    if (!ctx->session_state) {
        ctx->session_state = bucc_map_new(16);
    }
    
    const char* key = args[0].as.str->data;
    bucc_map_set_cstr(ctx->session_state, key, args[1]);
    
    return BUCC_NULL_VAL;
}

bucc_value_t bucc_host_session_node(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    if (!ctx || !ctx->bbs || !ctx->bbs->get_node) return BUCC_I64_VAL(1);
    return BUCC_I64_VAL(ctx->bbs->get_node(ctx->bbs_ctx));
}

bucc_value_t bucc_host_session_elapsed_ms(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    if (!ctx || !ctx->vm) return BUCC_I64_VAL(0);
    return BUCC_I64_VAL(ctx->vm->counters.elapsed_ms);
}

bucc_value_t bucc_host_bbs_send_msg(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->bbs || !ctx->bbs->send_msg) {
        return bucc_make_error("BBS.SENDMSG not available");
    }
    if (argc < 2) {
        return bucc_make_error("BBS.SENDMSG requires node and message");
    }
    
    int node = (int)bucc_value_to_int(args[0]);
    char* msg = bucc_value_to_cstring(args[1]);
    
    bool ok = ctx->bbs->send_msg(ctx->bbs_ctx, node, msg);
    free(msg);
    
    return BUCC_BOOL_VAL(ok);
}

bucc_value_t bucc_host_bbs_online(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    if (!ctx || !ctx->bbs || !ctx->bbs->get_online) {
        bucc_array_t* arr = bucc_array_new(8);
        bucc_value_t result;
        result.kind = BUCC_VAL_ARRAY;
        result.as.array = arr;
        return result;
    }
    
    bucc_array_t* online = ctx->bbs->get_online(ctx->bbs_ctx);
    if (!online) {
        online = bucc_array_new(8);
    }
    
    bucc_value_t result;
    result.kind = BUCC_VAL_ARRAY;
    result.as.array = online;
    return result;
}

bucc_value_t bucc_host_bbs_node(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    (void)args;
    (void)argc;
    if (!ctx || !ctx->bbs || !ctx->bbs->get_node) {
        return BUCC_I64_VAL(1);
    }
    
    return BUCC_I64_VAL(ctx->bbs->get_node(ctx->bbs_ctx));
}

bucc_value_t bucc_host_users_find(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->users || !ctx->users->find) {
        return bucc_make_error("USERS.FIND not available");
    }
    if (argc < 1 || !BUCC_IS_MAP(args[0])) {
        return bucc_make_error("USERS.FIND requires a query map");
    }
    
    time_t now = time(NULL);
    if (now - ctx->throttle_window_start > 60) {
        ctx->throttle_window_start = now;
        ctx->throttle_count = 0;
    }
    
    if (ctx->throttle_count >= ctx->throttle_limit) {
        return bucc_make_error("USERS.FIND rate limit exceeded");
    }
    ctx->throttle_count++;
    
    bucc_map_t* query = args[0].as.map;
    int limit = (argc > 1) ? (int)bucc_value_to_int(args[1]) : 10;
    if (limit > 100) limit = 100;
    
    bucc_array_t* results = ctx->users->find(ctx->users_ctx, query, limit);
    if (!results) {
        results = bucc_array_new(8);
    }
    
    bucc_value_t result;
    result.kind = BUCC_VAL_ARRAY;
    result.as.array = results;
    return result;
}

bucc_value_t bucc_host_users_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->users || !ctx->users->get) {
        return BUCC_NULL_VAL;
    }
    if (argc < 1) {
        return bucc_make_error("USERS.GET requires a user ID");
    }
    
    int64_t id = bucc_value_to_int(args[0]);
    bucc_map_t* user = ctx->users->get(ctx->users_ctx, id);
    
    if (!user) return BUCC_NULL_VAL;
    
    bucc_value_t result;
    result.kind = BUCC_VAL_MAP;
    result.as.map = user;
    return result;
}

bucc_value_t bucc_host_msg_read(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->msg || !ctx->msg->read) {
        return BUCC_NULL_VAL;
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("MSG.READ requires area and message ID");
    }
    
    const char* area = args[0].as.str->data;
    int64_t id = bucc_value_to_int(args[1]);
    
    bucc_map_t* msg = ctx->msg->read(ctx->msg_ctx, area, id);
    if (!msg) return BUCC_NULL_VAL;
    
    bucc_value_t result;
    result.kind = BUCC_VAL_MAP;
    result.as.map = msg;
    return result;
}

bucc_value_t bucc_host_msg_list(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->msg || !ctx->msg->list) {
        bucc_array_t* arr = bucc_array_new(8);
        bucc_value_t result;
        result.kind = BUCC_VAL_ARRAY;
        result.as.array = arr;
        return result;
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("MSG.LIST requires an area name");
    }
    
    const char* area = args[0].as.str->data;
    int limit = (argc > 1) ? (int)bucc_value_to_int(args[1]) : 50;
    int offset = (argc > 2) ? (int)bucc_value_to_int(args[2]) : 0;
    
    bucc_array_t* msgs = ctx->msg->list(ctx->msg_ctx, area, limit, offset);
    if (!msgs) {
        msgs = bucc_array_new(8);
    }
    
    bucc_value_t result;
    result.kind = BUCC_VAL_ARRAY;
    result.as.array = msgs;
    return result;
}

bucc_value_t bucc_host_msg_post(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->msg || !ctx->msg->post) {
        return bucc_make_error("MSG.POST not available");
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0]) || !BUCC_IS_MAP(args[1])) {
        return bucc_make_error("MSG.POST requires area and message map");
    }
    
    const char* area = args[0].as.str->data;
    bucc_map_t* msg = args[1].as.map;
    
    int64_t id = ctx->msg->post(ctx->msg_ctx, area, msg);
    return BUCC_I64_VAL(id);
}

bucc_value_t bucc_host_file_list(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->file || !ctx->file->list) {
        bucc_array_t* arr = bucc_array_new(8);
        bucc_value_t result;
        result.kind = BUCC_VAL_ARRAY;
        result.as.array = arr;
        return result;
    }
    if (argc < 1 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("FILE.LIST requires an area name");
    }
    
    const char* area = args[0].as.str->data;
    int limit = (argc > 1) ? (int)bucc_value_to_int(args[1]) : 50;
    int offset = (argc > 2) ? (int)bucc_value_to_int(args[2]) : 0;
    
    bucc_array_t* files = ctx->file->list(ctx->file_ctx, area, limit, offset);
    if (!files) {
        files = bucc_array_new(8);
    }
    
    bucc_value_t result;
    result.kind = BUCC_VAL_ARRAY;
    result.as.array = files;
    return result;
}

bucc_value_t bucc_host_file_info(bucc_host_context_t* ctx, bucc_value_t* args, int argc) {
    if (!ctx || !ctx->file || !ctx->file->info) {
        return BUCC_NULL_VAL;
    }
    if (argc < 2 || !BUCC_IS_STRING(args[0])) {
        return bucc_make_error("FILE.INFO requires area and file ID");
    }
    
    const char* area = args[0].as.str->data;
    int64_t id = bucc_value_to_int(args[1]);
    
    bucc_map_t* info = ctx->file->info(ctx->file_ctx, area, id);
    if (!info) return BUCC_NULL_VAL;
    
    bucc_value_t result;
    result.kind = BUCC_VAL_MAP;
    result.as.map = info;
    return result;
}

bucc_value_t bucc_host_dispatch(bucc_vm_t* vm, const char* ns, const char* fn,
                                bucc_value_t* args, int argc, void* ctx_ptr) {
    bucc_host_context_t* ctx = (bucc_host_context_t*)ctx_ptr;
    if (!ctx) {
        return bucc_make_error("No host context");
    }
    
    ctx->vm = vm;
    
    bucc_host_function_t* func = find_host_function(ns, fn);
    if (!func) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Unknown host function: %s.%s", ns, fn);
        return bucc_make_error(msg);
    }

    if (func->capability) {
        uint64_t cap = capability_from_string(func->capability);
        if (cap && !(ctx->allowed_capabilities & cap)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Capability denied: %s.%s requires %s",
                     ns, fn, func->capability);
            return bucc_make_error(msg);
        }
    }
    
    if (argc < func->min_args) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s.%s requires at least %d arguments", 
                 ns, fn, func->min_args);
        return bucc_make_error(msg);
    }

    if (func->max_args >= 0 && argc > func->max_args) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s.%s accepts at most %d arguments",
                 ns, fn, func->max_args);
        return bucc_make_error(msg);
    }
    
    if (strcasecmp_local(ns, "TERM") == 0) {
        if (strcasecmp_local(fn, "PRINT") == 0) return bucc_host_term_print(ctx, args, argc);
        if (strcasecmp_local(fn, "PRINTLN") == 0) return bucc_host_term_println(ctx, args, argc);
        if (strcasecmp_local(fn, "CLS") == 0) return bucc_host_term_cls(ctx, args, argc);
        if (strcasecmp_local(fn, "COLOR") == 0) return bucc_host_term_color(ctx, args, argc);
        if (strcasecmp_local(fn, "GOTOXY") == 0) return bucc_host_term_gotoxy(ctx, args, argc);
        if (strcasecmp_local(fn, "GETKEY") == 0) return bucc_host_term_getkey(ctx, args, argc);
        if (strcasecmp_local(fn, "INPUT") == 0) return bucc_host_term_input(ctx, args, argc);
        if (strcasecmp_local(fn, "INPUT_PASSWORD") == 0) return bucc_host_term_input_password(ctx, args, argc);
        if (strcasecmp_local(fn, "PAUSE") == 0) return bucc_host_term_pause(ctx, args, argc);
        if (strcasecmp_local(fn, "WIDTH") == 0) return bucc_host_term_width(ctx, args, argc);
        if (strcasecmp_local(fn, "HEIGHT") == 0) return bucc_host_term_height(ctx, args, argc);
        if (strcasecmp_local(fn, "SUPPORTS_ANSI") == 0) return bucc_host_term_supports_ansi(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "USER") == 0) {
        if (strcasecmp_local(fn, "NAME") == 0) return bucc_host_user_name(ctx, args, argc);
        if (strcasecmp_local(fn, "ALIAS") == 0) return bucc_host_user_alias(ctx, args, argc);
        if (strcasecmp_local(fn, "ID") == 0) return bucc_host_user_id(ctx, args, argc);
        if (strcasecmp_local(fn, "SECURITY") == 0) return bucc_host_user_security(ctx, args, argc);
        if (strcasecmp_local(fn, "TIMELEFT") == 0 ||
            strcasecmp_local(fn, "TIME_LEFT") == 0 ||
            strcasecmp_local(fn, "TIME_REMAINING") == 0) return bucc_host_user_time_left(ctx, args, argc);
        if (strcasecmp_local(fn, "FLAGS") == 0) return bucc_host_user_flags(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "DATA") == 0) {
        if (strcasecmp_local(fn, "INSERT") == 0) return bucc_host_data_insert(ctx, args, argc);
        if (strcasecmp_local(fn, "UPDATE") == 0) return bucc_host_data_update(ctx, args, argc);
        if (strcasecmp_local(fn, "DELETE") == 0) return bucc_host_data_delete(ctx, args, argc);
        if (strcasecmp_local(fn, "GET") == 0) return bucc_host_data_get(ctx, args, argc);
        if (strcasecmp_local(fn, "FIND") == 0) return bucc_host_data_find(ctx, args, argc);
        if (strcasecmp_local(fn, "COUNT") == 0) return bucc_host_data_count(ctx, args, argc);
        if (strcasecmp_local(fn, "BEGIN") == 0) return bucc_host_data_begin(ctx, args, argc);
        if (strcasecmp_local(fn, "COMMIT") == 0) return bucc_host_data_commit(ctx, args, argc);
        if (strcasecmp_local(fn, "ROLLBACK") == 0) return bucc_host_data_rollback(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "KV") == 0) {
        if (strcasecmp_local(fn, "GET") == 0) return bucc_host_kv_get(ctx, args, argc);
        if (strcasecmp_local(fn, "SET") == 0) return bucc_host_kv_set(ctx, args, argc);
        if (strcasecmp_local(fn, "DELETE") == 0) return bucc_host_kv_delete(ctx, args, argc);
        if (strcasecmp_local(fn, "EXISTS") == 0) return bucc_host_kv_exists(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "APP") == 0) {
        if (strcasecmp_local(fn, "GET") == 0) return bucc_host_app_get(ctx, args, argc);
        if (strcasecmp_local(fn, "SET") == 0) return bucc_host_app_set(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "SHARED") == 0) {
        if (strcasecmp_local(fn, "GET") == 0) return bucc_host_shared_get(ctx, args, argc);
        if (strcasecmp_local(fn, "SET") == 0) return bucc_host_shared_set(ctx, args, argc);
        if (strcasecmp_local(fn, "CAS") == 0) return bucc_host_shared_cas(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "TEXT") == 0) {
        if (strcasecmp_local(fn, "READALL") == 0) return bucc_host_text_read_all(ctx, args, argc);
        if (strcasecmp_local(fn, "READLINES") == 0) return bucc_host_text_read_lines(ctx, args, argc);
        if (strcasecmp_local(fn, "WRITEALL") == 0) return bucc_host_text_write_all(ctx, args, argc);
        if (strcasecmp_local(fn, "APPEND") == 0) return bucc_host_text_append(ctx, args, argc);
        if (strcasecmp_local(fn, "EXISTS") == 0) return bucc_host_text_exists(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "SYS") == 0) {
        if (strcasecmp_local(fn, "NOW") == 0) return bucc_host_sys_now(ctx, args, argc);
        if (strcasecmp_local(fn, "TODAY") == 0) return bucc_host_sys_today(ctx, args, argc);
        if (strcasecmp_local(fn, "SLEEP") == 0) return bucc_host_sys_sleep(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "SESSION") == 0) {
        if (strcasecmp_local(fn, "GET") == 0) return bucc_host_session_get(ctx, args, argc);
        if (strcasecmp_local(fn, "SET") == 0) return bucc_host_session_set(ctx, args, argc);
        if (strcasecmp_local(fn, "NODE") == 0) return bucc_host_session_node(ctx, args, argc);
        if (strcasecmp_local(fn, "ELAPSED_MS") == 0) return bucc_host_session_elapsed_ms(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "BBS") == 0) {
        if (strcasecmp_local(fn, "SENDMSG") == 0) return bucc_host_bbs_send_msg(ctx, args, argc);
        if (strcasecmp_local(fn, "ONLINE") == 0) return bucc_host_bbs_online(ctx, args, argc);
        if (strcasecmp_local(fn, "NODE") == 0) return bucc_host_bbs_node(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "USERS") == 0) {
        if (strcasecmp_local(fn, "FIND") == 0) return bucc_host_users_find(ctx, args, argc);
        if (strcasecmp_local(fn, "GET") == 0) return bucc_host_users_get(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "MSG") == 0) {
        if (strcasecmp_local(fn, "READ") == 0) return bucc_host_msg_read(ctx, args, argc);
        if (strcasecmp_local(fn, "LIST") == 0) return bucc_host_msg_list(ctx, args, argc);
        if (strcasecmp_local(fn, "POST") == 0) return bucc_host_msg_post(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "FILE") == 0) {
        if (strcasecmp_local(fn, "LIST") == 0) return bucc_host_file_list(ctx, args, argc);
        if (strcasecmp_local(fn, "INFO") == 0) return bucc_host_file_info(ctx, args, argc);
    }
    else if (strcasecmp_local(ns, "DOOR") == 0) {
        if (strcasecmp_local(fn, "EXIT") == 0) return bucc_host_door_exit(ctx, args, argc);
        if (strcasecmp_local(fn, "CHAIN") == 0) return bucc_host_door_chain(ctx, args, argc);
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Unimplemented host function: %s.%s", ns, fn);
    return bucc_make_error(msg);
}
