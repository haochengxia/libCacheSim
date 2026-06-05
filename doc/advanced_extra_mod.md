# Extra modules

Extra modules provide a build-time extension mechanism for integrating external cache eviction algorithms into libCacheSim without modifying the main source tree.

This is useful when you want to develop experimental algorithms, maintain private algorithms, or test agent-generated algorithms while still using the same native C interface as built-in libCacheSim eviction algorithms.

Extra modules are compiled into libCacheSim during the CMake build. After compilation, they can be used by `cachesim` in the same way as built-in algorithms.

## When to use extra modules

Use extra modules when:

* You want to implement a new eviction algorithm in C.
* You want the algorithm to use libCacheSim native data structures and cache interfaces.
* You do not want to add experimental code directly into the main libCacheSim repository.
* You want the algorithm to be selected by name from `cachesim`.
* You want a path that is closer to a built-in eviction algorithm than the runtime plugin interface.

If you only need a runtime-loaded policy and do not need native built-in integration, the existing plugin cache mechanism may be simpler.

## Module layout

An extra module is a directory containing a `CMakeLists.txt` file and the source files for the algorithm.

Example layout:

```text
example/evictionAlgoModule/mod_mru/
├── CMakeLists.txt
└── ModMRU.c
```

A module can also contain headers or helper files:

```text
my_extra_modules/
└── mod_mru/
    ├── CMakeLists.txt
    ├── ModMRU.c
    ├── ModMRU.h
    └── helper.c
```

## Module CMakeLists.txt

Each module registers its eviction algorithm with `libcachesim_register_eviction_algo`.

Example:

```cmake
libcachesim_register_eviction_algo(
  NAME ModMRU
  INIT_FN ModMRU_init
  SOURCES
    ModMRU.c
)
```

For modules with headers or multiple source files:

```cmake
libcachesim_register_eviction_algo(
  NAME ModMRU
  INIT_FN ModMRU_init
  SOURCES
    ModMRU.c
    helper.c
  INCLUDE_DIRS
    .
)
```

The `SOURCES` and `INCLUDE_DIRS` paths may be relative to the module directory. The build system converts them to absolute paths automatically.

## Required arguments

### `NAME`

The algorithm name used by `cachesim`.

Example:

```cmake
NAME ModMRU
```

Then the algorithm can be selected as:

```bash
./build/bin/cachesim trace.txt txt ModMRU 0.1
```

Algorithm lookup is case-insensitive if the registry uses normalized names, so `modmru` and `ModMRU` are treated the same.

### `INIT_FN`

The C initialization function for the algorithm.

Example:

```cmake
INIT_FN ModMRU_init
```

The function must have this signature:

```c
cache_t *ModMRU_init(const common_cache_params_t ccache_params,
                     const char *cache_specific_params);
```

### `SOURCES`

The C source files to compile into libCacheSim.

Example:

```cmake
SOURCES
  ModMRU.c
  helper.c
```

## Optional arguments

### `INCLUDE_DIRS`

Additional include directories required by the module.

Example:

```cmake
INCLUDE_DIRS
  .
  include
```

### `COMPILE_DEFINITIONS`

Additional compile definitions for the module.

Example:

```cmake
COMPILE_DEFINITIONS
  ENABLE_MOD_MRU=1
```

### `LINK_LIBS`

Additional libraries required by the module.

Example:

```cmake
LINK_LIBS
  m
```

## Implementing an extra eviction algorithm

An extra eviction algorithm should follow the same native cache interface as built-in libCacheSim eviction algorithms.

At minimum, the module should provide an init function:

```c
#include "libCacheSim/cache.h"
#include "libCacheSim/evictionAlgo.h"

cache_t *ModMRU_init(const common_cache_params_t ccache_params,
                     const char *cache_specific_params) {
  // Initialize and return a cache_t instance.
}
```

Most eviction algorithms also implement the standard cache operations used by libCacheSim, such as `get`, `find`, `insert`, `evict`, `remove`, and `free`, depending on the algorithm design.

A common way to start is to copy an existing simple algorithm, such as `MRU.c` or `LRU.c`, rename the functions, and modify the replacement policy.

## Building with an extra module

From the libCacheSim repository root:

```bash
cmake -S . -B build \
  -DLIBCACHESIM_EXTRA_MODULES_PATH="$PWD/example/evictionAlgoModule/mod_mru"

cmake --build build -j$(nproc)
```

Then run:

```bash
./build/bin/cachesim ./data/cloudPhysicsIO.txt txt ModMRU 0.1
```

If the registry uses case-insensitive lookup, this also works:

```bash
./build/bin/cachesim ./data/cloudPhysicsIO.txt txt modmru 0.1
```

## Passing a module root directory

`LIBCACHESIM_EXTRA_MODULES_PATH` can point either to a single module directory:

```bash
-DLIBCACHESIM_EXTRA_MODULES_PATH="$PWD/example/evictionAlgoModule/mod_mru"
```

or to a directory containing multiple modules:

```bash
-DLIBCACHESIM_EXTRA_MODULES_PATH="$PWD/example/evictionAlgoModule"
```

For example:

```text
example/evictionAlgoModule/
├── mod_mru/
│   ├── CMakeLists.txt
│   └── ModMRU.c
└── mod_lru/
    ├── CMakeLists.txt
    └── ModLRU.c
```

In this case, CMake will scan the child directories and add every directory that contains a `CMakeLists.txt`.

## Passing multiple module paths

`LIBCACHESIM_EXTRA_MODULES_PATH` is a semicolon-separated CMake list.

Example:

```bash
cmake -S . -B build \
  -DLIBCACHESIM_EXTRA_MODULES_PATH="$PWD/modules/mod_mru;$PWD/modules/mod_lru"
```

## How it works

During CMake configuration, libCacheSim performs the following steps:

1. Scans `LIBCACHESIM_EXTRA_MODULES_PATH`.
2. Adds each discovered module with `add_subdirectory`.
3. Each module calls `libcachesim_register_eviction_algo`.
4. The build system collects the module source files.
5. The build system generates an extra registry source file.
6. The extra registry source file is compiled into libCacheSim.
7. At runtime, `cachesim` can find the extra algorithm by name and call its init function.

The generated registry file is placed under the build directory, for example:

```text
build/generated/libcachesim_extra_registry.c
```

For a module registered as:

```cmake
libcachesim_register_eviction_algo(
  NAME ModMRU
  INIT_FN ModMRU_init
  SOURCES ModMRU.c
)
```

the generated registry contains code similar to:

```c
extern cache_t *ModMRU_init(const common_cache_params_t ccache_params,
                            const char *cache_specific_params);

void libcachesim_register_extra_eviction_algos(void) {
  libcachesim_register_eviction_algo("ModMRU", ModMRU_init);
}
```

## Verifying that a module was added

During CMake configuration, you should see output similar to:

```text
-- Adding libCacheSim extra module: /path/to/mod_mru
-- Registered extra eviction algo: ModMRU -> ModMRU_init
```

You should also see the module source file in `cache_sources_c`:

```text
/path/to/mod_mru/ModMRU.c
```

To inspect the generated registry:

```bash
sed -n '1,120p' build/generated/libcachesim_extra_registry.c
```

## Troubleshooting

### CMake cannot find the module source file

Error:

```text
Cannot find source file: ModMRU.c
```

This usually means the module passed a relative source path that was not resolved correctly.

Use a recent version of the extra modules helper that converts relative paths to absolute paths. Alternatively, write absolute paths explicitly:

```cmake
SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/ModMRU.c
```

### `do not support algorithm ModMRU`

This means the module source was compiled, but the runtime registry was not used or was not populated.

Check that:

1. `build/generated/libcachesim_extra_registry.c` exists.
2. The generated file contains `ModMRU_init`.
3. The generated file is included in `cache_sources_c`.
4. `cache_init.h` calls `libcachesim_register_all_eviction_algos()` or `libcachesim_register_extra_eviction_algos()` before calling `libcachesim_find_eviction_algo()`.

### Undefined reference to `libcachesim_register_extra_eviction_algos`

This means the generated registry source file was not compiled into libCacheSim.

Check that the build system appends the generated file to `cache_sources_c` before creating `cache_lib_c`.

### Undefined reference to `libcachesim_find_eviction_algo`

This means the registry implementation was not compiled.

Check that the registry implementation source file is included in the cache source list, for example:

```cmake
eviction/evictionAlgoRegistry.c
```

### Generated registry file is missing semicolons

If the generated file looks like this:

```c
extern cache_t *ModMRU_init(...)

void libcachesim_register_extra_eviction_algos(void) {
  libcachesim_register_eviction_algo("ModMRU", ModMRU_init)
}
```

then the generated C code is invalid.

This can happen because CMake treats semicolons as list separators. The robust approach is to store registry fragments without semicolons in CMake properties and append semicolons only when generating the final registry text.

The generated file should look like this:

```c
extern cache_t *ModMRU_init(const common_cache_params_t ccache_params,
                            const char *cache_specific_params);

void libcachesim_register_extra_eviction_algos(void) {
  libcachesim_register_eviction_algo("ModMRU", ModMRU_init);
}
```

## Example

Example module:

```text
example/evictionAlgoModule/mod_mru/
├── CMakeLists.txt
└── ModMRU.c
```

`CMakeLists.txt`:

```cmake
libcachesim_register_eviction_algo(
  NAME ModMRU
  INIT_FN ModMRU_init
  SOURCES
    ModMRU.c
)
```

Build:

```bash
cmake -S . -B build \
  -DLIBCACHESIM_EXTRA_MODULES_PATH="$PWD/example/evictionAlgoModule/mod_mru"

cmake --build build -j$(nproc)
```

Run:

```bash
./build/bin/cachesim ./data/cloudPhysicsIO.txt txt ModMRU 0.1
```

> [!TIP]
> Avoid the conflicts between your header files and the included header files from original libCacheSim. You can name it as `<you_mod_name>_<purpose>.h`.
