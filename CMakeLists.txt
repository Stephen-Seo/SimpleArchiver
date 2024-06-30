cmake_minimum_required(VERSION 3.7)
project(SimpleArchiver C)

set(SimpleArchiver_VERSION 1.0)

set(SimpleArchiver_SOURCES
    src/main.c
    src/parser.c
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

add_executable(test_datastructures
    src/data_structures/test.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
    src/data_structures/priority_heap.c
    src/algorithms/linear_congruential_gen.c
)
