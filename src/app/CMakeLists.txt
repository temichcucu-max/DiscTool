find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_executable(disktool
    main.cpp
    MainWindow.cpp
    MainWindow.h
    FormatUtils.h
    ScanJob.h
    Theme.cpp
    Theme.h
    TrashWorker.cpp
    TrashWorker.h
    models/DirTreeModel.cpp
    models/DirTreeModel.h
    models/FileTableModel.cpp
    models/FileTableModel.h
    tabs/AnalysisTab.cpp
    tabs/AnalysisTab.h
    tabs/CleanupTab.cpp
    tabs/CleanupTab.h
    tabs/SearchTab.cpp
    tabs/SearchTab.h
    widgets/TreemapWidget.cpp
    widgets/TreemapWidget.h
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc
)

target_include_directories(disktool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# --- Windows executable icon ------------------------------------------------
# Configure a small .rc file in the build tree so the icon path can be
# absolute; this works with every generator and every build directory.
if(WIN32)
    set(APP_ICON_PATH "${PROJECT_SOURCE_DIR}/resources/app.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/disktool.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/disktool.rc"
        @ONLY
    )
    target_sources(disktool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/disktool.rc")
    set_target_properties(disktool PROPERTIES WIN32_EXECUTABLE TRUE)

    # UAC: the MFT/USN scanner needs an elevated volume handle. Qt 6.8+ reads
    # QT_WINDOWS_APP_PROJECT_EXECUTION_LEVEL; the MSVC linker flag enforces the
    # same manifest entry for Qt 6.5-6.7 as well.
    set_target_properties(disktool PROPERTIES
        QT_WINDOWS_APP_PROJECT_EXECUTION_LEVEL "requireAdministrator")
    if(MSVC)
        target_link_options(disktool PRIVATE
            "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    endif()
endif()

target_link_libraries(disktool
    PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        disktool_core
        disktool_warnings
)

if(WIN32)
    target_link_libraries(disktool PRIVATE Shell32 Ole32 Uuid)
endif()

if(WIN32 AND TARGET Qt6::EntryPoint)
    target_link_libraries(disktool PRIVATE Qt6::EntryPoint)
endif()

# --- Portable Release package ----------------------------------------------
# Copy the Qt runtime next to disktool.exe for Release builds. Debug builds
# are skipped inside deploy_qt.cmake so debug DLLs are never overwritten.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "$ENV{QT_ROOT}/bin" "${QT6_INSTALL_PREFIX}/bin"
    )

    add_custom_command(TARGET disktool POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DEXE=$<TARGET_FILE:disktool>"
            "-DWINDEPLOYQT=${WINDEPLOYQT_EXECUTABLE}"
            "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/deploy_qt.cmake"
        COMMENT "Deploying Qt runtime next to disktool.exe (Release only)"
        VERBATIM
    )
endif()

# Keep the executable in a predictable place for every generator:
#   build/<preset>/bin/<Config>/disktool.exe
set_target_properties(disktool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

# Qt is linked dynamically (default). Do not add Qt6::Widgets STATIC or
# any static Qt import: dynamic linking is required for the LGPL terms.
