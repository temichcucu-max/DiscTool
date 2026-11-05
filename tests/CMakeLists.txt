if(NOT BUILD_TESTING)
    return()
endif()

find_package(Catch2 3 CONFIG QUIET)

if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

include(Catch)

add_executable(disktool_tests
    scanner_tests.cpp
    tree_builder_tests.cpp
    treemap_tests.cpp
)

target_link_libraries(disktool_tests
    PRIVATE
        disktool_core
        disktool_warnings
        Catch2::Catch2WithMain
)

catch_discover_tests(disktool_tests PROPERTIES TIMEOUT 30)
