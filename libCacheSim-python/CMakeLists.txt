# Modified from https://github.com/pybind/scikit_build_example/blob/master/CMakeLists.txt
cmake_minimum_required(VERSION 3.15...3.27)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
set(CMAKE_CXX_COMPILE_OBJECT "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
# enable -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

project(
  ${SKBUILD_PROJECT_NAME}
  VERSION ${SKBUILD_PROJECT_VERSION}
  LANGUAGES C CXX)

# use customized cmake module
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/../cmake/Modules/")

# set the logging level to DEBUG by default
set(LOG_LEVEL DEBUG CACHE STRING "change the logging level")

string(TOLOWER "${LOG_LEVEL}" LOG_LEVEL_LOWER)

if(LOG_LEVEL_LOWER STREQUAL "vvverbose")
    add_compile_definitions(LOGLEVEL=3)
elseif(LOG_LEVEL_LOWER STREQUAL "vverbose")
    add_compile_definitions(LOGLEVEL=4)
elseif(LOG_LEVEL_LOWER STREQUAL "verbose")
    add_compile_definitions(LOGLEVEL=5)
elseif(LOG_LEVEL_LOWER STREQUAL "debug")
    add_compile_definitions(LOGLEVEL=6)
elseif(LOG_LEVEL_LOWER STREQUAL "info")
    add_compile_definitions(LOGLEVEL=7)
elseif(LOG_LEVEL_LOWER STREQUAL "warn")
    add_compile_definitions(LOGLEVEL=8)
elseif(LOG_LEVEL_LOWER STREQUAL "error")
    add_compile_definitions(LOGLEVEL=9)

# default none is info
elseif(LOG_LEVEL_LOWER STREQUAL "none")
    add_compile_definitions(LOGLEVEL=7)
else()
    message(WARNING "unknown log level ${LOG_LEVEL}, use INFO as default")
    add_compile_definitions(LOGLEVEL=7)
endif()

option(ENABLE_LRB "enable LRB" ON)
option(ENABLE_GLCACHE "enable GLCache" ON)
option(ENABLE_3L_CACHE "enable 3LCache" ON)
add_compile_definitions(ENABLE_GLCACHE=1)
add_compile_definitions(ENABLE_LRB=1)
add_compile_definitions(ENABLE_3L_CACHE=1)

# find python and pybind11
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

# find glib
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0)

# ------------------------------------------------------------
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/include
    ${GLIB2_INCLUDE_DIRS}
)

file(GLOB UTILS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/utils/*.c"
)

add_library(utils STATIC ${UTILS_SOURCES})
set_target_properties(utils PROPERTIES 
    LANGUAGE C
    POSITION_INDEPENDENT_CODE ON
)
target_compile_options(utils PRIVATE -fPIC)
target_link_libraries(utils PRIVATE ${GLIB2_LIBRARIES})

file(GLOB_RECURSE CACHE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/cache/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/cache/eviction/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/cache/admission/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/cache/prefetch/*.c"
)

# Try to find XGBoost and LightGBM (optional)
find_package(xgboost QUIET)
if(XGBOOST_FOUND)
    message(STATUS "XGBoost found, enabling GLCache")
    include_directories(${XGBOOST_INCLUDE_DIR})
    set(XGBOOST_LIBRARIES xgboost::xgboost)
    add_compile_definitions(ENABLE_XGBOOST=1)
else()
    message(WARNING "XGBoost not found, disabling GLCache")
    add_compile_definitions(ENABLE_XGBOOST=0)
    set(XGBOOST_LIBRARIES "")
endif()

find_path(LIGHTGBM_PATH LightGBM QUIET)
find_library(LIGHTGBM_LIB _lightgbm QUIET)
if(LIGHTGBM_PATH AND LIGHTGBM_LIB)
    message(STATUS "LightGBM found, enabling LRB and 3LCache")
    include_directories(${LIGHTGBM_PATH})
    set(LIGHTGBM_LIBRARIES ${LIGHTGBM_LIB})
    add_compile_definitions(ENABLE_LIGHTGBM=1)
else()
    message(WARNING "LightGBM not found, disabling LRB and 3LCache")
    add_compile_definitions(ENABLE_LIGHTGBM=0)
    set(LIGHTGBM_LIBRARIES "")
endif()

# Only include sources if dependencies are available
if(ENABLE_LIGHTGBM)
    file(GLOB_RECURSE LRB_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/cache/eviction/LRB/*.cpp"
    )
    file(GLOB_RECURSE 3L_CACHE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/cache/eviction/3LCache/*.cpp"
    )
else()
    set(LRB_SOURCES "")
    set(3L_CACHE_SOURCES "")

    # disable the features
    add_compile_definitions(ENABLE_3L_CACHE=0)
    add_compile_definitions(ENABLE_LRB=0)
endif()

file(GLOB_RECURSE CACHE_CPP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/cache/eviction/LHD/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/cache/eviction/cpp/*"
)

file(GLOB_RECURSE DS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/dataStructure/*.c"
)

file(GLOB_RECURSE READER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../libCacheSim/traceReader/*.c"
)

# ------------------------------------------------------------

add_library(cachesim_c STATIC 
    ${UTILS_SOURCES}
    ${CACHE_SOURCES}
    ${LRB_SOURCES}
    ${3L_CACHE_SOURCES}
    ${DS_SOURCES}
    ${READER_SOURCES}
)

set_target_properties(cachesim_c PROPERTIES 
    LANGUAGE C
    POSITION_INDEPENDENT_CODE ON
)

target_compile_options(cachesim_c PRIVATE -fPIC)
target_link_libraries(cachesim_c PRIVATE ${GLIB2_LIBRARIES})

add_library(cachesim_cpp STATIC ${CACHE_CPP_SOURCES})
set_target_properties(cachesim_cpp PROPERTIES 
    LANGUAGE CXX
    POSITION_INDEPENDENT_CODE ON
)
target_compile_options(cachesim_cpp PRIVATE -fPIC)
target_link_libraries(cachesim_cpp PRIVATE ${GLIB2_LIBRARIES})

# create python module
python_add_library(_libcachesim MODULE 
    src/pylibcachesim.cpp
    WITH_SOABI
)

set_target_properties(_libcachesim PROPERTIES 
    POSITION_INDEPENDENT_CODE ON
    INSTALL_RPATH_USE_LINK_PATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
)

target_compile_options(_libcachesim PRIVATE -fPIC)
target_compile_definitions(_libcachesim PRIVATE VERSION_INFO=${PROJECT_VERSION})

target_link_libraries(_libcachesim PRIVATE
    cachesim_c
    cachesim_cpp
    pybind11::headers
    ${GLIB2_LIBRARIES}
    ${XGBOOST_LIBRARIES}
    ${LIGHTGBM_LIBRARIES}
    -Wl,--no-as-needed -ldl
)

# install to wheel directory
install(TARGETS _libcachesim cachesim_c
    LIBRARY DESTINATION libcachesim
)