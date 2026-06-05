# A tiny extra eviction algorithm module used to test
# LIBCACHESIM_EXTRA_MODULES_PATH.
#
# Configure from the repository root with:
# cmake -S . -B build -DLIBCACHESIM_EXTRA_MODULES_PATH="$(pwd)/example/evictionAlgoModule/mod_mru"

libcachesim_register_eviction_algo(
    NAME ModMRU
    INIT_FN ModMRU_init
    SOURCES ModMRU.c
)
