/*
 * test_plugin.c - Plugin Subsystem Unit Tests
 *
 * Tests the plugin loader, registry, and host API functionality.
 */

#include "bbs_plugin_api.h"
#include "bbs_plugin_loader.h"
#include "bbs_plugin_registry.h"
#include "bbs_session.h"
#include "bbs_util.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_ASSERT(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL: %s\n", msg);                                      \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static const char *g_hello_plugin_path = "build/plugins/hello.so";

static bbs_plugin_query_fn test_plugin_query_symbol(void *handle) {
  void *sym = dlsym(handle, "bbs_plugin_query");
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
  bbs_plugin_query_fn fn = (bbs_plugin_query_fn)sym;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  return fn;
}

void send_str(Session *s, const char *str) {
  if (!s || !str)
    return;
  fd_write_all(s->fd, str, strlen(str));
}

int session_readline(Session *s, uint8_t *buf, size_t cap, int timeout) {
  if (!s)
    return -1;
  return fd_readline(s->fd, timeout, buf, cap);
}

int session_readline_echo(Session *s, uint8_t *buf, size_t cap, int timeout,
                          int echo) {
  (void)echo;
  return session_readline(s, buf, cap, timeout);
}

static void rm_rf(const char *path) { bbs_remove_tree(path); }

static int copy_file(const char *src, const char *dst) {
  int in = open(src, O_RDONLY);
  if (in < 0)
    return -1;
  int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (out < 0) {
    close(in);
    return -1;
  }
  char buf[8192];
  ssize_t n;
  while ((n = read(in, buf, sizeof(buf))) > 0) {
    ssize_t off = 0;
    while (off < n) {
      ssize_t w = write(out, buf + off, (size_t)(n - off));
      if (w <= 0) {
        close(in);
        close(out);
        return -1;
      }
      off += w;
    }
  }
  close(in);
  close(out);
  return n == 0 ? 0 : -1;
}

static int make_plugin_dir(char *dir, size_t dir_sz) {
  snprintf(dir, dir_sz, "%s/mutineer_plugin_test_%ld_%d",
           getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid(),
           rand());
  rm_rf(dir);
  if (mkdir(dir, 0755) != 0)
    return -1;
  char dst[512];
  snprintf(dst, sizeof(dst), "%s/hello.so", dir);
  return copy_file(g_hello_plugin_path, dst);
}

/* Test ABI constants */
static int test_abi_constants(void) {
  TEST_ASSERT(BBS_PLUGIN_ABI_VERSION == 0x00010002u,
              "ABI version should be 1.2");
  TEST_ASSERT(BBS_PLUGIN_ABI_VERSION_1_1 == 0x00010001u,
              "ABI 1.1 constant should remain stable");
  TEST_ASSERT(BBS_PLUGIN_ABI_VERSION_1_0 == 0x00010000u,
              "ABI 1.0 compatibility constant should remain available");
  TEST_ASSERT(BBS_PLUGIN_MAGIC == 0x4250534Fu, "Magic should be 'BPSO'");

  TEST_ASSERT(BBS_OK == 0, "BBS_OK should be 0");
  TEST_ASSERT(BBS_EINVAL < 0, "Error codes should be negative");
  TEST_ASSERT(BBS_EUNSUPPORTED < 0, "Error codes should be negative");
  TEST_ASSERT(BBS_EIO < 0, "Error codes should be negative");

  TEST_ASSERT(BBS_CAP_NONE == 0, "CAP_NONE should be 0");
  TEST_ASSERT(BBS_CAP_INTERACTIVE == 1, "CAP_INTERACTIVE should be 1");
  TEST_ASSERT(BBS_CAP_COMMANDS == 2, "CAP_COMMANDS should be 2");
  TEST_ASSERT(BBS_CAP_BACKGROUND == 4, "CAP_BACKGROUND should be 4");

  return 0;
}

/* Test struct sizes for ABI compatibility */
static int test_struct_sizes(void) {
  TEST_ASSERT(sizeof(bbs_host_api_t) > 0, "host_api should have size");
  TEST_ASSERT(sizeof(bbs_plugin_desc_t) > 0, "plugin_desc should have size");
  TEST_ASSERT(sizeof(bbs_plugin_instance_vtbl_t) > 0,
              "instance_vtbl should have size");
  TEST_ASSERT(sizeof(bbs_io_t) > 0, "io should have size");
  TEST_ASSERT(sizeof(bbs_sched_t) > 0, "sched should have size");
  TEST_ASSERT(sizeof(bbs_event_t) > 0, "event should have size");
  TEST_ASSERT(sizeof(bbs_command_def_t) > 0, "command_def should have size");

  return 0;
}

/* Test registry initialization */
static int test_registry_init(void) {
  plugin_registry_init();

  TEST_ASSERT(plugin_registry_count() == 0, "Registry should start empty");
  TEST_ASSERT(!plugin_registry_has_instances(), "No instances initially");
  TEST_ASSERT(plugin_registry_find("nonexistent") == NULL,
              "Find should return NULL for missing");

  plugin_registry_shutdown();
  return 0;
}

/* Test registry add/find/remove */
static int test_registry_operations(void) {
  plugin_registry_init();

  bbs_plugin_desc_t desc1 = {.abi_version = BBS_PLUGIN_ABI_VERSION,
                             .size = sizeof(bbs_plugin_desc_t),
                             .magic = BBS_PLUGIN_MAGIC,
                             .id = "test.plugin.one",
                             .name = "Test Plugin One",
                             .version = "1.0.0",
                             .author = "Test",
                             .description = "Test plugin",
                             .caps = BBS_CAP_INTERACTIVE};

  bbs_plugin_desc_t desc2 = {.abi_version = BBS_PLUGIN_ABI_VERSION,
                             .size = sizeof(bbs_plugin_desc_t),
                             .magic = BBS_PLUGIN_MAGIC,
                             .id = "test.plugin.two",
                             .name = "Test Plugin Two",
                             .version = "2.0.0",
                             .author = "Test",
                             .description = "Another test plugin",
                             .caps = BBS_CAP_COMMANDS};

  /* Add plugins */
  TEST_ASSERT(plugin_registry_add(&desc1, NULL, "test1.so"),
              "Add plugin 1 should succeed");
  TEST_ASSERT(plugin_registry_add(&desc2, NULL, "test2.so"),
              "Add plugin 2 should succeed");
  TEST_ASSERT(plugin_registry_count() == 2, "Should have 2 plugins");

  /* Duplicate add should fail */
  TEST_ASSERT(!plugin_registry_add(&desc1, NULL, "test1.so"),
              "Duplicate add should fail");

  /* Find plugins */
  plugin_entry_t *e1 = plugin_registry_find("test.plugin.one");
  TEST_ASSERT(e1 != NULL, "Find plugin 1 should succeed");
  TEST_ASSERT(strcmp(e1->desc.name, "Test Plugin One") == 0,
              "Name should match");

  plugin_entry_t *e2 = plugin_registry_find("test.plugin.two");
  TEST_ASSERT(e2 != NULL, "Find plugin 2 should succeed");
  TEST_ASSERT(strcmp(e2->desc.version, "2.0.0") == 0, "Version should match");

  /* Remove plugin */
  TEST_ASSERT(plugin_registry_remove("test.plugin.one"),
              "Remove should succeed");
  TEST_ASSERT(plugin_registry_find("test.plugin.one") == NULL,
              "Removed plugin should not be found");
  TEST_ASSERT(plugin_registry_count() == 1,
              "Should have 1 plugin after remove");

  plugin_registry_shutdown();
  return 0;
}

/* Test instance counting */
static int test_instance_counting(void) {
  plugin_registry_init();

  bbs_plugin_desc_t desc = {.abi_version = BBS_PLUGIN_ABI_VERSION,
                            .size = sizeof(bbs_plugin_desc_t),
                            .magic = BBS_PLUGIN_MAGIC,
                            .id = "test.instance",
                            .name = "Instance Test",
                            .version = "1.0.0"};

  TEST_ASSERT(plugin_registry_add(&desc, NULL, "test.so"),
              "Add should succeed");
  TEST_ASSERT(plugin_registry_instance_count("test.instance") == 0,
              "Initial count should be 0");
  TEST_ASSERT(!plugin_registry_has_instances(), "No instances initially");

  /* Increment */
  plugin_registry_instance_inc("test.instance");
  TEST_ASSERT(plugin_registry_instance_count("test.instance") == 1,
              "Count should be 1");
  TEST_ASSERT(plugin_registry_has_instances(), "Should have instances");

  plugin_registry_instance_inc("test.instance");
  TEST_ASSERT(plugin_registry_instance_count("test.instance") == 2,
              "Count should be 2");

  /* Decrement */
  plugin_registry_instance_dec("test.instance");
  TEST_ASSERT(plugin_registry_instance_count("test.instance") == 1,
              "Count should be 1");

  plugin_registry_instance_dec("test.instance");
  TEST_ASSERT(plugin_registry_instance_count("test.instance") == 0,
              "Count should be 0");
  TEST_ASSERT(!plugin_registry_has_instances(), "No instances after decrement");

  /* Decrement below 0 should not go negative */
  plugin_registry_instance_dec("test.instance");
  TEST_ASSERT(plugin_registry_instance_count("test.instance") == 0,
              "Count should stay at 0");

  plugin_registry_shutdown();
  return 0;
}

/* Test registry list */
static int test_registry_list(void) {
  plugin_registry_init();

  bbs_plugin_desc_t desc = {.abi_version = BBS_PLUGIN_ABI_VERSION,
                            .size = sizeof(bbs_plugin_desc_t),
                            .magic = BBS_PLUGIN_MAGIC,
                            .id = "test.list",
                            .name = "List Test",
                            .version = "1.0.0",
                            .description = "Testing list output"};

  plugin_registry_add(&desc, NULL, "test.so");

  char buf[1024];
  size_t n = plugin_registry_list(buf, sizeof(buf));

  TEST_ASSERT(n > 0, "List should return data");
  TEST_ASSERT(strstr(buf, "test.list") != NULL,
              "List should contain plugin ID");
  TEST_ASSERT(strstr(buf, "List Test") != NULL,
              "List should contain plugin name");
  TEST_ASSERT(strstr(buf, "1.0.0") != NULL, "List should contain version");

  plugin_registry_shutdown();
  return 0;
}

/* Test loading actual plugin */
static int test_plugin_load(void) {
  const char *plugin_path = g_hello_plugin_path;

  if (access(plugin_path, F_OK) != 0) {
    printf("  (skipping - hello.so not built yet)\n");
    return 0;
  }

  void *handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  TEST_ASSERT(handle != NULL, "dlopen should succeed");

  bbs_plugin_query_fn query = test_plugin_query_symbol(handle);
  TEST_ASSERT(query != NULL, "dlsym should find bbs_plugin_query");

  bbs_plugin_desc_t desc;
  memset(&desc, 0, sizeof(desc));

  /* Create a minimal host API for testing */
  bbs_host_api_t host = {.abi_version = BBS_PLUGIN_ABI_VERSION,
                         .size = sizeof(bbs_host_api_t),
                         .magic = BBS_PLUGIN_MAGIC};

  bbs_rc_t rc = query(BBS_PLUGIN_ABI_VERSION, &host, &desc);
  TEST_ASSERT(rc == BBS_OK, "query should succeed");
  TEST_ASSERT(desc.magic == BBS_PLUGIN_MAGIC, "magic should match");
  TEST_ASSERT(desc.abi_version == BBS_PLUGIN_ABI_VERSION,
              "ABI version should match");
  TEST_ASSERT(strcmp(desc.id, "com.mutineer.hello") == 0,
              "ID should be com.mutineer.hello");
  TEST_ASSERT(desc.caps & BBS_CAP_INTERACTIVE,
              "Should have interactive capability");
  TEST_ASSERT(desc.init != NULL, "Should have init function");
  TEST_ASSERT(desc.create_instance != NULL,
              "Should have create_instance function");

  dlclose(handle);
  return 0;
}

/* Test ABI version mismatch */
static int test_abi_mismatch(void) {
  const char *plugin_path = g_hello_plugin_path;

  if (access(plugin_path, F_OK) != 0) {
    printf("  (skipping - hello.so not built yet)\n");
    return 0;
  }

  void *handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  TEST_ASSERT(handle != NULL, "dlopen should succeed");

  bbs_plugin_query_fn query = test_plugin_query_symbol(handle);
  TEST_ASSERT(query != NULL, "dlsym should find bbs_plugin_query");

  bbs_plugin_desc_t desc;
  memset(&desc, 0, sizeof(desc));

  bbs_host_api_t host = {.abi_version = BBS_PLUGIN_ABI_VERSION,
                         .size = sizeof(bbs_host_api_t),
                         .magic = BBS_PLUGIN_MAGIC};

  /* Test with wrong major version */
  uint32_t wrong_version = 0x00020000u; /* v2.0 */
  bbs_rc_t rc = query(wrong_version, &host, &desc);
  TEST_ASSERT(rc == BBS_EUNSUPPORTED, "Should reject wrong ABI version");

  dlclose(handle);
  return 0;
}

/* Test concurrent registry access */
static void *thread_inc_dec(void *arg) {
  const char *id = (const char *)arg;
  for (int i = 0; i < 1000; i++) {
    plugin_registry_instance_inc(id);
    plugin_registry_instance_dec(id);
  }
  return NULL;
}

static int test_concurrent_registry(void) {
  plugin_registry_init();

  bbs_plugin_desc_t desc = {.abi_version = BBS_PLUGIN_ABI_VERSION,
                            .size = sizeof(bbs_plugin_desc_t),
                            .magic = BBS_PLUGIN_MAGIC,
                            .id = "test.concurrent",
                            .name = "Concurrent Test"};

  plugin_registry_add(&desc, NULL, "test.so");

  pthread_t threads[4];
  for (int i = 0; i < 4; i++) {
    pthread_create(&threads[i], NULL, thread_inc_dec,
                   (void *)"test.concurrent");
  }

  for (int i = 0; i < 4; i++) {
    pthread_join(threads[i], NULL);
  }

  /* After all increments and decrements, count should be 0 */
  TEST_ASSERT(plugin_registry_instance_count("test.concurrent") == 0,
              "Count should be 0 after concurrent inc/dec");

  plugin_registry_shutdown();
  return 0;
}

/* Test plugin loader (requires plugins directory) */
static int test_loader_init(void) {
  TEST_ASSERT(!plugin_loader_enabled(),
              "Loader should not be enabled before init");
  return 0;
}

static int test_loader_config_rules(void) {
  if (access(g_hello_plugin_path, F_OK) != 0) {
    printf("  (skipping - hello.so not built yet)\n");
    return 0;
  }

  char dir[256];
  TEST_ASSERT(make_plugin_dir(dir, sizeof(dir)) == 0,
              "temp plugin dir creates");

  BbsConfig cfg = {0};
  cfg.plugins_enabled = 0;
  snprintf(cfg.plugins_dir, sizeof(cfg.plugins_dir), "%s", dir);
  TEST_ASSERT(plugin_loader_init(&cfg), "disabled loader init succeeds");
  TEST_ASSERT(!plugin_loader_enabled(), "disabled loader reports disabled");
  TEST_ASSERT(plugin_registry_count() == 0,
              "disabled loader leaves registry empty");
  plugin_loader_shutdown();

  memset(&cfg, 0, sizeof(cfg));
  cfg.plugins_enabled = 1;
  snprintf(cfg.plugins_dir, sizeof(cfg.plugins_dir), "%s", dir);
  TEST_ASSERT(plugin_loader_init(&cfg), "custom plugin dir init succeeds");
  TEST_ASSERT(plugin_loader_enabled(), "custom plugin dir reports enabled");
  TEST_ASSERT(plugin_registry_find("com.mutineer.hello") != NULL,
              "custom dir loads hello plugin");
  plugin_loader_shutdown();

  memset(&cfg, 0, sizeof(cfg));
  cfg.plugins_enabled = 1;
  snprintf(cfg.plugins_dir, sizeof(cfg.plugins_dir), "%s", dir);
  snprintf(cfg.plugins_allowlist, sizeof(cfg.plugins_allowlist),
           "com.example.missing");
  TEST_ASSERT(plugin_loader_init(&cfg), "allowlist init succeeds");
  TEST_ASSERT(plugin_registry_count() == 0,
              "allowlist excludes unlisted plugin");
  plugin_loader_shutdown();

  memset(&cfg, 0, sizeof(cfg));
  cfg.plugins_enabled = 1;
  snprintf(cfg.plugins_dir, sizeof(cfg.plugins_dir), "%s", dir);
  snprintf(cfg.plugins_allowlist, sizeof(cfg.plugins_allowlist),
           "com.mutineer.hello");
  snprintf(cfg.plugins_denylist, sizeof(cfg.plugins_denylist),
           "com.mutineer.hello");
  TEST_ASSERT(plugin_loader_init(&cfg), "denylist init succeeds");
  TEST_ASSERT(plugin_registry_count() == 0, "denylist wins over allowlist");
  plugin_loader_shutdown();

  rm_rf(dir);
  return 0;
}

static int test_host_api_filesystem_safety(void) {
  char root[256];
  snprintf(root, sizeof(root), "%s/mutineer_plugin_data_%ld_%d",
           getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid(),
           rand());
  rm_rf(root);
  TEST_ASSERT(mkdir(root, 0755) == 0, "plugin data root creates");

  BbsConfig cfg = {0};
  cfg.plugins_enabled = 0;
  snprintf(cfg.data_path, sizeof(cfg.data_path), "%s", root);
  TEST_ASSERT(plugin_loader_init(&cfg), "host API config init succeeds");
  const bbs_host_api_t *api = plugin_get_host_api();
  TEST_ASSERT(api != NULL, "host API available");

  TEST_ASSERT(api->kv_set("safe.ns", "safe-key", "value") == BBS_OK,
              "safe KV set succeeds");
  char out[32];
  TEST_ASSERT(api->kv_get("safe.ns", "safe-key", out, sizeof(out)) == BBS_OK,
              "safe KV get succeeds");
  TEST_ASSERT(strcmp(out, "value") == 0, "safe KV value round-trips");

  TEST_ASSERT(api->kv_set("../escape", "key", "x") == BBS_EINVAL,
              "KV namespace traversal rejected");
  TEST_ASSERT(api->kv_set("/abs", "key", "x") == BBS_EINVAL,
              "KV absolute-like namespace rejected");
  TEST_ASSERT(api->kv_set("safe", "../key", "x") == BBS_EINVAL,
              "KV key traversal rejected");
  TEST_ASSERT(api->kv_set("safe", "bad/key", "x") == BBS_EINVAL,
              "KV slash key rejected");
  TEST_ASSERT(access("/tmp/escape", F_OK) != 0,
              "invalid KV does not create outside file");

  char data_dir[512];
  TEST_ASSERT(api->plugin_data_dir("com.example.safe", data_dir,
                                   sizeof(data_dir)) == BBS_OK,
              "safe plugin data dir succeeds");
  TEST_ASSERT(strstr(data_dir, root) == data_dir,
              "plugin data dir stays under root");
  TEST_ASSERT(api->plugin_data_dir("../escape", data_dir, sizeof(data_dir)) ==
                  BBS_EINVAL,
              "plugin data traversal rejected");

  plugin_loader_shutdown();
  rm_rf(root);
  return 0;
}

static int test_host_api_io_crlf_normalization(void) {
  BbsConfig cfg = {0};
  cfg.plugins_enabled = 0;
  snprintf(cfg.data_path, sizeof(cfg.data_path), "%s",
           getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  TEST_ASSERT(plugin_loader_init(&cfg), "host API config init succeeds");
  const bbs_host_api_t *api = plugin_get_host_api();
  TEST_ASSERT(api != NULL && api->io != NULL && api->io->write != NULL,
              "host IO API available");

  int fds[2];
  TEST_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0,
              "socketpair succeeds");

  Session sess;
  memset(&sess, 0, sizeof(sess));
  sess.fd = fds[0];

  const char *text = "one\ntwo\r\nthree";
  TEST_ASSERT(api->io->write((bbs_session_t *)&sess, text, strlen(text)) ==
                  BBS_OK,
              "host IO write succeeds");

  char buf[64];
  ssize_t n = read(fds[1], buf, sizeof(buf) - 1);
  TEST_ASSERT(n > 0, "host IO data readable");
  buf[n] = '\0';
  TEST_ASSERT(strcmp(buf, "one\r\ntwo\r\nthree") == 0,
              "host IO normalizes bare LF without doubling CRLF");

  close(fds[0]);
  close(fds[1]);
  plugin_loader_shutdown();
  return 0;
}

int main(int argc, char **argv) {
  int failures = 0;
  int total = 0;

  if (argc > 1 && argv[1][0]) {
    g_hello_plugin_path = argv[1];
  } else {
    const char *env_path = getenv("MUTINEER_HELLO_PLUGIN");
    if (env_path && env_path[0]) {
      g_hello_plugin_path = env_path;
    }
  }

  printf("Running plugin subsystem tests...\n\n");

#define RUN_TEST(name)                                                         \
  do {                                                                         \
    total++;                                                                   \
    printf("  %s... ", #name);                                                 \
    fflush(stdout);                                                            \
    if (name() == 0) {                                                         \
      printf("PASS\n");                                                        \
    } else {                                                                   \
      printf("FAIL\n");                                                        \
      failures++;                                                              \
    }                                                                          \
  } while (0)

  RUN_TEST(test_abi_constants);
  RUN_TEST(test_struct_sizes);
  RUN_TEST(test_registry_init);
  RUN_TEST(test_registry_operations);
  RUN_TEST(test_instance_counting);
  RUN_TEST(test_registry_list);
  RUN_TEST(test_plugin_load);
  RUN_TEST(test_abi_mismatch);
  RUN_TEST(test_concurrent_registry);
  RUN_TEST(test_loader_init);
  RUN_TEST(test_loader_config_rules);
  RUN_TEST(test_host_api_filesystem_safety);
  RUN_TEST(test_host_api_io_crlf_normalization);

  printf("\n%d/%d tests passed\n", total - failures, total);
  return failures > 0 ? 1 : 0;
}
