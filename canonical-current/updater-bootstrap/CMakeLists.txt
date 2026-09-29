cmake_minimum_required(VERSION 3.24)
project(J3WorshipUpdaterBootstrap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED J3_BOOTSTRAP_VERSION OR
   NOT DEFINED J3_BOOTSTRAP_URL OR
   NOT DEFINED J3_BOOTSTRAP_SHA256)
  message(FATAL_ERROR "Bootstrap requires version, URL and SHA256")
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/BootstrapConfig.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/BootstrapConfig.h"
  @ONLY
)

add_executable(J3WorshipUpdaterBootstrap WIN32 Bootstrap.cpp)
target_include_directories(J3WorshipUpdaterBootstrap PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(J3WorshipUpdaterBootstrap PRIVATE winhttp advapi32 shell32)

if(MSVC)
  set_property(TARGET J3WorshipUpdaterBootstrap PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded${<CONFIG:Debug>:Debug}")
  target_compile_options(J3WorshipUpdaterBootstrap PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(J3WorshipUpdaterBootstrap PROPERTIES
  OUTPUT_NAME "J3Worship-Setup"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../dist-bootstrap"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../dist-bootstrap"
)
