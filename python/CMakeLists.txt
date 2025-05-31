# Modified from https://github.com/pybind/scikit_build_example/blob/master/CMakeLists.txt
cmake_minimum_required(VERSION 3.15...3.27)

project(
  ${SKBUILD_PROJECT_NAME}
  VERSION ${SKBUILD_PROJECT_VERSION}
  LANGUAGES CXX)

# find python and pybind11
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

# add libCacheSim library
add_subdirectory(.. ${CMAKE_CURRENT_BINARY_DIR}/libCacheSim)

# create python module
python_add_library(_libcachesim MODULE 
    src/pylibcachesim.cpp 
    WITH_SOABI
)

target_link_libraries(_libcachesim PRIVATE 
    pybind11::headers
    libCacheSim  # link libCacheSim
)

target_compile_definitions(_libcachesim PRIVATE VERSION_INFO=${PROJECT_VERSION})

# install to wheel directory
install(TARGETS _libcachesim DESTINATION libcachesim)