/*
 * plugin_loader.c - Plugin Loading and Lifecycle Management
 *
 * This file handles loading plugins via dlopen(), validating their ABI,
 * and managing the plugin lifecycle (init/shutdown).
 */

#define _GNU_SOURCE
#include "bbs_plugin_loader.h"
#include "bbs_plugin_registry.h"
#include "bbs_plugin_api.h"
#include "bbs_log.h"
#include "bbs_util.h"
#include <dlfcn.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

/* Plugin configuration */
static struct {
  bool enabled;
  char dir[256];
  char allowlist[1024];
  char denylist[1024];
  bool initialized;
} g_plugin_cfg = {0};

/* Forward declarations */
extern const bbs_host_api_t* plugin_host_api_get(void);
extern void plugin_host_api_shutdown(void);
extern void plugin_host_api_configure(const BbsConfig* cfg);

/* Check if plugin ID is in a comma-separated list */
static bool id_in_list(const char* id, const char* list) {
  if (!id || !list || !list[0]) return false;
  
  char buf[1024];
  strncpy(buf, list, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  
  char* saveptr = NULL;
  char* token = strtok_r(buf, ",", &saveptr);
  while (token) {
    /* Trim whitespace */
    while (*token == ' ') token++;
    char* end = token + strlen(token) - 1;
    while (end > token && *end == ' ') *end-- = '\0';
    
    if (strcmp(token, id) == 0) return true;
    token = strtok_r(NULL, ",", &saveptr);
  }
  return false;
}

/* Check if plugin is allowed to load based on allow/deny lists */
static bool plugin_allowed(const char* id) {
  /* Denylist always takes precedence */
  if (id_in_list(id, g_plugin_cfg.denylist)) {
    log_info("plugin %s is in denylist, skipping", id);
    return false;
  }
  
  /* If allowlist is set, only allow listed plugins */
  if (g_plugin_cfg.allowlist[0]) {
    if (!id_in_list(id, g_plugin_cfg.allowlist)) {
      log_info("plugin %s not in allowlist, skipping", id);
      return false;
    }
  }
  
  return true;
}

/* Load a single plugin from a .so file */
static bool load_plugin(const char* path) {
  log_info("loading plugin: %s", path);
  
  /* Open the shared object */
  void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    log_error("dlopen failed for %s: %s", path, dlerror());
    return false;
  }
  
  /* Clear any existing error */
  dlerror();
  
  /* Find the query function */
  bbs_plugin_query_fn query_fn = (bbs_plugin_query_fn)dlsym(handle, "bbs_plugin_query");
  const char* err = dlerror();
  if (err || !query_fn) {
    log_error("dlsym failed for bbs_plugin_query in %s: %s", path, err ? err : "symbol not found");
    dlclose(handle);
    return false;
  }
  
  /* Query the plugin */
  bbs_plugin_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  
  const bbs_host_api_t* host_api = plugin_host_api_get();
  bbs_rc_t rc = query_fn(BBS_PLUGIN_ABI_VERSION, host_api, &desc);
  
  if (rc != BBS_OK) {
    log_error("bbs_plugin_query failed for %s: rc=%d", path, rc);
    dlclose(handle);
    return false;
  }
  
  /* Validate magic */
  if (desc.magic != BBS_PLUGIN_MAGIC) {
    log_error("plugin %s has invalid magic: 0x%08x (expected 0x%08x)",
              path, desc.magic, BBS_PLUGIN_MAGIC);
    dlclose(handle);
    return false;
  }
  
  /* Validate ABI version (major must match) */
  uint32_t host_major = (BBS_PLUGIN_ABI_VERSION >> 16) & 0xFFFF;
  uint32_t plugin_major = (desc.abi_version >> 16) & 0xFFFF;
  if (plugin_major != host_major) {
    log_error("plugin %s ABI version mismatch: plugin=0x%08x, host=0x%08x",
              path, desc.abi_version, BBS_PLUGIN_ABI_VERSION);
    dlclose(handle);
    return false;
  }
  
  /* Validate struct size for forward compatibility */
  if (desc.size < sizeof(bbs_plugin_desc_t)) {
    log_error("plugin %s has invalid desc size: %u (expected >= %zu)",
              path, desc.size, sizeof(bbs_plugin_desc_t));
    dlclose(handle);
    return false;
  }
  
  /* Validate required fields */
  if (!desc.id || !bbs_safe_identifier(desc.id, 96)) {
    log_error("plugin %s has missing or unsafe ID", path);
    dlclose(handle);
    return false;
  }
  
  /* Check allow/deny lists */
  if (!plugin_allowed(desc.id)) {
    dlclose(handle);
    return false;
  }
  
  /* Check if already loaded */
  if (plugin_registry_find(desc.id)) {
    log_warn("plugin %s already loaded, skipping duplicate from %s", desc.id, path);
    dlclose(handle);
    return false;
  }
  
  /* Call plugin init if provided */
  if (desc.init) {
    rc = desc.init(host_api);
    if (rc != BBS_OK) {
      log_error("plugin %s init failed: rc=%d", desc.id, rc);
      dlclose(handle);
      return false;
    }
  }
  
  /* Register the plugin */
  if (!plugin_registry_add(&desc, handle, path)) {
    log_error("failed to register plugin %s", desc.id);
    if (desc.shutdown) desc.shutdown();
    dlclose(handle);
    return false;
  }
  
  log_info("loaded plugin: %s (%s) v%s by %s",
           desc.id,
           desc.name ? desc.name : "unnamed",
           desc.version ? desc.version : "0.0.0",
           desc.author ? desc.author : "unknown");
  
  return true;
}

/* Scan plugin directory and load all .so files */
static int scan_and_load_plugins(const char* dir) {
  DIR* d = opendir(dir);
  if (!d) {
    log_warn("cannot open plugin directory: %s", dir);
    return 0;
  }
  
  int loaded = 0;
  struct dirent* ent;
  
  while ((ent = readdir(d)) != NULL) {
    /* Skip hidden files and directories */
    if (ent->d_name[0] == '.') continue;
    
    /* Check for .so extension */
    size_t len = strlen(ent->d_name);
    if (len < 4) continue;
    if (strcmp(ent->d_name + len - 3, ".so") != 0) continue;
    
    /* Build full path */
    char path[512];
    path_join(dir, ent->d_name, path, sizeof(path));
    
    /* Verify it's a regular file */
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    
    /* Try to load it */
    if (load_plugin(path)) {
      loaded++;
    }
  }
  
  closedir(d);
  return loaded;
}

bool plugin_loader_init(const BbsConfig* cfg) {
  if (g_plugin_cfg.initialized) {
    log_warn("plugin loader already initialized");
    return true;
  }
  
  /* Initialize registry first */
  plugin_registry_init();
  plugin_host_api_configure(cfg);
  
  g_plugin_cfg.enabled = cfg ? (cfg->plugins_enabled != 0) : true;
  snprintf(g_plugin_cfg.dir, sizeof(g_plugin_cfg.dir), "%s",
           (cfg && cfg->plugins_dir[0]) ? cfg->plugins_dir : "plugins");
  snprintf(g_plugin_cfg.allowlist, sizeof(g_plugin_cfg.allowlist), "%s",
           cfg ? cfg->plugins_allowlist : "");
  snprintf(g_plugin_cfg.denylist, sizeof(g_plugin_cfg.denylist), "%s",
           cfg ? cfg->plugins_denylist : "");

  if (!g_plugin_cfg.enabled) {
    log_info("plugins disabled by config");
    g_plugin_cfg.enabled = false;
    g_plugin_cfg.initialized = true;
    return true;
  }

  struct stat st;
  if (stat(g_plugin_cfg.dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
    log_info("plugin directory not found: %s", g_plugin_cfg.dir);
    g_plugin_cfg.initialized = true;
    return true;
  }
  
  /* Scan and load plugins */
  int loaded = scan_and_load_plugins(g_plugin_cfg.dir);
  log_info("plugin loader initialized: %d plugin(s) loaded from %s", loaded, g_plugin_cfg.dir);
  
  g_plugin_cfg.initialized = true;
  return true;
}

void plugin_loader_shutdown(void) {
  if (!g_plugin_cfg.initialized) return;
  
  /* Check for active instances */
  if (plugin_registry_has_instances()) {
    log_warn("plugin shutdown called with active instances!");
  }
  
  /* Shutdown and unload each plugin */
  size_t count = plugin_registry_count();
  for (size_t i = 0; i < count; i++) {
    plugin_entry_t* entry = plugin_registry_get(i);
    if (!entry || !entry->loaded) continue;
    
    log_info("shutting down plugin: %s", entry->desc.id);
    
    /* Call shutdown if provided */
    if (entry->desc.shutdown) {
      entry->desc.shutdown();
    }
    
    /* Close the shared object */
    if (entry->dl_handle) {
      dlclose(entry->dl_handle);
      entry->dl_handle = NULL;
    }
    
    entry->loaded = false;
  }
  
  /* Shutdown registry */
  plugin_registry_shutdown();
  
  /* Shutdown plugin host API (task scheduler, etc.) */
  plugin_host_api_shutdown();
  
  g_plugin_cfg.initialized = false;
  log_info("plugin loader shutdown complete");
}

bool plugin_loader_reload(void) {
  if (!g_plugin_cfg.initialized || !g_plugin_cfg.enabled) {
    return false;
  }
  
  /* Only reload plugins with no active instances */
  size_t count = plugin_registry_count();
  for (size_t i = 0; i < count; i++) {
    plugin_entry_t* entry = plugin_registry_get(i);
    if (!entry || !entry->loaded) continue;
    
    if (entry->instance_count > 0) {
      log_info("skipping reload of %s: %d active instance(s)",
               entry->desc.id, entry->instance_count);
      continue;
    }
    
    /* Shutdown and unload */
    if (entry->desc.shutdown) {
      entry->desc.shutdown();
    }
    if (entry->dl_handle) {
      dlclose(entry->dl_handle);
    }
    
    /* Reload from same path */
    char path[256];
    strncpy(path, entry->path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    
    plugin_registry_remove(entry->desc.id);
    load_plugin(path);
  }
  
  return true;
}

const bbs_host_api_t* plugin_get_host_api(void) {
  return plugin_host_api_get();
}

bool plugin_loader_enabled(void) {
  return g_plugin_cfg.initialized && g_plugin_cfg.enabled;
}

const char* plugin_loader_get_dir(void) {
  return g_plugin_cfg.dir;
}
