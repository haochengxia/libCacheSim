
add_executable(profilerS3 main.c cli_parser.c prof.c ../cli_reader_utils.c
)
target_link_libraries(profilerS3 ${ALL_MODULES} ${LIBS} ${CMAKE_THREAD_LIBS_INIT} utils) # profilerLib
install(TARGETS profilerS3 RUNTIME DESTINATION bin)

set_target_properties(profilerS3
        PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)
