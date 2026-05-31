# cmake/LibCacheSimExtraModules.cmake

function(_libcachesim_make_abs_paths out_var)
  set(abs_paths "")

  foreach(path IN LISTS ARGN)
    if(IS_ABSOLUTE "${path}")
      list(APPEND abs_paths "${path}")
    else()
      list(APPEND abs_paths "${CMAKE_CURRENT_LIST_DIR}/${path}")
    endif()
  endforeach()

  set(${out_var} "${abs_paths}" PARENT_SCOPE)
endfunction()

function(libcachesim_register_eviction_algo)
  set(options)
  set(oneValueArgs NAME INIT_FN)
  set(multiValueArgs SOURCES INCLUDE_DIRS COMPILE_DEFINITIONS LINK_LIBS)

  cmake_parse_arguments(LCS_ALGO
    "${options}"
    "${oneValueArgs}"
    "${multiValueArgs}"
    ${ARGN}
  )

  if(NOT LCS_ALGO_NAME)
    message(FATAL_ERROR "libcachesim_register_eviction_algo requires NAME")
  endif()

  if(NOT LCS_ALGO_INIT_FN)
    message(FATAL_ERROR "libcachesim_register_eviction_algo requires INIT_FN")
  endif()

  if(NOT LCS_ALGO_SOURCES)
    message(FATAL_ERROR "libcachesim_register_eviction_algo requires SOURCES")
  endif()

  _libcachesim_make_abs_paths(
    LCS_ALGO_ABS_SOURCES
    ${LCS_ALGO_SOURCES}
  )

  set_property(GLOBAL APPEND PROPERTY
    LIBCACHESIM_EXTRA_C_SOURCES
    ${LCS_ALGO_ABS_SOURCES}
  )

  if(LCS_ALGO_INCLUDE_DIRS)
    _libcachesim_make_abs_paths(
      LCS_ALGO_ABS_INCLUDE_DIRS
      ${LCS_ALGO_INCLUDE_DIRS}
    )

    set_property(GLOBAL APPEND PROPERTY
      LIBCACHESIM_EXTRA_INCLUDE_DIRS
      ${LCS_ALGO_ABS_INCLUDE_DIRS}
    )
  endif()

  if(LCS_ALGO_COMPILE_DEFINITIONS)
    set_property(GLOBAL APPEND PROPERTY
      LIBCACHESIM_EXTRA_COMPILE_DEFINITIONS
      ${LCS_ALGO_COMPILE_DEFINITIONS}
    )
  endif()

  if(LCS_ALGO_LINK_LIBS)
    set_property(GLOBAL APPEND PROPERTY
      LIBCACHESIM_EXTRA_LINK_LIBS
      ${LCS_ALGO_LINK_LIBS}
    )
  endif()

  # Store C fragments WITHOUT semicolons.
  # Semicolons are CMake list separators, so add them later when generating text.
  set(decl
    "extern cache_t *${LCS_ALGO_INIT_FN}(const common_cache_params_t ccache_params, const char *cache_specific_params)"
  )

  set(reg
    "  libcachesim_register_eviction_algo(\"${LCS_ALGO_NAME}\", ${LCS_ALGO_INIT_FN})"
  )

  set_property(GLOBAL APPEND PROPERTY
    LIBCACHESIM_EXTRA_ALGO_DECLS
    "${decl}"
  )

  set_property(GLOBAL APPEND PROPERTY
    LIBCACHESIM_EXTRA_ALGO_REGS
    "${reg}"
  )

  set_property(GLOBAL APPEND PROPERTY
    LIBCACHESIM_EXTRA_ALGO_NAMES
    "${LCS_ALGO_NAME}"
  )

  message(STATUS
    "Registered extra eviction algo: ${LCS_ALGO_NAME} -> ${LCS_ALGO_INIT_FN}")
endfunction()

function(_libcachesim_add_one_extra_module module_dir module_name)
  message(STATUS "Adding libCacheSim extra module: ${module_dir}")

  add_subdirectory(
    "${module_dir}"
    "${CMAKE_BINARY_DIR}/libcachesim_extra_modules/${module_name}"
  )
endfunction()

function(libcachesim_discover_extra_modules)
  if(NOT LIBCACHESIM_EXTRA_MODULES_PATH)
    return()
  endif()

  foreach(extra_path IN LISTS LIBCACHESIM_EXTRA_MODULES_PATH)
    if(NOT IS_DIRECTORY "${extra_path}")
      message(FATAL_ERROR
        "LIBCACHESIM_EXTRA_MODULES_PATH entry does not exist: ${extra_path}")
    endif()

    # Case 1: extra_path itself is a module.
    if(EXISTS "${extra_path}/CMakeLists.txt")
      get_filename_component(module_name "${extra_path}" NAME)
      _libcachesim_add_one_extra_module("${extra_path}" "${module_name}")
    else()
      # Case 2: extra_path is a root containing multiple modules.
      file(GLOB children RELATIVE "${extra_path}" "${extra_path}/*")

      foreach(child ${children})
        set(module_dir "${extra_path}/${child}")

        if(IS_DIRECTORY "${module_dir}" AND EXISTS "${module_dir}/CMakeLists.txt")
          _libcachesim_add_one_extra_module("${module_dir}" "${child}")
        endif()
      endforeach()
    endif()
  endforeach()
endfunction()

function(_libcachesim_build_generated_registry_source out_var)
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

  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

  set(generated_registry
    "${CMAKE_BINARY_DIR}/generated/libcachesim_extra_registry.c"
  )

  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/libcachesim_extra_registry.c.in"
    "${generated_registry}"
    @ONLY
  )

  set(${out_var} "${generated_registry}" PARENT_SCOPE)
endfunction()

function(libcachesim_add_extra_modules_to_sources sources_var)
  libcachesim_discover_extra_modules()

  get_property(extra_c_sources
    GLOBAL PROPERTY LIBCACHESIM_EXTRA_C_SOURCES)

  if(extra_c_sources)
    list(APPEND ${sources_var} ${extra_c_sources})
  endif()

  _libcachesim_build_generated_registry_source(generated_registry)

  list(APPEND ${sources_var} "${generated_registry}")

  set(${sources_var} "${${sources_var}}" PARENT_SCOPE)
endfunction()

function(libcachesim_apply_extra_modules_to_target target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR
      "libcachesim_apply_extra_modules_to_target: target does not exist: ${target_name}")
  endif()

  get_property(extra_include_dirs
    GLOBAL PROPERTY LIBCACHESIM_EXTRA_INCLUDE_DIRS)

  if(extra_include_dirs)
    target_include_directories(${target_name} PRIVATE ${extra_include_dirs})
  endif()

  get_property(extra_compile_definitions
    GLOBAL PROPERTY LIBCACHESIM_EXTRA_COMPILE_DEFINITIONS)

  if(extra_compile_definitions)
    target_compile_definitions(${target_name} PRIVATE
      ${extra_compile_definitions}
    )
  endif()

  get_property(extra_link_libs
    GLOBAL PROPERTY LIBCACHESIM_EXTRA_LINK_LIBS)

  if(extra_link_libs)
    target_link_libraries(${target_name} PRIVATE ${extra_link_libs})
  endif()
endfunction()
