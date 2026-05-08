file(GLOB OAK_EXAMPLES CONFIGURE_DEPENDS
    ${OAK_EXAMPLES_DIR}/*/*.oak
)

foreach(script ${OAK_EXAMPLES})
    get_filename_component(script_base ${script} DIRECTORY)
    get_filename_component(script_name ${script} NAME_WE)
    set(script_base ${script_base}/${script_name})

    file(RELATIVE_PATH script_rel ${OAK_EXAMPLES_DIR} ${script})
    get_filename_component(script_dir ${script_rel} DIRECTORY)
    if(script_dir STREQUAL script_name)
        set(test_name ${script_name})
    else()
        file(RELATIVE_PATH test_name ${OAK_EXAMPLES_DIR} ${script_base})
        string(REPLACE "/" "_" test_name "${test_name}")
    endif()

    if(EXISTS ${script_base}.expected_error)
        add_test(
            NAME example_${test_name}
            COMMAND ${CMAKE_COMMAND}
                -DOAK_BIN=$<TARGET_FILE:oak>
                -DSCRIPT=${script}
                -DEXPECTED_ERR=${script_base}.expected_error
                -DWORKDIR=${OAK_SOURCE_DIR}
                -P ${OAK_SOURCE_DIR}/cmake/testing/check_expected_error.cmake
        )
    else()
        add_test(NAME example_${test_name} COMMAND $<TARGET_FILE:oak> ${script})
        set_tests_properties(example_${test_name} PROPERTIES WORKING_DIRECTORY ${OAK_SOURCE_DIR})
    endif()
endforeach()
