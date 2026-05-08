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
