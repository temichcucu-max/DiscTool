cmake_minimum_required(VERSION 3.21)

project(DiscTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Qt discovery -----------------------------------------------------------
# No Qt path is hard-coded. A user can point CMake at Qt 6 with any of:
#   -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64
#   -DQt6_DIR=C:/Qt/6.7.3/msvc2019_64/lib/cmake/Qt6
#   environment variable CMAKE_PREFIX_PATH (read by CMake itself)
#   environment variable QT_ROOT or QTDIR (handled below for convenience)
if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
elseif(DEFINED ENV{QTDIR} AND NOT "$ENV{QTDIR}" STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()

# Common warning settings for our own targets only. No /WX: warnings from
# third-party headers must not break the build.
add_library(disktool_warnings INTERFACE)
if(MSVC)
    target_compile_options(disktool_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(disktool_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(tests)
