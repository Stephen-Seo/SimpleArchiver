cmake_minimum_required(VERSION 3.10)
project(SimpleArchiver C)

set(SimpleArchiver_VERSION 2.6.2)

set(SimpleArchiver_SOURCES
    src/main.c
    src/parser.c
    src/helpers.c
    src/archiver.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
    src/data_structures/chunked_array.c
    src/data_structures/list_array.c
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

if(DEFINED BUNDLED_LIBCAP AND BUNDLED_LIBCAP)
    message(STATUS "bundled libcap ENABLED")
    set(ENABLE_LIBCAP_LIB "ON")
    set(ENABLE_BUNDLED_LIBCAP "ON")
elseif(NOT DEFINED ENABLE_LIBCAP OR NOT ENABLE_LIBCAP)
    message(STATUS "libcap NOT enabled")
    set(ENABLE_LIBCAP_LIB "OFF")
else()
    message(STATUS "libcap ENABLED")
    set(ENABLE_LIBCAP_LIB "ON")
    set(ENABLE_LIBCAP_LIB_LINK "ON")
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
    $<IF:$<STREQUAL:"${ENABLE_LIBCAP_LIB}","ON">,-DENABLE_LIBCAP,>
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

if(ENABLE_LIBCAP_LIB_LINK)
    target_link_libraries(simplearchiver PRIVATE cap)
endif()

if(NOT DEFINED DISABLE_FLAGS_FOR_COMPAT)
    target_compile_options(simplearchiver PUBLIC
        -fstrict-flex-arrays=3
    )
    target_link_options(simplearchiver PUBLIC
        -fstrict-flex-arrays=3
    )
endif()

if(BUILD_WITH_DEBUGGING_INFO)
    target_compile_options(simplearchiver PUBLIC -g)
    target_link_options(simplearchiver PUBLIC -g)
endif()

# Inhibit format-string-related warning in src/archiver.c .
set_source_files_properties(src/archiver.c
    PROPERTIES
        COMPILE_FLAGS -Wno-format-nonliteral
)

if(DEFINED SDSA_OVERRIDE_VERSION_STRING)
    message("Setting version \"${SDSA_OVERRIDE_VERSION_STRING}\"...")
    if(EXISTS /usr/bin/sed)
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dummy_output_override_version.txt"
            COMMAND /usr/bin/sed ARGS -i "/^#define SIMPLE_ARCHIVER_VERSION_STR /c #define SIMPLE_ARCHIVER_VERSION_STR \\\"${SDSA_OVERRIDE_VERSION_STRING}\\\"" "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h"
            VERBATIM
        )
        add_custom_target(VersionStringCMD
            DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/dummy_output_override_version.txt")
        add_dependencies(simplearchiver VersionStringCMD)
    else()
        set_source_files_properties(src/parser.c
            PROPERTIES
                COMPILE_FLAGS "-DSIMPLE_ARCHIVER_VERSION_STR=\\\"${SDSA_OVERRIDE_VERSION_STRING}\\\""
        )
    endif()
elseif(EXISTS /usr/bin/git AND EXISTS /usr/bin/sh AND EXISTS /usr/bin/cat AND EXISTS /usr/bin/xargs AND EXISTS /usr/bin/sed)
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
    src/data_structures/list_array.c
    src/data_structures/priority_heap.c
    src/algorithms/linear_congruential_gen.c
    src/helpers.c
)
if(ENABLE_LIBCAP_LIB_LINK)
    target_link_libraries(test_datastructures PRIVATE cap)
endif()

add_executable(test_simplearchiver
    src/test.c
    src/parser.c
    src/helpers.c
    src/archiver.c
    src/algorithms/linear_congruential_gen.c
    src/data_structures/linked_list.c
    src/data_structures/hash_map.c
    src/data_structures/chunked_array.c
    src/data_structures/list_array.c
    src/data_structures/priority_heap.c
    src/users.c
)
if(ENABLE_LIBCAP_LIB_LINK)
    target_link_libraries(test_simplearchiver PRIVATE cap)
endif()

if(ENABLE_BUNDLED_LIBCAP)
    if(DEFINED CROSS_CC)
        string(LENGTH ${CROSS_CC} CC_LENGTH)
        string(FIND "${CROSS_CC}" "/" CC_SLASH_INDEX REVERSE)
        if(CC_SLASH_INDEX EQUAL "-1")
            string(FIND "${CROSS_CC}" "gcc" CC_INDEX REVERSE)
            string(SUBSTRING ${CROSS_CC} 0 ${CC_INDEX} CROSS_PREFIX_WITH_DASH)
            string(FIND "${CROSS_CC}" "-gcc" CC_INDEX REVERSE)
            string(SUBSTRING ${CROSS_CC} 0 ${CC_INDEX} CROSS_PREFIX)
        else()
            string(SUBSTRING "${CROSS_CC}" ${CC_SLASH_INDEX} -1 CROSS_FULL_CC)
            string(FIND "${CROSS_FULL_CC}" "gcc" CC_INDEX REVERSE)
            math(EXPR CC_INDEX "${CC_INDEX} - 1")
            string(SUBSTRING "${CROSS_FULL_CC}" 1 ${CC_INDEX} CROSS_PREFIX_WITH_DASH)
            string(FIND "${CROSS_FULL_CC}" "-gcc" CC_INDEX REVERSE)
            math(EXPR CC_INDEX "${CC_INDEX} - 1")
            string(SUBSTRING "${CROSS_FULL_CC}" 1 ${CC_INDEX} CROSS_PREFIX)
        endif()
        set(LIBCAP_CROSS_LIB "/usr/${CROSS_PREFIX}/lib")
    endif()
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/third_party/lib/libcap.a"
        COMMAND mkdir -p "${CMAKE_CURRENT_BINARY_DIR}/third_party/lib"
        COMMAND mkdir -p "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        COMMAND test -e "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcap-2.77.tar.gz" || curl -L -o "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcap-2.77.tar.gz" "https://git.kernel.org/pub/scm/libs/libcap/libcap.git/snapshot/libcap-2.77.tar.gz"
        COMMAND sha256sum "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcap-2.77.tar.gz" | grep "5e339f5ab434bf5b7979f65437ca24942ddcd6e6a3e57347370cd791bc0ea51c"
        COMMAND rm -rf "${CMAKE_CURRENT_BINARY_DIR}/third_party/libcap-2.77"
        COMMAND tar -xf "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcap-2.77.tar.gz" -C "${CMAKE_CURRENT_BINARY_DIR}/third_party"
        COMMAND env CROSS_COMPILE=${CROSS_PREFIX_WITH_DASH} lib=${LIBCAP_CROSS_LIB} PAM_CAP=no BUILD_CC=/usr/bin/gcc SHARED=no LIBCSTATIC=yes make -C "${CMAKE_CURRENT_BINARY_DIR}/third_party/libcap-2.77/libcap"
        COMMAND install -D -m644 "${CMAKE_CURRENT_BINARY_DIR}/third_party/libcap-2.77/libcap/libcap.a" "${CMAKE_CURRENT_BINARY_DIR}/third_party/lib/libcap.a"
        COMMAND cp -r "${CMAKE_CURRENT_BINARY_DIR}/third_party/libcap-2.77/libcap/include" "${CMAKE_CURRENT_BINARY_DIR}/third_party/"
        COMMAND cp -r "${CMAKE_CURRENT_BINARY_DIR}/third_party/libcap-2.77/psx" "${CMAKE_CURRENT_BINARY_DIR}/third_party/"
        COMMAND ln -srf "${CMAKE_CURRENT_BINARY_DIR}/third_party/psx/psx_syscall.h" "${CMAKE_CURRENT_BINARY_DIR}/third_party/include/sys/psx_syscall.h"
        COMMAND rm -rf "${CMAKE_CURRENT_BINARY_DIR}/third_party/libcap-2.77"
        VERBATIM)
    add_custom_target(BUNDLED_LIBCAP_TARGET DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/third_party/lib/libcap.a")

    add_dependencies(simplearchiver BUNDLED_LIBCAP_TARGET)
    add_dependencies(test_datastructures BUNDLED_LIBCAP_TARGET)
    add_dependencies(test_simplearchiver BUNDLED_LIBCAP_TARGET)

    target_link_libraries(simplearchiver PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/third_party/lib/libcap.a")
    target_link_libraries(test_datastructures PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/third_party/lib/libcap.a")
    target_link_libraries(test_simplearchiver PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/third_party/lib/libcap.a")

    target_include_directories(simplearchiver PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/third_party/include")
    target_include_directories(test_datastructures PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/third_party/include")
    target_include_directories(test_simplearchiver PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/third_party/include")
endif()
