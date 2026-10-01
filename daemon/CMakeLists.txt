# SPDX-License-Identifier: MIT
#
# projecteurd: Qt6 background service for the Logitech Spotlight (Ubuntu 26.04, GNOME/Wayland).
#
# Standalone project on purpose: the top-level CMakeLists.txt of this fork is upstream's KDE application,
# which needs Plasma/KPipeWire/LayerShellQt 6.7 and cannot be built on Ubuntu 26.04.
#
#   cmake -S daemon -B build/daemon -G Ninja && cmake --build build/daemon && ctest --test-dir build/daemon
cmake_minimum_required(VERSION 3.20)
project(projecteurd VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
add_compile_options(-Wall -Wextra -Werror)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Test)

# The logic of the pointer effects (hold / freeze / click / double click), free of any I/O.
add_library(effectstate STATIC src/effectstate.cpp src/effectstate.h)
target_include_directories(effectstate PUBLIC src)

# HID++ 2.0 messages: building requests, decoding events.
add_library(hidpp STATIC src/hidpp.cpp src/hidpp.h)
target_include_directories(hidpp PUBLIC src)
target_link_libraries(hidpp PUBLIC Qt6::Core)

enable_testing()
add_executable(test_hidpp tests/test_hidpp.cpp)
target_link_libraries(test_hidpp PRIVATE hidpp Qt6::Test)
add_test(NAME hidpp COMMAND test_hidpp)

add_executable(test_effectstate tests/test_effectstate.cpp)
target_link_libraries(test_effectstate PRIVATE effectstate Qt6::Test)
add_test(NAME effectstate COMMAND test_effectstate)
