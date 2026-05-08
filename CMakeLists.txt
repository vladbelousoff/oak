cmake_minimum_required(VERSION 3.20)

project(oak VERSION 1.0.0 LANGUAGES C)

include(cmake/project/options.cmake)
include(cmake/project/dependencies.cmake)
include(cmake/project/layout.cmake)

include(cmake/targets/acorn.cmake)

if(EMSCRIPTEN)
    include(cmake/targets/wasm.cmake)
else()
    include(cmake/targets/cli.cmake)
    include(cmake/testing/tests.cmake)
    include(cmake/testing/examples.cmake)
endif()
