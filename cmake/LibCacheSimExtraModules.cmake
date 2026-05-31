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
      LCS_ALGO_ABS_INCLUDE
      ${LCS_ALGO_INCLUDE_DIRS}
    )
    set_property(GLOBAL APPEND PROPERTY
      LIBCACHESIM_EXTRA_INCLUDE_DIRS
      ${LCS_ALGO_ABS_INCLUDE}
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

  message(STATUS "Registered extra eviction algo: ${LCS_ALGO_NAME} -> ${LCS_ALGO_INIT_FN}")
endfunction()
