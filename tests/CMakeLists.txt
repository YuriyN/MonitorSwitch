# Single test runner covering the ncurses-free core. The custom framework keeps
# the configure step offline (no downloaded dependencies).
add_executable(monitor_switch_tests
    test_main.cpp
    test_inputs.cpp
    test_ddc.cpp
    test_menu.cpp
    test_cli.cpp
    test_process.cpp
)
target_link_libraries(monitor_switch_tests PRIVATE monitor_switch_core)
target_include_directories(monitor_switch_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
monitor_switch_set_warnings(monitor_switch_tests)

add_test(NAME monitor_switch_tests COMMAND monitor_switch_tests)
