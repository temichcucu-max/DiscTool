# Runs windeployqt for Release builds only. Invoked from a POST_BUILD step:
#   cmake -DEXE=... -DWINDEPLOYQT=... -DCONFIG=... -P deploy_qt.cmake
#
# Keeping the config check here (instead of in CMakeLists.txt) avoids putting
# release Qt DLLs next to the Debug executable.

if(NOT CONFIG STREQUAL "Release")
    return()
endif()

if(NOT WINDEPLOYQT OR NOT EXISTS "${WINDEPLOYQT}")
    message(FATAL_ERROR
        "windeployqt was not found. Set QT_ROOT to a Qt 6 MSVC installation "
        "(for example C:/Qt/6.7.3/msvc2019_64).")
endif()

message(STATUS "Deploying Qt runtime with: ${WINDEPLOYQT}")
execute_process(
    COMMAND "${WINDEPLOYQT}"
        --release
        --no-translations
        --no-system-d3d-compiler
        --no-opengl-sw
        "${EXE}"
    RESULT_VARIABLE deploy_result
)

if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed with exit code ${deploy_result}")
endif()

message(STATUS "Qt runtime deployed next to ${EXE}")
