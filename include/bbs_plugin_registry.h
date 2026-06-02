/*
 * bbs_plugin_registry.h - Plugin Registry Internal API
 *
 * This header provides the internal API for the plugin registry.
 * The registry tracks loaded plugins and their active instances.
 */

#pragma once
#include "bbs_plugin_api.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of plugins that can be loaded */
#define PLUGIN_REGISTRY_MAX 64

/* Plugin entry in the registry */
typedef struct plugin_entry {
  bbs_plugin_desc_t desc;
  void* dl_handle;           /* dlopen handle */
  int instance_count;        /* active instances */
  bool loaded;               /* true if successfully loaded */
  char path[256];            /* path to .so file */
} plugin_entry_t;

/*
 * Initialize the plugin registry.
 */
void plugin_registry_init(void);

/*
 * Shutdown the plugin registry.
 * Frees all resources but does NOT unload plugins (use plugin_loader_shutdown).
 */
void plugin_registry_shutdown(void);

/*
 * Add a plugin to the registry.
 * Returns true on success, false if registry is full or plugin ID already exists.
 */
bool plugin_registry_add(const bbs_plugin_desc_t* desc, void* dl_handle, const char* path);

/*
 * Remove a plugin from the registry by ID.
 * Returns true if found and removed, false otherwise.
 * Does NOT call dlclose - that's the loader's job.
 */
bool plugin_registry_remove(const char* id);

/*
 * Find a plugin by ID.
 * Returns pointer to entry or NULL if not found.
 */
plugin_entry_t* plugin_registry_find(const char* id);

/*
 * Get the number of loaded plugins.
 */
size_t plugin_registry_count(void);

/*
 * Get a plugin by index (0 to count-1).
 * Returns NULL if index is out of bounds.
 */
plugin_entry_t* plugin_registry_get(size_t index);

/*
 * Increment the instance count for a plugin.
 * Called when a new instance is created.
 */
void plugin_registry_instance_inc(const char* id);

/*
 * Decrement the instance count for a plugin.
 * Called when an instance is destroyed.
 */
void plugin_registry_instance_dec(const char* id);

/*
 * Get the instance count for a plugin.
 */
int plugin_registry_instance_count(const char* id);

/*
 * Check if any plugin has active instances.
 * Used during shutdown to ensure safe unloading.
 */
bool plugin_registry_has_instances(void);

/*
 * List all loaded plugins to a buffer.
 * Format: "id: name (version) - description\n"
 * Returns number of bytes written.
 */
size_t plugin_registry_list(char* buf, size_t buf_sz);

#ifdef __cplusplus
}
#endif
