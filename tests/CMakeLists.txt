find_package(Qt6 REQUIRED COMPONENTS Test)

# Each test gets its own config and data directories, so tests never touch
# the real snippets database or settings, and can run without a display.
function(add_snippet_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE snippetcore Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set(test_home "${CMAKE_CURRENT_BINARY_DIR}/${name}-home")
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;HOME=${test_home};XDG_CONFIG_HOME=${test_home}/config;XDG_DATA_HOME=${test_home}/data;XDG_RUNTIME_DIR=${test_home}/runtime")
    file(MAKE_DIRECTORY "${test_home}/runtime")
endfunction()

add_snippet_test(tst_database)
