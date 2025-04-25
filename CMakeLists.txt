cmake_minimum_required(VERSION 3.10)
project(SimpleArchiver C)

set(SimpleArchiver_VERSION 1.21)

set(SimpleArchiver_SOURCES
    src/main.c
    src/parser.c
    src/helpers.c
    src/archiver.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
    src/data_structures/chunked_array.c
    src/data_structures/priority_heap.c
    src/algorithms/linear_congruential_gen.c
    src/users.c
)

add_compile_options(
    -Wall -Wextra -Wpedantic -Wno-missing-braces
    $<$<COMPILE_LANGUAGE:CXX>:-Weffc++>
    $<$<CONFIG:DEBUG>:-Og>
)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Debug', none was specified.")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

add_executable(simplearchiver ${SimpleArchiver_SOURCES})

target_compile_options(simplearchiver PUBLIC
    -Wall -Wformat -Wformat=2 -Wconversion -Wimplicit-fallthrough
    -Werror=format-security
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
    -D_GLIBCXX_ASSERTIONS
    -fstack-clash-protection -fstack-protector-strong
    -Wl,-z,nodlopen -Wl,-z,noexecstack
    -Wl,-z,relro -Wl,-z,now
    -Wl,--as-needed -Wl,--no-copy-dt-needed-entries
    -fPIE -pie
)

target_link_options(simplearchiver PUBLIC
    -Wall -Wformat -Wformat=2 -Wconversion -Wimplicit-fallthrough
    -Werror=format-security
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
    -D_GLIBCXX_ASSERTIONS
    -fstack-clash-protection -fstack-protector-strong
    -Wl,-z,nodlopen -Wl,-z,noexecstack
    -Wl,-z,relro -Wl,-z,now
    -Wl,--as-needed -Wl,--no-copy-dt-needed-entries
    -fPIE -pie
)

if(NOT DEFINED DISABLE_FLAGS_FOR_COMPAT)
    target_compile_options(simplearchiver PUBLIC
        -fstrict-flex-arrays=3
    )
    target_link_options(simplearchiver PUBLIC
        -fstrict-flex-arrays=3
    )
endif()

# Inhibit format-string-related warning in src/archiver.c .
set_source_files_properties(src/archiver.c
    PROPERTIES
        COMPILE_FLAGS -Wno-format-nonliteral
)

if(EXISTS /usr/bin/git AND EXISTS /usr/bin/sh AND EXISTS /usr/bin/cat AND EXISTS /usr/bin/xargs AND EXISTS /usr/bin/sed)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/version_string.txt"
        COMMAND /usr/bin/sh ARGS -c "/usr/bin/git describe --long --tags >${CMAKE_CURRENT_BINARY_DIR}/version_string.txt || /usr/bin/echo ${SimpleArchiver_VERSION} >${CMAKE_CURRENT_BINARY_DIR}/version_string.txt"
        COMMAND /usr/bin/sh ARGS -c "/usr/bin/cat ${CMAKE_CURRENT_BINARY_DIR}/version_string.txt | /usr/bin/xargs -Iderp sed -i \"/^#define SIMPLE_ARCHIVER_VERSION_STR /c #define SIMPLE_ARCHIVER_VERSION_STR \\\"derp\\\"\" ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h"
        COMMAND /usr/bin/rm ARGS -f "${CMAKE_CURRENT_BINARY_DIR}/version_string.txt"
        VERBATIM)
    add_custom_target(VersionStringCMD
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/version_string.txt")
    add_dependencies(simplearchiver VersionStringCMD)
else()
    message("Setting version \"${SimpleArchiver_VERSION}\"")
    set_source_files_properties(src/parser.c
        PROPERTIES
            COMPILE_FLAGS "-DSIMPLE_ARCHIVER_VERSION_STR=\\\"${SimpleArchiver_VERSION}\\\""
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(simplearchiver PUBLIC
        -fno-delete-null-pointer-checks -fno-strict-overflow
        -fno-strict-aliasing
    )
    target_link_options(simplearchiver PUBLIC
        -fno-delete-null-pointer-checks -fno-strict-overflow
        -fno-strict-aliasing
    )
    if(NOT DEFINED DISABLE_FLAGS_FOR_COMPAT)
        target_compile_options(simplearchiver PUBLIC
            -ftrivial-auto-var-init=zero
        )
        target_link_options(simplearchiver PUBLIC
            -ftrivial-auto-var-init=zero
        )
    endif()
endif()

add_executable(test_datastructures
    src/data_structures/test.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
    src/data_structures/chunked_array.c
    src/data_structures/priority_heap.c
    src/algorithms/linear_congruential_gen.c
)

add_executable(test_simplearchiver
    src/test.c
    src/parser.c
    src/helpers.c
    src/archiver.c
    src/algorithms/linear_congruential_gen.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
    src/data_structures/chunked_array.c
    src/data_structures/priority_heap.c
    src/users.c
)
