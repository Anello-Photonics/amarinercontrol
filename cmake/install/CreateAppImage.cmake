# CreateAppImage.cmake
message(STATUS "QGC: Creating AppImage")

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(APPDIR_PATH   "${CMAKE_INSTALL_PREFIX}/..")
set(APPIMAGE_PATH "${CMAKE_INSTALL_PREFIX}/../${CMAKE_PROJECT_NAME}-${CMAKE_SYSTEM_PROCESSOR}.AppImage")

# STAGE_ROOT is the directory that contains usr/, typically:
# .../_CPack_Packages/Linux/TGZ/<pkg>/..
get_filename_component(STAGE_ROOT "${APPDIR_PATH}" ABSOLUTE)

message(STATUS "REPO_ROOT=${REPO_ROOT}")
message(STATUS "CMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}")
message(STATUS "APPDIR_PATH=${APPDIR_PATH}")
message(STATUS "STAGE_ROOT=${STAGE_ROOT}")
message(STATUS "APPIMAGE_PATH=${APPIMAGE_PATH}")

# ---------------------------------------------------------------------------
# Download tools
function(download_tool VAR URL)
    cmake_path(GET URL FILENAME _name)
    set(_dest "${CMAKE_INSTALL_PREFIX}/../tools/${_name}")
    if(NOT EXISTS "${_dest}")
        file(MAKE_DIRECTORY "${CMAKE_INSTALL_PREFIX}/../tools")
        message(STATUS "QGC: Downloading ${_name} to ${_dest}")
        file(DOWNLOAD "${URL}" "${_dest}" STATUS _status)
        list(GET _status 0 _result)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "Failed to download ${URL} to ${_dest}: ${_status}")
        endif()
        file(CHMOD "${_dest}"
             FILE_PERMISSIONS
                OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE)
    endif()
    set(${VAR}_PATH "${_dest}" PARENT_SCOPE)
endfunction()

download_tool(LINUXDEPLOY  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${CMAKE_SYSTEM_PROCESSOR}.AppImage")
download_tool(APPIMAGETOOL "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${CMAKE_SYSTEM_PROCESSOR}.AppImage")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    download_tool(APPIMAGELINT "https://github.com/TheAssassin/appimagelint/releases/download/continuous/appimagelint-${CMAKE_SYSTEM_PROCESSOR}.AppImage")
endif()

# ---------------------------------------------------------------------------
# Ensure executable exists inside AppDir/usr/bin
file(MAKE_DIRECTORY "${APPDIR_PATH}/usr/bin")
set(EXE_PATH "${APPDIR_PATH}/usr/bin/${CMAKE_PROJECT_NAME}")

if(NOT EXISTS "${EXE_PATH}")
    message(STATUS "QGC: EXE not found in AppDir, searching...")

    # Known likely locations (relative to STAGE_ROOT and repo)
    set(_exe_candidates
        "${STAGE_ROOT}/Release/${CMAKE_PROJECT_NAME}"
        "${STAGE_ROOT}/qml/QGroundControl"
        "${REPO_ROOT}/build/Desktop_Qt_6_8_3-Release/Release/${CMAKE_PROJECT_NAME}"
        "${REPO_ROOT}/build/Desktop_Qt_6_8_3-Release/qml/QGroundControl"
    )

    set(_found_exe "")
    foreach(_cand IN LISTS _exe_candidates)
        if(EXISTS "${_cand}")
            set(_found_exe "${_cand}")
            break()
        endif()
    endforeach()

    # Fallback: glob any build/*/Release/<app>
    if(_found_exe STREQUAL "")
        file(GLOB _globbed
            "${REPO_ROOT}/build/*/Release/${CMAKE_PROJECT_NAME}"
            "${REPO_ROOT}/build/*/qml/QGroundControl"
        )
        list(LENGTH _globbed _n)
        if(_n GREATER 0)
            list(GET _globbed 0 _found_exe)
        endif()
    endif()

    if(_found_exe STREQUAL "")
        message(FATAL_ERROR
            "Expected executable not found in AppDir:\n"
            "  ${EXE_PATH}\n\n"
            "and no build output candidate found. Tried:\n"
            "  ${_exe_candidates}\n\n"
            "and globbed:\n"
            "  ${REPO_ROOT}/build/*/Release/${CMAKE_PROJECT_NAME}\n"
            "  ${REPO_ROOT}/build/*/qml/QGroundControl\n"
        )
    endif()

    message(STATUS "QGC: Copying executable from: ${_found_exe}")
    file(COPY_FILE "${_found_exe}" "${EXE_PATH}")
    file(CHMOD "${EXE_PATH}"
         FILE_PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endif()

message(STATUS "EXE_PATH=${EXE_PATH}")

# ---------------------------------------------------------------------------
# Icon
set(ICON_SRC "${REPO_ROOT}/deploy/linux/AMarinerControl_256.png")
set(ICON_DST "${APPDIR_PATH}/usr/share/icons/hicolor/256x256/apps/${CMAKE_PROJECT_NAME}.png")
if(NOT EXISTS "${ICON_SRC}")
    message(FATAL_ERROR "Icon source not found: ${ICON_SRC}")
endif()
file(MAKE_DIRECTORY "${APPDIR_PATH}/usr/share/icons/hicolor/256x256/apps")
file(COPY_FILE "${ICON_SRC}" "${ICON_DST}")
message(STATUS "ICON_DST=${ICON_DST}")

# ---------------------------------------------------------------------------
# Desktop file
set(DESKTOP_DST "${APPDIR_PATH}/usr/share/applications/${CMAKE_PROJECT_NAME}.desktop")
file(MAKE_DIRECTORY "${APPDIR_PATH}/usr/share/applications")
file(WRITE "${DESKTOP_DST}"
"[Desktop Entry]\n"
"Type=Application\n"
"Name=${CMAKE_PROJECT_NAME}\n"
"Exec=${CMAKE_PROJECT_NAME}\n"
"Icon=${CMAKE_PROJECT_NAME}\n"
"Terminal=false\n"
"Categories=Utility;\n")
message(STATUS "DESKTOP_DST=${DESKTOP_DST}")

# ---------------------------------------------------------------------------
# linuxdeploy
execute_process(
    COMMAND "${LINUXDEPLOY_PATH}"
            --appdir "${APPDIR_PATH}"
            --executable "${EXE_PATH}"
            --desktop-file "${DESKTOP_DST}"
            --icon-file "${ICON_DST}"
    COMMAND_ECHO STDOUT
    COMMAND_ERROR_IS_FATAL ANY
)

# ---------------------------------------------------------------------------
# appimagetool
set(ENV{ARCH} "${CMAKE_SYSTEM_PROCESSOR}")
set(ENV{VERSION} "${CMAKE_PROJECT_VERSION}")

execute_process(
    COMMAND "${APPIMAGETOOL_PATH}" "${APPDIR_PATH}" "${APPIMAGE_PATH}"
    COMMAND_ECHO STDOUT
    COMMAND_ERROR_IS_FATAL ANY
)

# ---------------------------------------------------------------------------
# Lint (optional)
if(DEFINED APPIMAGELINT_PATH AND EXISTS "${APPIMAGELINT_PATH}")
    execute_process(
        COMMAND "${APPIMAGELINT_PATH}" "${APPIMAGE_PATH}"
        RESULT_VARIABLE LINT_RESULT
        COMMAND_ECHO STDOUT
    )
    if(NOT LINT_RESULT EQUAL 0)
        message(WARNING "QGC: appimagelint reported problems; see log above")
    endif()
endif()

