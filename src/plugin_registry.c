/*
 * plugin_registry.c - Thread-Safe Plugin Registry
 *
 * This file manages the in-memory registry of loaded plugins,
 * tracking their descriptors, handles, and active instance counts.
 */

#include "bbs_plugin_registry.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>

/* Registry storage */
static plugin_entry_t g_plugins[PLUGIN_REGISTRY_MAX];
static size_t g_plugin_count = 0;
static pthread_mutex_t g_registry_mu = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;

void plugin_registry_init(void) {
  pthread_mutex_lock(&g_registry_mu);
  
  memset(g_plugins, 0, sizeof(g_plugins));
  g_plugin_count = 0;
  g_initialized = true;
  
  pthread_mutex_unlock(&g_registry_mu);
}

void plugin_registry_shutdown(void) {
  pthread_mutex_lock(&g_registry_mu);
  
  memset(g_plugins, 0, sizeof(g_plugins));
  g_plugin_count = 0;
  g_initialized = false;
  
  pthread_mutex_unlock(&g_registry_mu);
}

bool plugin_registry_add(const bbs_plugin_desc_t* desc, void* dl_handle, const char* path) {
  if (!desc || !desc->id) return false;
  
  pthread_mutex_lock(&g_registry_mu);
  
  /* Check if already exists */
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded && strcmp(g_plugins[i].desc.id, desc->id) == 0) {
      pthread_mutex_unlock(&g_registry_mu);
      return false;
    }
  }
  
  /* Check capacity */
  if (g_plugin_count >= PLUGIN_REGISTRY_MAX) {
    pthread_mutex_unlock(&g_registry_mu);
    return false;
  }
  
  /* Add to registry */
  plugin_entry_t* entry = &g_plugins[g_plugin_count];
  memcpy(&entry->desc, desc, sizeof(bbs_plugin_desc_t));
  entry->dl_handle = dl_handle;
  entry->instance_count = 0;
  entry->loaded = true;
  
  if (path) {
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';
  } else {
    entry->path[0] = '\0';
  }
  
  g_plugin_count++;
  
  pthread_mutex_unlock(&g_registry_mu);
  return true;
}

bool plugin_registry_remove(const char* id) {
  if (!id) return false;
  
  pthread_mutex_lock(&g_registry_mu);
  
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded && strcmp(g_plugins[i].desc.id, id) == 0) {
      /* Mark as unloaded but don't remove from array to preserve indices */
      g_plugins[i].loaded = false;
      g_plugins[i].dl_handle = NULL;
      
      pthread_mutex_unlock(&g_registry_mu);
      return true;
    }
  }
  
  pthread_mutex_unlock(&g_registry_mu);
  return false;
}

plugin_entry_t* plugin_registry_find(const char* id) {
  if (!id) return NULL;
  
  pthread_mutex_lock(&g_registry_mu);
  
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded && strcmp(g_plugins[i].desc.id, id) == 0) {
      pthread_mutex_unlock(&g_registry_mu);
      return &g_plugins[i];
    }
  }
  
  pthread_mutex_unlock(&g_registry_mu);
  return NULL;
}

size_t plugin_registry_count(void) {
  pthread_mutex_lock(&g_registry_mu);
  
  size_t count = 0;
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded) count++;
  }
  
  pthread_mutex_unlock(&g_registry_mu);
  return count;
}

plugin_entry_t* plugin_registry_get(size_t index) {
  pthread_mutex_lock(&g_registry_mu);
  
  size_t count = 0;
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded) {
      if (count == index) {
        pthread_mutex_unlock(&g_registry_mu);
        return &g_plugins[i];
      }
      count++;
    }
  }
  
  pthread_mutex_unlock(&g_registry_mu);
  return NULL;
}

void plugin_registry_instance_inc(const char* id) {
  if (!id) return;
  
  pthread_mutex_lock(&g_registry_mu);
  
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded && strcmp(g_plugins[i].desc.id, id) == 0) {
      g_plugins[i].instance_count++;
      break;
    }
  }
  
  pthread_mutex_unlock(&g_registry_mu);
}

void plugin_registry_instance_dec(const char* id) {
  if (!id) return;
  
  pthread_mutex_lock(&g_registry_mu);
  
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded && strcmp(g_plugins[i].desc.id, id) == 0) {
      if (g_plugins[i].instance_count > 0) {
        g_plugins[i].instance_count--;
      }
      break;
    }
  }
  
  pthread_mutex_unlock(&g_registry_mu);
}

int plugin_registry_instance_count(const char* id) {
  if (!id) return 0;
  
  pthread_mutex_lock(&g_registry_mu);
  
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded && strcmp(g_plugins[i].desc.id, id) == 0) {
      int count = g_plugins[i].instance_count;
      pthread_mutex_unlock(&g_registry_mu);
      return count;
    }
  }
  
  pthread_mutex_unlock(&g_registry_mu);
  return 0;
}

bool plugin_registry_has_instances(void) {
  pthread_mutex_lock(&g_registry_mu);
  
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (g_plugins[i].loaded && g_plugins[i].instance_count > 0) {
      pthread_mutex_unlock(&g_registry_mu);
      return true;
    }
  }
  
  pthread_mutex_unlock(&g_registry_mu);
  return false;
}

size_t plugin_registry_list(char* buf, size_t buf_sz) {
  if (!buf || buf_sz == 0) return 0;
  
  buf[0] = '\0';
  size_t written = 0;
  
  pthread_mutex_lock(&g_registry_mu);
  
  for (size_t i = 0; i < g_plugin_count; i++) {
    if (!g_plugins[i].loaded) continue;
    
    const bbs_plugin_desc_t* d = &g_plugins[i].desc;
    
    int n = snprintf(buf + written, buf_sz - written,
                     "%s: %s (%s) - %s [instances: %d]\n",
                     d->id ? d->id : "unknown",
                     d->name ? d->name : "unnamed",
                     d->version ? d->version : "0.0.0",
                     d->description ? d->description : "",
                     g_plugins[i].instance_count);
    
    if (n < 0 || (size_t)n >= buf_sz - written) break;
    written += (size_t)n;
  }
  
  pthread_mutex_unlock(&g_registry_mu);
  return written;
}
