extern const eviction_algo_mod_entry_t libcachesim_eviction_algo_modules[];
extern const size_t libcachesim_n_eviction_algo_modules;

cache_t *create_cache_from_eviction_algo_module(
    const char *eviction_algo,
    common_cache_params_t ccache_params,
    const char *eviction_params) {
  for (size_t i = 0; i < libcachesim_n_eviction_algo_modules; i++) {
    if (strcasecmp(eviction_algo, libcachesim_eviction_algo_modules[i].name) == 0) {
      return libcachesim_eviction_algo_modules[i].init_func(
          ccache_params, eviction_params);
    }
  }
  return NULL;
}
