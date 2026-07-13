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
                                          BUCC_CAP_SESSION_READ | BUCC_CAP_DOOR_CHAIN);

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
