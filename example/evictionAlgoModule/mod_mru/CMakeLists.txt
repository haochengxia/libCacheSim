# A tiny extra eviction algorithm module used to test
# LIBCACHESIM_EXTRA_MODULES_PATH.
#
# Configure from the repository root with:
#   cmake -B build -DLIBCACHESIM_EXTRA_MODULES_PATH=/abs/path/to/example/evictionAlgoModule/mod_mru

libcachesim_add_eviction_algo_module(
    NAME mod_mru
    INIT ModMRU_init
    SOURCES ModMRU.c
    ALIASES mod-mru ModMRU
)
