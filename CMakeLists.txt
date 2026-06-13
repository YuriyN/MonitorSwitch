cmake_minimum_required(VERSION 3.16)

project(monitor-switch
    VERSION 0.2.0
    DESCRIPTION "A simple CLI app to switch inputs on a DDC/CI-supported monitor"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(CompilerWarnings)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Core library: parsing, selection, and dispatch logic. Deliberately free of
# ncurses so it can be unit tested without a terminal.
add_library(monitor_switch_core STATIC
    src/inputs.cpp
    src/ddc.cpp
    src/process.cpp
    src/menu.cpp
    src/cli.cpp
)
target_include_directories(monitor_switch_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
monitor_switch_set_warnings(monitor_switch_core)

# Production executable. Only this target links the terminal interface.
find_package(Curses REQUIRED)

add_executable(monitor-switch
    src/main.cpp
    src/tui.cpp
)
target_link_libraries(monitor-switch PRIVATE monitor_switch_core ${CURSES_LIBRARIES})
target_include_directories(monitor-switch PRIVATE ${CURSES_INCLUDE_DIRS})
monitor_switch_set_warnings(monitor-switch)

install(TARGETS monitor-switch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Tests
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
