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
