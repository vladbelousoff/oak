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
set(OAK_CORE_SOURCES
        ${OAK_SRC_DIR}/oak_log.c
        ${OAK_SRC_DIR}/oak_mem.c
        ${OAK_SRC_DIR}/oak_arena.c
        ${OAK_SRC_DIR}/oak_lexer.c
        ${OAK_SRC_DIR}/oak_token.c
        ${OAK_SRC_DIR}/oak_parser.c
)

# Tests
set(OAK_TEST_SOURCES
        ${OAK_TESTS_DIR}/0001_lex_empty.c
        ${OAK_TESTS_DIR}/0002_lex_integer.c
        ${OAK_TESTS_DIR}/0003_lex_float.c
        ${OAK_TESTS_DIR}/0004_lex_arithmetic_expression.c
        ${OAK_TESTS_DIR}/0005_lex_ident.c
        ${OAK_TESTS_DIR}/0006_lex_keywords.c
        ${OAK_TESTS_DIR}/0007_lex_ops.c
        ${OAK_TESTS_DIR}/0008_lex_string.c
        ${OAK_TESTS_DIR}/0009_lex_whitespace_newlines.c
        ${OAK_TESTS_DIR}/0010_lex_float_exponent.c
        ${OAK_TESTS_DIR}/0011_parse_expression.c
        ${OAK_TESTS_DIR}/0012_parse_type_decl.c
)

set(OAK_APP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/oak.c
)

# Shared debug-only compile definitions
set(OAK_DEBUG_DEFINES
        $<$<CONFIG:Debug>:OAK_TRACK_MEMORY>
        $<$<CONFIG:Debug>:OAK_DEBUG_LOGGING>
)

# Library
add_library(oak-core STATIC ${OAK_CORE_SOURCES})

target_include_directories(oak-core
        PUBLIC
        ${OAK_INCLUDE_DIR}
        PRIVATE
        ${OAK_SRC_DIR}
)

target_compile_definitions(oak-core
        PRIVATE
        ${OAK_DEBUG_DEFINES}
)

# Executable
add_executable(oak ${OAK_APP_SOURCES})
target_link_libraries(oak PRIVATE oak-core)

target_include_directories(oak
        PUBLIC
        ${OAK_INCLUDE_DIR}
        PRIVATE
        ${OAK_SRC_DIR}
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
            oak-core
    )

    target_include_directories(${test_name}
            PRIVATE
            ${OAK_INCLUDE_DIR}
            ${OAK_SRC_DIR}
            ${OAK_TESTS_DIR}/shared
    )

    target_compile_definitions(${test_name}
            PRIVATE
            ${OAK_DEBUG_DEFINES}
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach ()
