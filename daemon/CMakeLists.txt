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

# The version the daemon reports (--version, GetStatus). The package build passes its own, so a bug report names the build.
set(PROJECTEURD_VERSION "${PROJECT_VERSION}" CACHE STRING "version reported by the daemon")
add_compile_definitions(PROJECTEURD_VERSION="${PROJECTEURD_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
add_compile_options(-Wall -Wextra -Werror)

# ---- build flags ---------------------------------------------------------------------------------------------
# The daemon sleeps almost all the time and wakes for ~100 small HID++ reports a second while the button is held: what
# matters is a small binary that loads fast, not -O3 loop optimisation. The package adds the distribution's hardening
# flags (dpkg-buildflags, see packaging/build-deb.sh).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "flags of a release build (CMake's default is -O3)")

option(PROJECTEURD_LTO "link-time optimisation in release builds" ON)
if(PROJECTEURD_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipoSupported OUTPUT ipoError LANGUAGES CXX)
  if(ipoSupported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
  else()
    message(STATUS "no link-time optimisation: ${ipoError}")
  endif()
endif()
# every function and object in its own section, so the linker drops what is never called; no needless libraries
add_compile_options($<$<CONFIG:Release,MinSizeRel>:-ffunction-sections> $<$<CONFIG:Release,MinSizeRel>:-fdata-sections>)
add_link_options(LINKER:--as-needed LINKER:-O1 $<$<CONFIG:Release,MinSizeRel>:LINKER:--gc-sections>)

# Run the tests under sanitizers:  cmake -DPROJECTEURD_SANITIZE=address,undefined -DCMAKE_BUILD_TYPE=Debug ...
set(PROJECTEURD_SANITIZE "" CACHE STRING "comma separated -fsanitize= list for the daemon and its tests (empty: none)")
if(PROJECTEURD_SANITIZE)
  add_compile_options(-fsanitize=${PROJECTEURD_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${PROJECTEURD_SANITIZE})
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Test)

# The logic of the pointer effects (hold / freeze / click / double click), free of any I/O.
add_library(effectstate STATIC src/effectstate.cpp src/effectstate.h)
target_include_directories(effectstate PUBLIC src)

# HID++ 2.0 messages: building requests, decoding events.
add_library(hidpp STATIC src/hidpp.cpp src/hidpp.h)
target_include_directories(hidpp PUBLIC src)
target_link_libraries(hidpp PUBLIC Qt6::Core)

# The remote: HID++ link over a file descriptor, and the device handshake / button events on top of it.
add_library(spotlightcore STATIC src/hidpplink.cpp src/hidpplink.h src/spotlightdevice.cpp src/spotlightdevice.h)
target_include_directories(spotlightcore PUBLIC src)
target_link_libraries(spotlightcore PUBLIC hidpp Qt6::Core)

# Settings, vibration patterns, the presentation timer and the actions of a held Next / Back key.
add_library(logic STATIC
  src/config.cpp src/config.h src/haptics.cpp src/haptics.h src/presentationtimer.cpp src/presentationtimer.h
  src/holdactions.cpp src/holdactions.h src/keysink.cpp src/keysink.h)
target_include_directories(logic PUBLIC src)
target_link_libraries(logic PUBLIC spotlightcore effectstate Qt6::Core)

# Everything around one connected remote: evdev nodes, virtual keyboard, overlay client, discovery.
add_library(daemoncore STATIC
  src/evdevdevice.cpp src/evdevdevice.h src/overlayclient.cpp src/overlayclient.h
  src/remote.cpp src/remote.h src/discovery.cpp src/discovery.h
  src/service.cpp src/service.h src/dbusservice.cpp src/dbusservice.h)
target_include_directories(daemoncore PUBLIC src)
target_link_libraries(daemoncore PUBLIC logic spotlightcore effectstate Qt6::DBus)

add_executable(projecteurd src/main.cpp)
target_link_libraries(projecteurd PRIVATE daemoncore)

enable_testing()
add_executable(test_remote tests/test_remote.cpp tests/fakeremote.h)
target_link_libraries(test_remote PRIVATE daemoncore Qt6::Test)
add_test(NAME remote COMMAND test_remote)

add_executable(test_discovery tests/test_discovery.cpp)
target_link_libraries(test_discovery PRIVATE daemoncore Qt6::Test)
add_test(NAME discovery COMMAND test_discovery)

add_executable(test_spotlightdevice tests/test_spotlightdevice.cpp tests/fakeremote.h)
target_link_libraries(test_spotlightdevice PRIVATE spotlightcore Qt6::Test)
add_test(NAME spotlightdevice COMMAND test_spotlightdevice)

add_executable(test_hidpp tests/test_hidpp.cpp)
target_link_libraries(test_hidpp PRIVATE hidpp Qt6::Test)
add_test(NAME hidpp COMMAND test_hidpp)

add_executable(test_effectstate tests/test_effectstate.cpp)
target_link_libraries(test_effectstate PRIVATE effectstate Qt6::Test)
add_test(NAME effectstate COMMAND test_effectstate)

add_executable(test_config tests/test_config.cpp)
target_link_libraries(test_config PRIVATE logic Qt6::Test)
add_test(NAME config COMMAND test_config)

add_executable(test_timer tests/test_timer.cpp)
target_link_libraries(test_timer PRIVATE logic Qt6::Test)
add_test(NAME timer COMMAND test_timer)

add_executable(test_holdactions tests/test_holdactions.cpp)
target_link_libraries(test_holdactions PRIVATE logic Qt6::Test)
add_test(NAME holdactions COMMAND test_holdactions)

add_executable(test_overlayclient tests/test_overlayclient.cpp)
target_link_libraries(test_overlayclient PRIVATE daemoncore Qt6::Test)
add_test(NAME overlayclient COMMAND test_overlayclient)

add_executable(test_service tests/test_service.cpp tests/fakeremote.h)
target_link_libraries(test_service PRIVATE daemoncore Qt6::Test)
add_test(NAME service COMMAND test_service)

# ---- installation (used by packaging/build-deb.sh; prefix /usr) ---------------------------------------
include(GNUInstallDirs)
set(EXTENSION_UUID "projecteur-overlay@mamrehn.github.io")
install(TARGETS projecteurd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ../packaging/projecteur-setup DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../packaging/projecteurd.service DESTINATION lib/systemd/user)
install(FILES ../packaging/55-projecteurd.rules DESTINATION lib/udev/rules.d)
install(FILES ../packaging/projecteurd-modules.conf DESTINATION lib/modules-load.d RENAME projecteurd.conf)
install(DIRECTORY ../gnome-shell/${EXTENSION_UUID} DESTINATION share/gnome-shell/extensions
        PATTERN "gschemas.compiled" EXCLUDE)
