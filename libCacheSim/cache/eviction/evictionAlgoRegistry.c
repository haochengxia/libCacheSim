#include "libCacheSim/evictionAlgoRegistry.h"

#include <glib.h>
#include <stdbool.h>

typedef struct eviction_algo_entry {
  eviction_algo_init_fn_t init_fn;
} eviction_algo_entry_t;

static GHashTable *algo_table = NULL;
static bool registry_initialized = false;

static void ensure_algo_table(void) {
  if (algo_table == NULL) {
    algo_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  }
}

static char *normalize_algo_name(const char *name) {
  return g_ascii_strdown(name, -1);
}

int libcachesim_register_eviction_algo(const char *name,
                                       eviction_algo_init_fn_t init_fn) {
  if (name == NULL || init_fn == NULL) {
    return -1;
  }

  ensure_algo_table();

  char *key = normalize_algo_name(name);

  eviction_algo_entry_t *entry = g_new0(eviction_algo_entry_t, 1);
  entry->init_fn = init_fn;

  g_hash_table_replace(algo_table, key, entry);

  return 0;
}

eviction_algo_init_fn_t libcachesim_find_eviction_algo(const char *name) {
  if (name == NULL) {
    return NULL;
  }

  ensure_algo_table();

  char *key = normalize_algo_name(name);
  eviction_algo_entry_t *entry =
      (eviction_algo_entry_t *)g_hash_table_lookup(algo_table, key);
  g_free(key);

  if (entry == NULL) {
    return NULL;
  }

  return entry->init_fn;
}

void libcachesim_register_all_eviction_algos(void) {
  if (registry_initialized) {
    return;
  }

  registry_initialized = true;

  libcachesim_register_extra_eviction_algos();
}

void libcachesim_free_eviction_algo_registry(void) {
  if (algo_table != NULL) {
    g_hash_table_destroy(algo_table);
    algo_table = NULL;
  }

  registry_initialized = false;
}
