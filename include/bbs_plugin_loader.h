/*
 * bbs_plugin_loader.h - Plugin Loader Internal API
 *
 * This header provides the internal API for loading and managing plugins.
 * It is used by the BBS host, not by plugins themselves.
 */

#pragma once
#include "bbs_config.h"
#include "bbs_plugin_api.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the plugin loader and load all plugins from the configured directory.
 * Returns true on success (even if some plugins fail to load).
 * Returns false on fatal initialization error.
 */
bool plugin_loader_init(const BbsConfig* cfg);

/*
 * Shutdown the plugin loader.
 * Calls shutdown() on all plugins and unloads them.
 * Must be called only after all plugin instances have been destroyed.
 */
void plugin_loader_shutdown(void);

/*
 * Reload plugins (for development use).
 * Only reloads plugins with no active instances.
 */
bool plugin_loader_reload(void);

/*
 * Get the global host API instance.
 * Used by session code to pass to plugin instances.
 */
const bbs_host_api_t* plugin_get_host_api(void);

/*
 * Check if plugins are enabled in configuration.
 */
bool plugin_loader_enabled(void);

/*
 * Get the plugin directory path.
 */
const char* plugin_loader_get_dir(void);

/* Notify every loaded plugin of a host lifecycle event. */
void plugin_loader_dispatch_event(bbs_event_type_t type, bbs_session_t* session,
                                  void* data);

#ifdef __cplusplus
}
#endif
