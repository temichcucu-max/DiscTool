add_library(disktool_core STATIC
    version.cpp
    scanner.cpp
    tree_builder.cpp
    treemap.cpp
)

target_include_directories(disktool_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(disktool_core
    PRIVATE
        disktool_warnings
)

# Core is deliberately Qt-free: plain C++ that can be unit-tested on its own.
