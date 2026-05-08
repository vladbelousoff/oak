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
