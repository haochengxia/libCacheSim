# ModMRU Extra Module

This is a minimal eviction algorithm module for testing
`LIBCACHESIM_EXTRA_MODULES_PATH`.

The module wraps the built-in `MRU_init()` and registers it under these names:

- `mod_mru`
- `mod-mru`
- `ModMRU`

From the repository root:

```bash
cmake -B build \
  -DLIBCACHESIM_EXTRA_MODULES_PATH="$PWD/example/evictionAlgoModule/mod_mru"

cmake --build build -j

./build/bin/cachesim data/cloudPhysicsIO.vscsi vscsi mod_mru 1MB
./build/bin/cachesim data/cloudPhysicsIO.vscsi vscsi mod-mru 1MB
./build/bin/cachesim data/cloudPhysicsIO.vscsi vscsi ModMRU 1MB
```

The output cache name should be `ModMRU`, which makes it easy to tell that the
extra module path was used instead of a built-in algorithm name.
