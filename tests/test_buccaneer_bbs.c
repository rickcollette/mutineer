#include <stdio.h>
#include <string.h>

#include "include/bucc_bbs.h"

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

static int time_left(void* ctx) {
  return ctx == (void*)0x2222 ? 77 : -1;
}

static void kv_set(void* ctx, const char* key, const char* value) {
  (void)ctx;
  (void)key;
  (void)value;
}

int main(void) {
  bucc_door_runner_t* runner = bucc_door_runner_new();
  CHECK(runner != NULL, "runner allocates");
  CHECK((runner->allowed_capabilities & BUCC_CAP_SAFE) == BUCC_CAP_SAFE,
        "runner defaults to safe capabilities");
  CHECK((runner->allowed_capabilities & BUCC_CAP_KV_WRITE) == 0,
        "runner does not grant unsafe write capabilities by default");

  bucc_term_api_t term = {0};
  bucc_user_api_t user = { .get_time_left = time_left };
  bucc_data_api_t data = {0};
  bucc_kv_api_t kv = { .set = kv_set };
  bucc_text_api_t text = {0};
  bucc_bbs_api_t bbs = {0};

  bucc_door_set_term_api_ex(runner, &term, (void*)0x1111);
  bucc_door_set_user_api_ex(runner, &user, (void*)0x2222);
  bucc_door_set_data_api_ex(runner, &data, (void*)0x3333);
  bucc_door_set_kv_api_ex(runner, &kv, (void*)0x4444);
  bucc_door_set_text_api_ex(runner, &text, (void*)0x5555);
  bucc_door_set_bbs_api_ex(runner, &bbs, (void*)0x6666);

  CHECK(runner->host_ctx->session_ctx == (void*)0x1111, "term context is preserved");
  CHECK(runner->host_ctx->user_ctx == (void*)0x2222, "user context is preserved");
  CHECK(runner->host_ctx->data_ctx == (void*)0x3333, "data context is preserved");
  CHECK(runner->host_ctx->kv_ctx == (void*)0x4444, "kv context is preserved");
  CHECK(runner->host_ctx->text_ctx == (void*)0x5555, "text context is preserved");
  CHECK(runner->host_ctx->bbs_ctx == (void*)0x6666, "bbs context is preserved");

  bucc_value_t tl = bucc_host_dispatch(NULL, "USER", "TIME_LEFT", NULL, 0, runner->host_ctx);
  CHECK(tl.kind == BUCC_VAL_I64 && tl.as.i64 == 77,
        "dispatch sees caller-provided user context");

  bucc_host_set_allowed_capabilities(runner->host_ctx, runner->allowed_capabilities);
  bucc_string_t* key = bucc_string_from_cstr("k");
  bucc_string_t* val = bucc_string_from_cstr("v");
  bucc_value_t args[] = { BUCC_STRING_VAL(key), BUCC_STRING_VAL(val) };
  bucc_value_t denied = bucc_host_dispatch(NULL, "KV", "SET", args, 2, runner->host_ctx);
  CHECK(denied.kind == BUCC_VAL_ERROR, "unsafe capability is denied by default");
  CHECK(denied.as.error && strstr(denied.as.error->message, "Capability denied"),
        "denial explains capability failure");
  bucc_value_release(&denied);
  bucc_string_release(key);
  bucc_string_release(val);

  bucc_door_runner_free(runner);
  return 0;
}
