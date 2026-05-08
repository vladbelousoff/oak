cmake_minimum_required(VERSION 3.20)
project(oak VERSION 1.0.0 LANGUAGES C)

option(OAK_TESTS "Build and register tests" OFF)

include(FetchContent)
# Upstream add_library(yyjson) is STATIC when BUILD_SHARED_LIBS is OFF (CMake
# default). Tests are off so FetchContent only builds the library.
set(YYJSON_BUILD_TESTS OFF CACHE BOOL "yyjson: skip tests" FORCE)
FetchContent_Declare(
    yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.10.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(yyjson)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    set(OAK_WARN_FLAGS /W3 /wd4200)
else()
    set(OAK_WARN_FLAGS -Wall -Wextra -Wno-unused-function)
endif()

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
add_library(acorn STATIC ${OAK_CORE_SOURCES})

target_include_directories(acorn
    PUBLIC
    ${OAK_INCLUDE_DIR}
    PRIVATE
    ${OAK_SRC_INCLUDE_DIRS}
)

target_compile_options(acorn PRIVATE ${OAK_WARN_FLAGS})
target_compile_definitions(acorn PRIVATE ${OAK_DEBUG_DEFINES})
target_link_libraries(acorn PUBLIC yyjson)

if(EMSCRIPTEN)
    add_executable(oak_wasm wasm/oak_wasm_entry.c)

    target_link_libraries(oak_wasm PRIVATE acorn)

    target_link_options(oak_wasm PRIVATE
        "-sWASM=1"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=OakModule"
        "-sSTACK_SIZE=1048576"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORTED_FUNCTIONS=['_oak_run_wrapper']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    )

    set_target_properties(oak_wasm PROPERTIES SUFFIX ".js")
else()
    # Executable
    add_executable(oak ${OAK_APP_SOURCES})

    target_link_libraries(oak PRIVATE acorn)

    target_include_directories(oak
        PUBLIC
        ${OAK_INCLUDE_DIR}
        PRIVATE
        ${OAK_SRC_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(oak PRIVATE ${OAK_WARN_FLAGS})
    target_compile_definitions(oak PRIVATE ${OAK_DEBUG_DEFINES})

    if(OAK_TESTS)
    enable_testing()

    foreach(test_src ${OAK_TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)

        add_executable(${test_name} ${test_src})

        target_link_libraries(${test_name} PRIVATE acorn)

        target_include_directories(${test_name}
            PRIVATE
            ${OAK_INCLUDE_DIR}
            ${OAK_SRC_INCLUDE_DIRS}
            ${OAK_TESTS_DIR}/common
        )

        target_compile_options(${test_name} PRIVATE ${OAK_WARN_FLAGS})
        target_compile_definitions(${test_name} PRIVATE ${OAK_DEBUG_DEFINES})

        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()

    # Script tests: run oak on each .oak file with a matching .expected file
    # (top-level scripts/ files plus _main.oak entry points in subdirs) and
    # diff its stdout against that .expected file via cmake/run_script_test.cmake.
    file(GLOB OAK_SCRIPT_TESTS_TOP ${OAK_TESTS_DIR}/scripts/*.oak)
    file(GLOB_RECURSE OAK_SCRIPT_TESTS_NESTED ${OAK_TESTS_DIR}/scripts/*_main.oak)

    foreach(script ${OAK_SCRIPT_TESTS_TOP} ${OAK_SCRIPT_TESTS_NESTED})
        # Compute "imports/basic_main" from absolute path; transform '/' to '_'
        # for the test name.
        file(RELATIVE_PATH script_rel ${OAK_TESTS_DIR}/scripts ${script})
        get_filename_component(script_dir ${script_rel} DIRECTORY)
        get_filename_component(script_stem ${script_rel} NAME_WE)
        if(script_dir STREQUAL "")
            set(script_id ${script_stem})
        else()
            string(REPLACE "/" "_" script_id "${script_dir}/${script_stem}")
        endif()
        set(expected_path ${OAK_TESTS_DIR}/scripts/${script_dir}/${script_stem}.expected)
        if(script_dir STREQUAL "")
            set(expected_path ${OAK_TESTS_DIR}/scripts/${script_stem}.expected)
        endif()
        set(test_name "script_${script_id}")

        if(EXISTS ${expected_path})
            add_test(NAME ${test_name}
                COMMAND ${CMAKE_COMMAND}
                    -DOAK_BIN=$<TARGET_FILE:oak>
                    -DSCRIPT=${script}
                    -DEXPECTED=${expected_path}
                    -DWORKDIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_SOURCE_DIR}/cmake/run_script_test.cmake)
        endif()

        # Error-case variant: script_name.expected_error triggers a test that
        # expects a non-zero exit and stderr containing the given substring.
        set(expected_error_path ${OAK_TESTS_DIR}/scripts/${script_dir}/${script_stem}.expected_error)
        if(script_dir STREQUAL "")
            set(expected_error_path ${OAK_TESTS_DIR}/scripts/${script_stem}.expected_error)
        endif()
        if(EXISTS ${expected_error_path})
            add_test(NAME "error_${test_name}"
                COMMAND ${CMAKE_COMMAND}
                    -DOAK_BIN=$<TARGET_FILE:oak>
                    -DSCRIPT=${script}
                    -DEXPECTED_ERR=${expected_error_path}
                    -DWORKDIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_SOURCE_DIR}/cmake/run_error_script_test.cmake)
        endif()

        if(NOT EXISTS ${expected_path} AND NOT EXISTS ${expected_error_path})
            message(FATAL_ERROR
                "script test entry ${script_rel} has no matching .expected "
                "or .expected_error file")
        endif()
    endforeach()
    endif() # OAK_TESTS
endif()
