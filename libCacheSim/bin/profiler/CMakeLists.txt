
add_executable(profiler main.cpp ../cachesim/cli_parser.c ../cli_reader_utils.c)
target_link_libraries(profiler profilerLib ${ALL_MODULES} ${LIBS} ${CMAKE_THREAD_LIBS_INIT} utils)
install(TARGETS profiler RUNTIME DESTINATION bin)

set_target_properties(profiler
        PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)
