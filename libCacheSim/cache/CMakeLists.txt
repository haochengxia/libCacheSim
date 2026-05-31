# ==============================================================================
# Cache Library
# ==============================================================================

# ==============================================================================
# Admission
# ==============================================================================
# C admission algorithms
set(admission_sources_c
    admission/bloomfilter.c
    admission/prob.c
    admission/size.c
    admission/sizeProbabilistic.c
)

# C++ admission algorithms
set(admission_sources_cpp
    admission/adaptsize/adaptsize.cpp
    admission/adaptsize/adaptsize_interface.cpp
)

# ==============================================================================
# Eviction
# ==============================================================================

# C eviction algorithms
set(eviction_sources_c
        eviction/ARC.c
        eviction/ARCv0.c         # an inefficient version but easier to understand
        eviction/Belady.c
        eviction/BeladySize.c
        eviction/CAR.c
        eviction/Cacheus.c
        eviction/Clock.c
        eviction/Clock2QPlus.c
        eviction/ClockPro.c
        eviction/FIFO.c
        eviction/Hyperbolic.c
        eviction/LeCaR.c
        eviction/LeCaRv0.c       # an inefficient version but easier to understand
        eviction/LFU.c
        eviction/LFUDA.c
        eviction/LIRS.c
        eviction/LRU.c
        eviction/LRUProb.c       # a probabilistic version of LRU
        eviction/LRUv0.c         # an inefficient version but easier to understand
        eviction/MRU.c
        eviction/nop.c
        eviction/plugin_cache.c  # plugin cache that allows user to implement custom cache
        eviction/evictionAlgoRegistry.c   # allow extra module registry
        eviction/QDLP.c
        eviction/Random.c
        eviction/RandomLRU.c
        eviction/RandomTwo.c
        eviction/S3FIFO.c
        eviction/Sieve.c
        eviction/Size.c
        eviction/SLRU.c
        eviction/SLRUv0.c        # an inefficient version but easier to understand
        eviction/TwoQ.c
        eviction/WTinyLFU.c

        # these are sub-algorithms used by other algorithms
        eviction/CR_LFU.c
        eviction/SR_LRU.c

        # these are algorithms used for comparison
        eviction/FIFO_Merge.c
        eviction/FIFO_Reinsertion.c
        eviction/S3FIFOv0.c
        eviction/S3FIFOd.c
        eviction/other/flashProb.c
        eviction/other/S3LRU.c
)

# C++ eviction algorithms
set(eviction_sources_cpp
        eviction/cpp/LFU.cpp
        eviction/cpp/GDSF.cpp
        eviction/cpp/LRU_K.cpp
        eviction/LHD/lhd.cpp
        eviction/LHD/LHD_Interface.cpp
        )

# LRB
if (ENABLE_LRB)
    set (eviction_sources_cpp ${eviction_sources_cpp}
        eviction/LRB/lrb.cpp
        eviction/LRB/LRB_Interface.cpp
        )
endif()

# 3L Cache
if (ENABLE_3L_CACHE)
    set (eviction_sources_cpp ${eviction_sources_cpp}
        eviction/3LCache/ThreeLCache_Interface.cpp
        eviction/3LCache/ThreeLCache.cpp
        )
endif()

# GLCache
if (ENABLE_GLCACHE)
    set(eviction_sources_c
        ${eviction_sources_c}
        eviction/GLCache/bucket.c
        eviction/GLCache/dataPrep.c
        eviction/GLCache/eviction.c
        eviction/GLCache/GLCache.c
        eviction/GLCache/inference.c
        eviction/GLCache/init.c
        eviction/GLCache/segment.c
        eviction/GLCache/segSel.c
        eviction/GLCache/train.c
        )
endif()


# ==============================================================================
# Prefetch
# ==============================================================================

# C prefetch algorithms
set(prefetch_sources_c
    prefetch/Mithril.c
    prefetch/OBL.c
    prefetch/PG.c
)

# ==============================================================================
# Cache
# ==============================================================================
# Main cache library
set(cache_sources_c
    ${admission_sources_c}
    ${eviction_sources_c}
    ${prefetch_sources_c}
    cache.c
    plugin.c
)

# Extra module begin - will invoke functions in ExtraModules.cmake
include(${CMAKE_SOURCE_DIR}/cmake/LibCacheSimExtraModules.cmake)

if(LIBCACHESIM_EXTRA_MODULES_PATH)
  foreach(extra_path IN LISTS LIBCACHESIM_EXTRA_MODULES_PATH)
    if(NOT IS_DIRECTORY "${extra_path}")
      message(FATAL_ERROR
        "LIBCACHESIM_EXTRA_MODULES_PATH entry does not exist: ${extra_path}")
    endif()

    if(EXISTS "${extra_path}/CMakeLists.txt")
      get_filename_component(module_name "${extra_path}" NAME)
      message(STATUS "Adding libCacheSim extra module: ${extra_path}")
      add_subdirectory(
        "${extra_path}"
        "${CMAKE_BINARY_DIR}/libcachesim_extra_modules/${module_name}"
      )
    else()
      file(GLOB children RELATIVE "${extra_path}" "${extra_path}/*")
      foreach(child ${children})
        set(module_dir "${extra_path}/${child}")
        if(IS_DIRECTORY "${module_dir}" AND EXISTS "${module_dir}/CMakeLists.txt")
          message(STATUS "Adding libCacheSim extra module: ${module_dir}")
          add_subdirectory(
            "${module_dir}"
            "${CMAKE_BINARY_DIR}/libcachesim_extra_modules/${child}"
          )
        endif()
      endforeach()
    endif()
  endforeach()
endif()

get_property(LIBCACHESIM_EXTRA_C_SOURCES
  GLOBAL PROPERTY LIBCACHESIM_EXTRA_C_SOURCES)

if(LIBCACHESIM_EXTRA_C_SOURCES)
  list(APPEND cache_sources_c ${LIBCACHESIM_EXTRA_C_SOURCES})
endif()

get_property(LIBCACHESIM_EXTRA_ALGO_DECLS_LIST
  GLOBAL PROPERTY LIBCACHESIM_EXTRA_ALGO_DECLS)

get_property(LIBCACHESIM_EXTRA_ALGO_REGS_LIST
  GLOBAL PROPERTY LIBCACHESIM_EXTRA_ALGO_REGS)

if(LIBCACHESIM_EXTRA_ALGO_DECLS_LIST)
  string(JOIN "\n" LIBCACHESIM_EXTRA_ALGO_DECLS
    ${LIBCACHESIM_EXTRA_ALGO_DECLS_LIST})
else()
  set(LIBCACHESIM_EXTRA_ALGO_DECLS "")
endif()

if(LIBCACHESIM_EXTRA_ALGO_REGS_LIST)
get_property(LIBCACHESIM_EXTRA_ALGO_DECLS_LIST
  GLOBAL PROPERTY LIBCACHESIM_EXTRA_ALGO_DECLS)

get_property(LIBCACHESIM_EXTRA_ALGO_REGS_LIST
  GLOBAL PROPERTY LIBCACHESIM_EXTRA_ALGO_REGS)

set(LIBCACHESIM_EXTRA_ALGO_DECLS "")
if(LIBCACHESIM_EXTRA_ALGO_DECLS_LIST)
  foreach(decl IN LISTS LIBCACHESIM_EXTRA_ALGO_DECLS_LIST)
    string(APPEND LIBCACHESIM_EXTRA_ALGO_DECLS "${decl};\n")
  endforeach()
endif()

set(LIBCACHESIM_EXTRA_ALGO_REGS "")
if(LIBCACHESIM_EXTRA_ALGO_REGS_LIST)
  foreach(reg IN LISTS LIBCACHESIM_EXTRA_ALGO_REGS_LIST)
    string(APPEND LIBCACHESIM_EXTRA_ALGO_REGS "${reg};\n")
  endforeach()
endif()
else()
  set(LIBCACHESIM_EXTRA_ALGO_REGS "")
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/libcachesim_extra_registry.c.in"
  "${CMAKE_BINARY_DIR}/generated/libcachesim_extra_registry.c"
  @ONLY
)

list(APPEND cache_sources_c
  "${CMAKE_BINARY_DIR}/generated/libcachesim_extra_registry.c"
)
# Extra module end

set(cache_sources_cpp
    ${admission_sources_cpp}
    ${eviction_sources_cpp}
)

set(cache_sources
    ${cache_sources_c}
    ${cache_sources_cpp}
)

# Also set for parent scope if needed
set(cache_sources_c ${cache_sources_c} PARENT_SCOPE)
set(cache_sources_cpp ${cache_sources_cpp} PARENT_SCOPE)
set(cache_sources ${cache_sources} PARENT_SCOPE)

message(STATUS "cache_sources_c = ${cache_sources_c}")
message(STATUS "cache_sources_cpp = ${cache_sources_cpp}")

add_library(cache_lib_c OBJECT ${cache_sources_c})
add_library(cache_lib_cpp OBJECT ${cache_sources_cpp})

# Set target-specific compiler flags
target_compile_options(cache_lib_c PRIVATE
    ${LIBCACHESIM_C_FLAGS}
)

target_compile_options(cache_lib_cpp PRIVATE
    ${LIBCACHESIM_CXX_FLAGS}
)

target_include_directories(cache_lib_c PRIVATE
    ${GLib_INCLUDE_DIRS}
    ${libCacheSim_include_dir}
)

target_include_directories(cache_lib_cpp PRIVATE
    ${GLib_INCLUDE_DIRS}
    ${libCacheSim_include_dir}
)

# extra module include
get_property(LIBCACHESIM_EXTRA_INCLUDE_DIRS
  GLOBAL PROPERTY LIBCACHESIM_EXTRA_INCLUDE_DIRS)
if(LIBCACHESIM_EXTRA_INCLUDE_DIRS)
  target_include_directories(cache_lib_c PRIVATE
    ${LIBCACHESIM_EXTRA_INCLUDE_DIRS}
  )
endif()

target_link_libraries(cache_lib_c PRIVATE dataStructure_lib utils_lib ${dependency_libs})
target_link_libraries(cache_lib_cpp PRIVATE dataStructure_lib utils_lib ${dependency_libs})

# Set C++ standard for C++ library
set_target_properties(cache_lib_cpp PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
