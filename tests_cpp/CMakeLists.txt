# SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Fork integration tests. Enabled with -DBB_AP_BUILD_TESTS=ON.
# apbackend_test needs BB_AP_SOURCE_ROOT pointing at the bb-archipelago
# checkout and a python that can run it; no game or emulator required.

find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(regular_settings_import_test
    regular_settings_import_test.cpp
    ../settings/RegularSettingsImport.cpp)
target_include_directories(regular_settings_import_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(regular_settings_import_test PRIVATE Qt6::Core Qt6::Test toml11::toml11)
set_target_properties(regular_settings_import_test PROPERTIES CXX_STANDARD 23 AUTOMOC ON)
add_test(NAME regular_settings_import_test COMMAND regular_settings_import_test -o
  "${CMAKE_CURRENT_BINARY_DIR}/regular_settings_import_results.txt,txt")

add_executable(apbackend_test apbackend_test.cpp ../modules/ApBackend.cpp)
target_include_directories(apbackend_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(apbackend_test PRIVATE Qt6::Core Qt6::Test)
set_target_properties(apbackend_test PROPERTIES
    CXX_STANDARD 20
    AUTOMOC ON)

add_test(NAME apbackend_test COMMAND apbackend_test -o
  "${CMAKE_CURRENT_BINARY_DIR}/ap_backend_results.txt,txt")
# Forward a configure-time source root when one was provided; otherwise
# the test inherits the ambient environment (BB_AP_SOURCE_ROOT must be
# set either way, plus a python that can run the backend).
if(DEFINED ENV{BB_AP_SOURCE_ROOT} AND NOT "$ENV{BB_AP_SOURCE_ROOT}" STREQUAL "")
  set_tests_properties(apbackend_test PROPERTIES
    ENVIRONMENT "BB_AP_SOURCE_ROOT=$ENV{BB_AP_SOURCE_ROOT}")
endif()

# End-to-end widget/coordinator test with an in-tree JSON-lines fixture. The
# fixture lives beside the test exe, outside old bundled-backend directories,
# so FindBackend cannot accidentally exercise a stale frozen build.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(apbackend_fixture apbackend_fixture.cpp)
target_link_libraries(apbackend_fixture PRIVATE Qt6::Core)
set_target_properties(apbackend_fixture PROPERTIES
    CXX_STANDARD 20
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture")

add_executable(apui_test
    apui_test.cpp
    ../modules/ApBackend.cpp
    ../modules/ApCoordinator.cpp
    ../modules/ApPage.cpp
    ../modules/EmulatorService.cpp
    ../modules/ModService.cpp
    ../modules/ApBackend.h
    ../modules/ApCoordinator.h
    ../modules/ApPage.h
    ../modules/EmulatorService.h)
target_include_directories(apui_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(apui_test PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)
set_target_properties(apui_test PROPERTIES
    CXX_STANDARD 23
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture")
add_dependencies(apui_test apbackend_fixture)
add_test(NAME apui_test COMMAND apui_test -o
  "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture/results.txt,txt")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture/work")
set_tests_properties(apui_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture/work"
    ENVIRONMENT "BB_AP_BACKEND=$<TARGET_FILE:apbackend_fixture>;BB_AP_STATE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ap_ui_fixture/state;QT_QPA_PLATFORM=offscreen")
