cmake_minimum_required(VERSION 3.28...3.30)

# ---------------------------------------------------------------------------
# Stream Mix - OBS Studio plugin
#
# Uses the official OBS plugin-template toolchain (cmake/ + .github/). Project
# name/version/author come from buildspec.json via bootstrap.cmake.
# ---------------------------------------------------------------------------

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# This plugin requires the frontend API (Tools menu, streaming service, docks).
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Build the Qt settings dock" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# This tree ships without a local OBS SDK to compile-test against, so a stray
# warning shouldn't turn the first CI build red. Re-enable for hardening once
# you have a green build (or pass -DCMAKE_COMPILE_WARNING_AS_ERROR=ON).
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(NOT ENABLE_FRONTEND_API)
  message(FATAL_ERROR "stream-mix requires ENABLE_FRONTEND_API=ON")
endif()
find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/stream-mix-audio.cpp
    src/stream-mix-hook.cpp
    src/stream-mix-config.cpp)

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/stream-mix-dock.cpp)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_QT)
endif()

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_compile_definitions(${CMAKE_PROJECT_NAME}
                           PRIVATE PLUGIN_VERSION="${PLUGIN_VERSION}")

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME
                                                             ${_name})
