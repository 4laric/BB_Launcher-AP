# SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Fork integration tests. Enabled with -DBB_AP_BUILD_TESTS=ON.
# apbackend_test needs BB_AP_SOURCE_ROOT pointing at the bb-archipelago
# checkout and a python that can run it; no game or emulator required.

find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(apbackend_test apbackend_test.cpp ../modules/ApBackend.cpp)
target_include_directories(apbackend_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(apbackend_test PRIVATE Qt6::Core Qt6::Test)
set_target_properties(apbackend_test PROPERTIES
    CXX_STANDARD 20
    AUTOMOC ON)

add_test(NAME apbackend_test COMMAND apbackend_test)
# Forward a configure-time source root when one was provided; otherwise
# the test inherits the ambient environment (BB_AP_SOURCE_ROOT must be
# set either way, plus a python that can run the backend).
if(DEFINED ENV{BB_AP_SOURCE_ROOT} AND NOT "$ENV{BB_AP_SOURCE_ROOT}" STREQUAL "")
  set_tests_properties(apbackend_test PROPERTIES
    ENVIRONMENT "BB_AP_SOURCE_ROOT=$ENV{BB_AP_SOURCE_ROOT}")
endif()
