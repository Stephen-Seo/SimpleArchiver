cmake_minimum_required(VERSION 3.7)
project(SimpleArchiver C)

set(SimpleArchiver_VERSION 1.0)

set(SimpleArchiver_SOURCES
    src/main.c
    src/parser.c
    src/helpers.c
    src/archiver.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
    src/data_structures/priority_heap.c
    src/algorithms/linear_congruential_gen.c
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
    -fstrict-flex-arrays=3
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
    -fstrict-flex-arrays=3
    -fstack-clash-protection -fstack-protector-strong
    -Wl,-z,nodlopen -Wl,-z,noexecstack
    -Wl,-z,relro -Wl,-z,now
    -Wl,--as-needed -Wl,--no-copy-dt-needed-entries
    -fPIE -pie
)

# Inhibit format-string-related warning in src/archiver.c .
set_source_files_properties(src/archiver.c
    PROPERTIES
        COMPILE_FLAGS -Wno-format-nonliteral
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(simplearchiver PUBLIC
        -fno-delete-null-pointer-checks -fno-strict-overflow
        -fno-strict-aliasing -ftrivial-auto-var-init=zero
    )
    target_link_options(simplearchiver PUBLIC
        -fno-delete-null-pointer-checks -fno-strict-overflow
        -fno-strict-aliasing -ftrivial-auto-var-init=zero
    )
endif()

add_executable(test_datastructures
    src/data_structures/test.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
    src/data_structures/priority_heap.c
    src/algorithms/linear_congruential_gen.c
)

add_executable(test_simplearchiver
    src/test.c
    src/parser.c
    src/helpers.c
    src/algorithms/linear_congruential_gen.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
)
