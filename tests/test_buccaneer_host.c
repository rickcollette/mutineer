#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/bucc_host.h"

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

static void term_print(void* ctx, const char* text) {
  (void)ctx;
  (void)text;
}

static void term_println(void* ctx, const char* text) {
  (void)ctx;
  (void)text;
}

static int kv_set_called = 0;
static int gotoxy_x = 0;
static int gotoxy_y = 0;
static int pause_called = 0;

static void kv_set(void* ctx, const char* key, const char* value) {
  (void)ctx;
  (void)key;
  (void)value;
  kv_set_called++;
}

static void term_gotoxy(void* ctx, int x, int y) {
  (void)ctx;
  gotoxy_x = x;
  gotoxy_y = y;
}

static char* term_input_password(void* ctx, const char* prompt) {
  (void)ctx;
  (void)prompt;
  char* out = malloc(7);
  if (out) memcpy(out, "secret", 7);
  return out;
}

static void term_pause(void* ctx, const char* prompt) {
  (void)ctx;
  (void)prompt;
  pause_called++;
}

static bool term_supports_ansi(void* ctx) {
  (void)ctx;
  return false;
}

static int user_time_left(void* ctx) {
  (void)ctx;
  return 42;
}

static int bbs_node(void* ctx) {
  (void)ctx;
  return 7;
}

static int check_bool_result(bucc_value_t v, bool expected, const char* msg) {
  if (v.kind != BUCC_VAL_BOOL || v.as.b != expected) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
  }
  return 0;
}

int main(void) {
  bucc_host_context_t* ctx = bucc_host_context_new();
  CHECK(ctx != NULL, "host context allocates");

  bucc_term_api_t term = {
    .print = term_print,
    .println = term_println,
    .gotoxy = term_gotoxy,
    .input_password = term_input_password,
    .pause = term_pause,
    .supports_ansi = term_supports_ansi
  };
  bucc_user_api_t user = {
    .get_time_left = user_time_left
  };
  bucc_bbs_api_t bbs = {
    .get_node = bbs_node
  };
  bucc_kv_api_t kv = {
    .set = kv_set
  };
  bucc_host_set_term_api(ctx, &term, NULL);
  bucc_host_set_user_api(ctx, &user, NULL);
  bucc_host_set_bbs_api(ctx, &bbs, NULL);
  bucc_host_set_kv_api(ctx, &kv, NULL);
  bucc_host_set_allowed_capabilities(ctx, BUCC_CAP_TERM_OUTPUT | BUCC_CAP_TERM_INPUT |
                                          BUCC_CAP_TERM_QUERY | BUCC_CAP_USER_READ |
                                          BUCC_CAP_SESSION_READ | BUCC_CAP_DOOR_CHAIN |
                                          BUCC_CAP_SHARED_READ | BUCC_CAP_SHARED_WRITE);

  bucc_value_t ok = bucc_host_dispatch(NULL, "TERM", "PRINTLN", NULL, 0, ctx);
  CHECK(ok.kind == BUCC_VAL_NULL, "allowed TERM.PRINTLN succeeds");

  bucc_value_t denied = bucc_host_dispatch(NULL, "KV", "SET", NULL, 0, ctx);
  CHECK(denied.kind == BUCC_VAL_ERROR, "denied KV.SET returns an error");
  CHECK(kv_set_called == 0, "denied KV.SET does not call handler");
  CHECK(denied.as.error && strstr(denied.as.error->message, "Capability denied") != NULL,
        "denied error names capability denial");
  bucc_value_release(&denied);

  bucc_value_t xy_args[] = { BUCC_I64_VAL(12), BUCC_I64_VAL(5) };
  bucc_value_t xy = bucc_host_dispatch(NULL, "TERM", "GOTOXY", xy_args, 2, ctx);
  CHECK(xy.kind == BUCC_VAL_NULL && gotoxy_x == 12 && gotoxy_y == 5,
        "TERM.GOTOXY dispatches with coordinates");

  bucc_value_t pwd = bucc_host_dispatch(NULL, "TERM", "INPUT_PASSWORD", NULL, 0, ctx);
  CHECK(pwd.kind == BUCC_VAL_STRING && strcmp(pwd.as.str->data, "secret") == 0,
        "TERM.INPUT_PASSWORD returns a string");
  bucc_value_release(&pwd);

  bucc_value_t pause = bucc_host_dispatch(NULL, "TERM", "PAUSE", NULL, 0, ctx);
  CHECK(pause.kind == BUCC_VAL_NULL && pause_called == 1, "TERM.PAUSE dispatches");

  bucc_value_t ansi = bucc_host_dispatch(NULL, "TERM", "SUPPORTS_ANSI", NULL, 0, ctx);
  CHECK(ansi.kind == BUCC_VAL_BOOL && ansi.as.b == false, "TERM.SUPPORTS_ANSI returns bool");

  bucc_value_t node = bucc_host_dispatch(NULL, "SESSION", "NODE", NULL, 0, ctx);
  CHECK(node.kind == BUCC_VAL_I64 && node.as.i64 == 7, "SESSION.NODE returns node number");

  bucc_vm_t vm = {0};
  vm.counters.elapsed_ms = 1234;
  ctx->vm = &vm;
  bucc_value_t elapsed = bucc_host_dispatch(&vm, "SESSION", "ELAPSED_MS", NULL, 0, ctx);
  CHECK(elapsed.kind == BUCC_VAL_I64 && elapsed.as.i64 == 1234, "SESSION.ELAPSED_MS returns VM counter");

  bucc_value_t time_left = bucc_host_dispatch(&vm, "USER", "TIME_LEFT", NULL, 0, ctx);
  CHECK(time_left.kind == BUCC_VAL_I64 && time_left.as.i64 == 42, "USER.TIME_LEFT returns time left");

  bucc_value_t time_remaining = bucc_host_dispatch(&vm, "USER", "TIME_REMAINING", NULL, 0, ctx);
  CHECK(time_remaining.kind == BUCC_VAL_I64 && time_remaining.as.i64 == 42,
        "USER.TIME_REMAINING compatibility alias returns time left");

  bucc_string_t* cas_key = bucc_string_from_cstr("cas-key");
  bucc_value_t set_scalar[] = { BUCC_STRING_VAL(cas_key), BUCC_I64_VAL(10) };
  bucc_value_t set_scalar_result = bucc_host_dispatch(&vm, "SHARED", "SET", set_scalar, 2, ctx);
  CHECK(set_scalar_result.kind == BUCC_VAL_NULL, "SHARED.SET scalar succeeds");
  bucc_value_t cas_scalar_ok[] = { BUCC_STRING_VAL(cas_key), BUCC_I64_VAL(10), BUCC_I64_VAL(11) };
  bucc_value_t cas_result = bucc_host_dispatch(&vm, "SHARED", "CAS", cas_scalar_ok, 3, ctx);
  CHECK(check_bool_result(cas_result, true, "SHARED.CAS scalar match succeeds") == 0,
        "SHARED.CAS scalar match succeeds");
  bucc_value_t cas_scalar_bad[] = { BUCC_STRING_VAL(cas_key), BUCC_I64_VAL(10), BUCC_I64_VAL(12) };
  cas_result = bucc_host_dispatch(&vm, "SHARED", "CAS", cas_scalar_bad, 3, ctx);
  CHECK(check_bool_result(cas_result, false, "SHARED.CAS scalar mismatch fails") == 0,
        "SHARED.CAS scalar mismatch fails");

  bucc_array_t* arr_current = bucc_array_new(2);
  bucc_array_t* arr_expected = bucc_array_new(2);
  bucc_array_t* arr_different = bucc_array_new(2);
  CHECK(arr_current && arr_expected && arr_different, "arrays allocate");
  bucc_array_push(arr_current, BUCC_I64_VAL(1));
  bucc_array_push(arr_current, bucc_make_string("two"));
  bucc_array_push(arr_expected, BUCC_I64_VAL(1));
  bucc_array_push(arr_expected, bucc_make_string("two"));
  bucc_array_push(arr_different, BUCC_I64_VAL(1));
  bucc_array_push(arr_different, bucc_make_string("three"));
  bucc_string_t* arr_key = bucc_string_from_cstr("array-key");
  bucc_value_t set_array[] = { BUCC_STRING_VAL(arr_key), BUCC_ARRAY_VAL(arr_current) };
  CHECK(bucc_host_dispatch(&vm, "SHARED", "SET", set_array, 2, ctx).kind == BUCC_VAL_NULL,
        "SHARED.SET array succeeds");
  bucc_value_t cas_array_ok[] = { BUCC_STRING_VAL(arr_key), BUCC_ARRAY_VAL(arr_expected), BUCC_I64_VAL(99) };
  cas_result = bucc_host_dispatch(&vm, "SHARED", "CAS", cas_array_ok, 3, ctx);
  CHECK(check_bool_result(cas_result, true, "SHARED.CAS deep array equality succeeds") == 0,
        "SHARED.CAS deep array equality succeeds");
  bucc_value_t set_array_again[] = { BUCC_STRING_VAL(arr_key), BUCC_ARRAY_VAL(arr_current) };
  CHECK(bucc_host_dispatch(&vm, "SHARED", "SET", set_array_again, 2, ctx).kind == BUCC_VAL_NULL,
        "SHARED.SET array again succeeds");
  bucc_value_t cas_array_bad[] = { BUCC_STRING_VAL(arr_key), BUCC_ARRAY_VAL(arr_different), BUCC_I64_VAL(100) };
  cas_result = bucc_host_dispatch(&vm, "SHARED", "CAS", cas_array_bad, 3, ctx);
  CHECK(check_bool_result(cas_result, false, "SHARED.CAS unequal array fails") == 0,
        "SHARED.CAS unequal array fails");

  bucc_map_t* map_current = bucc_map_new(2);
  bucc_map_t* map_expected = bucc_map_new(2);
  bucc_map_t* map_different = bucc_map_new(2);
  CHECK(map_current && map_expected && map_different, "maps allocate");
  bucc_map_set_cstr(map_current, "score", BUCC_I64_VAL(42));
  bucc_map_set_cstr(map_current, "name", bucc_make_string("mutineer"));
  bucc_map_set_cstr(map_expected, "score", BUCC_I64_VAL(42));
  bucc_map_set_cstr(map_expected, "name", bucc_make_string("mutineer"));
  bucc_map_set_cstr(map_different, "score", BUCC_I64_VAL(43));
  bucc_map_set_cstr(map_different, "name", bucc_make_string("mutineer"));
  bucc_string_t* map_key = bucc_string_from_cstr("map-key");
  bucc_value_t set_map[] = { BUCC_STRING_VAL(map_key), BUCC_MAP_VAL(map_current) };
  CHECK(bucc_host_dispatch(&vm, "SHARED", "SET", set_map, 2, ctx).kind == BUCC_VAL_NULL,
        "SHARED.SET map succeeds");
  bucc_value_t cas_map_ok[] = { BUCC_STRING_VAL(map_key), BUCC_MAP_VAL(map_expected), BUCC_I64_VAL(123) };
  cas_result = bucc_host_dispatch(&vm, "SHARED", "CAS", cas_map_ok, 3, ctx);
  CHECK(check_bool_result(cas_result, true, "SHARED.CAS deep map equality succeeds") == 0,
        "SHARED.CAS deep map equality succeeds");
  bucc_value_t set_map_again[] = { BUCC_STRING_VAL(map_key), BUCC_MAP_VAL(map_current) };
  CHECK(bucc_host_dispatch(&vm, "SHARED", "SET", set_map_again, 2, ctx).kind == BUCC_VAL_NULL,
        "SHARED.SET map again succeeds");
  bucc_value_t cas_map_bad[] = { BUCC_STRING_VAL(map_key), BUCC_MAP_VAL(map_different), BUCC_I64_VAL(124) };
  cas_result = bucc_host_dispatch(&vm, "SHARED", "CAS", cas_map_bad, 3, ctx);
  CHECK(check_bool_result(cas_result, false, "SHARED.CAS unequal map fails") == 0,
        "SHARED.CAS unequal map fails");

  bucc_string_release(cas_key);
  bucc_string_release(arr_key);
  bucc_string_release(map_key);
  bucc_array_release(arr_current);
  bucc_array_release(arr_expected);
  bucc_array_release(arr_different);
  bucc_map_release(map_current);
  bucc_map_release(map_expected);
  bucc_map_release(map_different);

  bucc_value_t extra_arg = BUCC_I64_VAL(1);
  bucc_value_t too_many = bucc_host_dispatch(&vm, "TERM", "SUPPORTS_ANSI", &extra_arg, 1, ctx);
  CHECK(too_many.kind == BUCC_VAL_ERROR, "host calls with too many args fail explicitly");
  bucc_value_release(&too_many);

  bucc_value_t exit_arg = BUCC_I64_VAL(7);
  bucc_value_t exit_result = bucc_host_dispatch(&vm, "DOOR", "EXIT", &exit_arg, 1, ctx);
  CHECK(exit_result.kind == BUCC_VAL_NULL && vm.status == VM_HALT && vm.exit_code == 7,
        "DOOR.EXIT preserves exit code and halts");

  bucc_string_t* target = bucc_string_from_cstr("next-door");
  bucc_array_t* chain_args = bucc_array_new(1);
  bucc_value_t chain_values[] = { BUCC_STRING_VAL(target), BUCC_ARRAY_VAL(chain_args) };
  bucc_value_t chain_result = bucc_host_dispatch(&vm, "DOOR", "CHAIN", chain_values, 2, ctx);
  CHECK(chain_result.kind == BUCC_VAL_NULL && vm.status == VM_CHAIN &&
        vm.chain_target && strcmp(vm.chain_target, "next-door") == 0 &&
        vm.chain_args.kind == BUCC_VAL_ARRAY,
        "DOOR.CHAIN preserves target, args, and returns VM_CHAIN");
  bucc_string_release(target);
  bucc_array_release(chain_args);
  free(vm.chain_target);
  bucc_value_release(&vm.chain_args);

  bucc_host_context_free(ctx);
  return 0;
}
