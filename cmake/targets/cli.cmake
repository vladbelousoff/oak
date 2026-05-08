add_executable(oak ${OAK_APP_SOURCES})

target_link_libraries(oak PRIVATE acorn)

target_include_directories(oak
    PUBLIC
    ${OAK_INCLUDE_DIR}
    PRIVATE
    ${OAK_SRC_INCLUDE_DIRS}
    ${OAK_SOURCE_DIR}
)

target_compile_options(oak PRIVATE ${OAK_WARN_FLAGS})
target_compile_definitions(oak PRIVATE ${OAK_DEBUG_DEFINES})
