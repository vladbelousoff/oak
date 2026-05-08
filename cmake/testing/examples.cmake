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

    add_test(NAME example_${test_name} COMMAND $<TARGET_FILE:oak> ${script})
    set_tests_properties(example_${test_name} PROPERTIES WORKING_DIRECTORY ${OAK_SOURCE_DIR})

    if(EXISTS ${script_base}.expected_error)
        set_tests_properties(example_${test_name} PROPERTIES WILL_FAIL TRUE)
    endif()
endforeach()
