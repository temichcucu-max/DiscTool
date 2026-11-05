find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_executable(disktool
    main.cpp
    MainWindow.cpp
    MainWindow.h
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc
)

target_link_libraries(disktool
    PRIVATE
        Qt6::Widgets
        disktool_core
        disktool_warnings
)

# Keep the executable in a predictable place for every generator:
#   build/<preset>/bin/<Config>/disktool.exe
set_target_properties(disktool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

# Qt is linked dynamically (default). Do not add Qt6::Widgets STATIC or
# any static Qt import: dynamic linking is required for the LGPL terms.
