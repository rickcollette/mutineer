#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/bucc_diag.h"
#include "include/bucc_emit.h"
#include "include/bucc_lexer.h"
#include "include/bucc_module.h"
#include "include/bucc_parser.h"
#include "include/bucc_semantic.h"
#include "include/bucc_value.h"
#include "include/bucc_vm.h"

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

typedef struct {
  char text[256];
} Output;

static void emit_u16(uint8_t* code, size_t* pos, uint16_t value) {
  code[(*pos)++] = (uint8_t)((value >> 8) & 0xff);
  code[(*pos)++] = (uint8_t)(value & 0xff);
}

static bucc_module_t* make_module(const uint8_t* code, size_t len) {
  bucc_module_t* mod = calloc(1, sizeof(*mod));
  if (!mod) return NULL;
  mod->refcount = 1;
  mod->bytecode = malloc(len);
  mod->bytecode_len = (uint32_t)len;
  mod->proc_count = 1;
  mod->procedures = calloc(1, sizeof(*mod->procedures));
  if (!mod->bytecode || !mod->procedures) {
    bucc_module_release(mod);
    return NULL;
  }
  memcpy(mod->bytecode, code, len);
  mod->procedures[0].name = strdup("Main");
  mod->procedures[0].id = 0;
  mod->procedures[0].code_offset = 0;
  mod->procedures[0].code_len = (uint32_t)len;
  return mod;
}

static void module_set_constants(bucc_module_t* mod, bucc_value_t* values, uint32_t count) {
  mod->const_count = count;
  mod->constants = calloc(count, sizeof(*mod->constants));
  for (uint32_t i = 0; i < count; i++) mod->constants[i] = values[i];
}

static void module_set_string(bucc_module_t* mod, const char* value) {
  mod->string_count = 1;
  mod->strings = calloc(1, sizeof(*mod->strings));
  mod->strings[0] = strdup(value);
}

static int run_code(const uint8_t* code, size_t len, bucc_module_t** out_mod, bucc_vm_t** out_vm) {
  bucc_module_t* mod = make_module(code, len);
  CHECK(mod != NULL, "manual module allocates");
  bucc_vm_t* vm = bucc_vm_new(mod);
  CHECK(vm != NULL, "manual VM allocates");
  bucc_vm_status_t status = bucc_vm_run(vm);
  (void)status;
  *out_mod = mod;
  *out_vm = vm;
  return 0;
}

static bucc_module_t* compile_source(const char* source) {
  bucc_diag_t* diag = bucc_diag_new();
  if (!diag) return NULL;
  uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", source, strlen(source));
  bucc_lexer_t lexer;
  bucc_lexer_init(&lexer, source, strlen(source), file_id, diag);
  bucc_parser_t parser;
  bucc_parser_init(&parser, &lexer, diag);
  bucc_node_t* ast = bucc_parse_module(&parser);
  if (bucc_diag_has_errors(diag)) {
    bucc_diag_print(diag, stderr);
    bucc_node_free(ast);
    bucc_diag_free(diag);
    return NULL;
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
  bucc_emitter_t* emitter = bucc_emitter_new(sem, diag);
  if (!emitter) {
    bucc_semantic_free(sem);
    bucc_node_free(ast);
    bucc_diag_free(diag);
    return NULL;
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
  bucc_module_t* mod = bucc_emitter_to_module(emitter);
  bucc_emitter_free(emitter);
  bucc_semantic_free(sem);
  bucc_node_free(ast);
  bucc_diag_free(diag);
  return mod;
}

static bucc_value_t output_host(bucc_vm_t* vm, const char* ns, const char* fn,
                                bucc_value_t* args, int argc, void* ctx) {
  (void)vm;
  Output* out = ctx;
  if (strcmp(ns, "TERM") == 0 && strcmp(fn, "PRINTLN") == 0 && argc > 0 && BUCC_IS_STRING(args[0])) {
    strncat(out->text, args[0].as.str->data, sizeof(out->text) - strlen(out->text) - 1);
    strncat(out->text, "\n", sizeof(out->text) - strlen(out->text) - 1);
  }
  return BUCC_NULL_VAL;
}

static int test_empty_array(void) {
  uint8_t code[] = { OP_ARRAY_NEW, 0, 0, OP_HALT };
  bucc_module_t* mod = NULL;
  bucc_vm_t* vm = NULL;
  CHECK(run_code(code, sizeof(code), &mod, &vm) == 0, "empty array VM runs");
  CHECK(vm->status == VM_HALT, "empty array program halts");
  CHECK(vm->sp == 1 && BUCC_IS_ARRAY(vm->stack[0]) && vm->stack[0].as.array->len == 0,
        "OP_ARRAY_NEW creates a defined empty array");
  bucc_vm_free(vm);
  bucc_module_release(mod);
  return 0;
}

static int test_invalid_dispatch_selector(void) {
  uint8_t code[16];
  size_t pos = 0;
  code[pos++] = OP_PUSH_I64; emit_u16(code, &pos, 0);
  code[pos++] = OP_DISPATCH_CALL; emit_u16(code, &pos, 0); code[pos++] = 1;
  code[pos++] = OP_HALT;
  bucc_module_t* mod = make_module(code, pos);
  CHECK(mod != NULL, "dispatch module allocates");
  bucc_value_t constants[] = { BUCC_I64_VAL(0) };
  module_set_constants(mod, constants, 1);
  bucc_vm_t* vm = bucc_vm_new(mod);
  CHECK(vm != NULL, "dispatch VM allocates");
  bucc_vm_status_t status = bucc_vm_run(vm);
  CHECK(status == VM_ERROR, "invalid dispatch selector is a VM error");
  CHECK(vm->sp == 0, "invalid dispatch selector leaves a defined empty stack");
  bucc_vm_free(vm);
  bucc_module_release(mod);
  return 0;
}

static int test_range_opcode(bool expect_true) {
  uint8_t code[32];
  size_t pos = 0;
  code[pos++] = OP_PUSH_I64; emit_u16(code, &pos, 0);
  code[pos++] = OP_PUSH_I64; emit_u16(code, &pos, 1);
  code[pos++] = OP_PUSH_I64; emit_u16(code, &pos, 2);
  code[pos++] = OP_RANGE_TEST; emit_u16(code, &pos, 0); code[pos++] = 0;
  code[pos++] = OP_HALT;
  bucc_module_t* mod = make_module(code, pos);
  CHECK(mod != NULL, "range module allocates");
  bucc_value_t constants[] = {
    BUCC_I64_VAL(expect_true ? 3 : 5),
    BUCC_I64_VAL(2),
    BUCC_I64_VAL(4)
  };
  module_set_constants(mod, constants, 3);
  bucc_vm_t* vm = bucc_vm_new(mod);
  CHECK(vm != NULL, "range VM allocates");
  bucc_vm_status_t status = bucc_vm_run(vm);
  CHECK(status == VM_HALT, "range VM halts");
  CHECK(vm->sp == 1 && BUCC_IS_BOOL(vm->stack[0]) && vm->stack[0].as.b == expect_true,
        expect_true ? "OP_RANGE_TEST true branch works" : "OP_RANGE_TEST false branch works");
  bucc_vm_free(vm);
  bucc_module_release(mod);
  return 0;
}

static int test_chain_opcode(void) {
  uint8_t code[] = { OP_CHAIN, 0, 0, 0 };
  bucc_module_t* mod = make_module(code, sizeof(code));
  CHECK(mod != NULL, "chain module allocates");
  module_set_string(mod, "next-door");
  bucc_vm_t* vm = bucc_vm_new(mod);
  CHECK(vm != NULL, "chain VM allocates");
  bucc_vm_status_t status = bucc_vm_run(vm);
  CHECK(status == VM_CHAIN, "OP_CHAIN returns VM_CHAIN");
  CHECK(vm->chain_target && strcmp(vm->chain_target, "next-door") == 0,
        "OP_CHAIN preserves chain target");
  bucc_vm_free(vm);
  bucc_module_release(mod);
  return 0;
}

static int test_on_call_emission(void) {
  const char* src =
    "PROGRAM \"Dispatch\"\n"
    "VERSION \"1.0\"\n"
    "CAPABILITY \"term.io\"\n"
    "SUB Main()\n"
    "  ON 2 CALL First, Second\n"
    "END SUB\n"
    "SUB First()\n"
    "  TERM.PRINTLN(\"first\")\n"
    "END SUB\n"
    "SUB Second()\n"
    "  TERM.PRINTLN(\"second\")\n"
    "END SUB\n";
  bucc_module_t* mod = compile_source(src);
  CHECK(mod != NULL, "ON CALL source compiles");
  bool found = false;
  for (uint32_t i = 0; i + 3 < mod->bytecode_len; i++) {
    if (mod->bytecode[i] == OP_DISPATCH_CALL &&
        mod->bytecode[i + 1] == 0 && mod->bytecode[i + 2] == 1 &&
        mod->bytecode[i + 3] == 2) {
      found = true;
      break;
    }
  }
  CHECK(found, "ON CALL emits actual contiguous target base and count");
  Output out = {0};
  bucc_vm_t* vm = bucc_vm_new(mod);
  CHECK(vm != NULL, "ON CALL VM allocates");
  bucc_vm_set_host_handler(vm, output_host, &out);
  bucc_vm_status_t status = bucc_vm_run(vm);
  CHECK(status == VM_HALT, "ON CALL VM halts");
  CHECK(strcmp(out.text, "second\n") == 0, "ON CALL dispatches to selected target");
  bucc_vm_free(vm);
  bucc_module_release(mod);
  return 0;
}

static int test_select_range_source(int selector, const char* expected) {
  char src[512];
  snprintf(src, sizeof(src),
    "PROGRAM \"Range\"\n"
    "VERSION \"1.0\"\n"
    "CAPABILITY \"term.io\"\n"
    "SUB Main()\n"
    "  SELECT CASE %d\n"
    "  CASE 2 TO 4\n"
    "    TERM.PRINTLN(\"range\")\n"
    "  CASE ELSE\n"
    "    TERM.PRINTLN(\"else\")\n"
    "  END SELECT\n"
    "END SUB\n", selector);
  bucc_module_t* mod = compile_source(src);
  CHECK(mod != NULL, "SELECT CASE range source compiles");
  bool has_range = false;
  for (uint32_t i = 0; i < mod->bytecode_len; i++) {
    if (mod->bytecode[i] == OP_RANGE_TEST) {
      has_range = true;
      break;
    }
  }
  CHECK(has_range, "SELECT CASE range emits OP_RANGE_TEST");
  Output out = {0};
  bucc_vm_t* vm = bucc_vm_new(mod);
  CHECK(vm != NULL, "SELECT CASE VM allocates");
  bucc_vm_set_host_handler(vm, output_host, &out);
  bucc_vm_status_t status = bucc_vm_run(vm);
  CHECK(status == VM_HALT, "SELECT CASE VM halts");
  CHECK(strcmp(out.text, expected) == 0, "SELECT CASE range chooses expected branch");
  bucc_vm_free(vm);
  bucc_module_release(mod);
  return 0;
}

int main(void) {
  CHECK(test_empty_array() == 0, "empty array test passed");
  CHECK(test_invalid_dispatch_selector() == 0, "invalid dispatch selector test passed");
  CHECK(test_range_opcode(true) == 0, "range true test passed");
  CHECK(test_range_opcode(false) == 0, "range false test passed");
  CHECK(test_chain_opcode() == 0, "chain opcode test passed");
  CHECK(test_on_call_emission() == 0, "ON CALL emission test passed");
  CHECK(test_select_range_source(3, "range\n") == 0, "SELECT range pass branch test passed");
  CHECK(test_select_range_source(5, "else\n") == 0, "SELECT range fail branch test passed");
  return 0;
}
