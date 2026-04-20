cmake_minimum_required(VERSION 3.20)
project(oak VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif ()

# Directories
set(OAK_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(OAK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(OAK_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# Sources
file(GLOB_RECURSE OAK_CORE_SOURCES ${OAK_SRC_DIR}/*.c)

set(OAK_SRC_INCLUDE_DIRS
        ${OAK_SRC_DIR}
        ${OAK_SRC_DIR}/common
        ${OAK_SRC_DIR}/runtime
        ${OAK_SRC_DIR}/lexer
        ${OAK_SRC_DIR}/parser
        ${OAK_SRC_DIR}/compiler
        ${OAK_SRC_DIR}/vm
)

# Tests
file(GLOB_RECURSE OAK_TEST_SOURCES ${OAK_TESTS_DIR}/*/*.c)

set(OAK_APP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/oak.c
        ${CMAKE_CURRENT_SOURCE_DIR}/oak_cli.c
)

# Shared debug-only compile definitions
set(OAK_DEBUG_DEFINES
        $<$<CONFIG:Debug>:OAK_TRACK_MEMORY>
        $<$<CONFIG:Debug>:OAK_DEBUG_LOGGING>
)

# Library
add_library(liboak STATIC ${OAK_CORE_SOURCES})

target_include_directories(liboak
        PUBLIC
        ${OAK_INCLUDE_DIR}
        PRIVATE
        ${OAK_SRC_INCLUDE_DIRS}
)

target_compile_definitions(liboak PRIVATE ${OAK_DEBUG_DEFINES})

# Executable
add_executable(oak ${OAK_APP_SOURCES})
target_link_libraries(oak PRIVATE liboak)

target_include_directories(oak
        PUBLIC
        ${OAK_INCLUDE_DIR}
        PRIVATE
        ${OAK_SRC_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(oak
        PRIVATE
        ${OAK_DEBUG_DEFINES}
)

enable_testing()

foreach (test_src ${OAK_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})

    target_link_libraries(${test_name}
            PRIVATE
            liboak
    )

    target_include_directories(${test_name}
            PRIVATE
            ${OAK_INCLUDE_DIR}
            ${OAK_SRC_INCLUDE_DIRS}
            ${OAK_TESTS_DIR}/common
    )

    target_compile_definitions(${test_name}
            PRIVATE
            ${OAK_DEBUG_DEFINES}
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach ()

# Script tests
file(GLOB OAK_SCRIPT_TESTS ${OAK_TESTS_DIR}/scripts/*.oak)

foreach (script ${OAK_SCRIPT_TESTS})
    get_filename_component(script_name ${script} NAME_WE)
    set(test_name "script_${script_name}")

    add_test(NAME ${test_name} COMMAND oak ${script})
endforeach ()
