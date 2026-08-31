cmake_minimum_required(VERSION 3.20)
project(dlna-server VERSION 1.7.0)

configure_file(${CMAKE_SOURCE_DIR}/src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(dlna_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

find_package(ZLIB)

# Some CMake configurations provide ZLIB via imported targets instead of
# setting ZLIB_FOUND. Treat either as success.
if(NOT (ZLIB_FOUND OR TARGET ZLIB::ZLIB))
    # Try to locate zlib in common vcpkg locations (helpful on Windows)
    if(DEFINED ENV{VCPKG_ROOT})
        set(_vcpkg_root $ENV{VCPKG_ROOT})
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/../vcpkg")
        set(_vcpkg_root "${CMAKE_SOURCE_DIR}/../vcpkg")
    elseif(EXISTS "C:/vcpkg")
        set(_vcpkg_root "C:/vcpkg")
    endif()

    if(DEFINED _vcpkg_root)
        file(GLOB _zlibs "${_vcpkg_root}/installed/*/lib/zlib.lib" "${_vcpkg_root}/installed/*/lib/zlibstatic.lib")
        if(_zlibs)
            list(GET _zlibs 0 _zlib_lib)
            get_filename_component(_triplet_dir "${_zlib_lib}" DIRECTORY)
            get_filename_component(_triplet_name "${_triplet_dir}" NAME)
            set(ZLIB_LIBRARY "${_zlib_lib}")
            set(ZLIB_INCLUDE_DIR "${_vcpkg_root}/installed/${_triplet_name}/include")
            find_package(ZLIB REQUIRED)
        endif()
    endif()

    if(NOT ZLIB_FOUND)
        message(FATAL_ERROR "Could NOT find ZLIB (missing: ZLIB_LIBRARY ZLIB_INCLUDE_DIR)\n\n"
            "Common fixes:\n"
            "  - If you use vcpkg (recommended), install zlib for your triplet and re-run CMake:\n"
            "      from vcpkg root: .\\vcpkg install zlib:x64-windows\n"
            "      then configure CMake with the vcpkg toolchain file:\n"
            "      cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake\n\n"
            "  - Or point CMake directly to an installed zlib:\n"
            "      -DZLIB_INCLUDE_DIR=\"C:/path/to/zlib/include\" -DZLIB_LIBRARY=\"C:/path/to/zlib/lib/zlib.lib\"\n")
    endif()
endif()
if(TARGET ZLIB::ZLIBSTATIC AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS ZLIB::ZLIBSTATIC)
endif()
find_package(CURL REQUIRED)

function(dlna_link_required_curl target_name)
    target_link_libraries(${target_name} PRIVATE CURL::libcurl)
    target_compile_definitions(${target_name} PRIVATE DLNA_HAS_LIBCURL=1)
endfunction()

if(WIN32)
set(SOURCES
         src/access_keys.cpp
         src/tray_notify.cpp
         src/access_key_hook.cpp
         src/mainwindow.cpp
         src/settingsdlg.cpp
         src/logdlg.cpp
         src/help_dialog.cpp
         src/ui_font.cpp
         src/dark_frame.cpp
         src/server.cpp
         src/netutils.cpp
         src/httpserver.cpp
         src/http_common.cpp
         src/contentdirectory.cpp
         src/upnp_eventing.cpp
         src/ssdp.cpp
         src/ssdp_common.cpp
         src/dlna_utils.cpp
         src/network_sources.cpp
         src/source_watcher.cpp
         src/media_database.cpp
         src/media_scan_common.cpp
         src/bounded_thread_pool.cpp
         src/playlist_scan_concurrency.cpp
         src/media_sources_common.cpp
         src/scan_cancellation.cpp
         src/fs_backend_win32.cpp
         src/settings_restart.cpp
         src/modal_focus.cpp
         src/config.cpp
         src/ipwhitelist.cpp
          src/firewall_access_win.cpp
          src/source_drop_target.cpp
           src/log.cpp
           src/netchange_watch_win.cpp
           src/dirwatch_win.cpp
       )

set(HEADERS
         src/access_keys.h
         src/tray_notify.h
         src/cli_flags.h
         src/settings_help.h
         src/mainwindow.h
         src/settingsdlg.h
         src/logdlg.h
         src/help_dialog.h
         src/server.h
         src/netutils.h
         src/netutils_internal.h
         src/network_interface_policy.h
         src/httpserver.h
         src/transmitfile_chunking.h
         src/http_common.h
         src/contentdirectory.h
         src/search_cache_budget.h
         src/upnp_eventing.h
         src/ssdp.h
         src/ssdp_common.h
         src/dlna_utils.h
         src/network_sources.h
         src/source_watcher.h
         src/media_database.h
         src/media_scan_common.h
         src/task_group.h
         src/bounded_thread_pool.h
         src/playlist_scan_concurrency.h
         src/fs_backend.h
         src/settings_restart.h
         src/source_list_focus.h
         src/hover_focus_state.h
          src/scan_cancellation.h
          src/input_gate.h
          src/modal_focus.h
          src/thread_guard.h
          src/health_check_policy.h
          src/geometry_dump_policy.h
         src/config.h
         src/ipwhitelist.h
          src/firewall_access.h
          src/source_drop_target.h
          src/source_drop_policy.h
          src/media_source_file_types.h
            src/startup_mode.h
             src/server_close_policy.h
             src/close_pending_state.h
             src/function_key_action.h
            src/ui_font.h
            src/dark_frame.h
             src/log.h
             src/network_change_debounce.h
             src/netchange_watch.h
             src/fs_change_debounce.h
             src/dirwatch.h
           resources/resource.h
     )

    list(REMOVE_ITEM SOURCES src/main.cpp)
    add_library(dlna_core STATIC ${SOURCES} ${HEADERS})
    set_property(TARGET dlna_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    dlna_enable_warnings(dlna_core)
    target_link_libraries(dlna_core PUBLIC CURL::libcurl)
    target_compile_definitions(dlna_core PRIVATE DLNA_HAS_LIBCURL=1)
target_link_libraries(dlna_core PUBLIC
         ws2_32 shell32 ole32 oleaut32 uuid comctl32 shlwapi dwmapi rpcrt4 iphlpapi)
    target_compile_definitions(dlna_core PUBLIC UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
    target_include_directories(dlna_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)

    add_executable(dlna-server WIN32 src/main.cpp resources/app.rc)
    target_link_libraries(dlna-server PRIVATE dlna_core)
    set_target_properties(dlna-server PROPERTIES OUTPUT_NAME "DLNA Server")
    set_property(TARGET dlna-server PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    install(TARGETS dlna-server RUNTIME DESTINATION .)
else()
    include(GNUInstallDirs)
    include(FetchContent)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    option(DLNA_ENABLE_GTK4_GUI "Build the native GTK4 Linux GUI" ON)
    option(DLNA_FLATPAK_BUILD "Use the flatpak specific GApplication id" OFF)

add_library(dlna_core STATIC
         src/access_keys.cpp
         src/tray_notify.cpp
         src/posix_server.cpp
         src/posix_ssdp.cpp
         src/ssdp_common.cpp
         src/posix_httpserver.cpp
         src/http_common.cpp
         src/media_sources_common.cpp
         src/scan_cancellation.cpp
         src/fs_backend_posix.cpp
         src/posix_netutils.cpp
         src/posix_config.cpp
         src/posix_log.cpp
         src/ipwhitelist.cpp
         src/contentdirectory.cpp
         src/upnp_eventing.cpp
         src/dlna_utils.cpp
         src/network_sources.cpp
         src/source_watcher.cpp
         src/media_database.cpp
         src/media_scan_common.cpp
         src/bounded_thread_pool.cpp
         src/playlist_scan_concurrency.cpp
         src/settings_restart.cpp
         src/posix_single_instance.cpp
         src/posix_daemonize.cpp
         src/netchange_watch_posix.cpp
         src/dirwatch_posix.cpp
     )
    dlna_enable_warnings(dlna_core)
    dlna_link_required_curl(dlna_core)
    target_include_directories(dlna_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
    target_compile_definitions(dlna_core PUBLIC DLNA_POSIX=1 DLNA_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")
    if(APPLE)
        target_compile_definitions(dlna_core PUBLIC DLNA_PLATFORM_NAME=\"macOS\")
    else()
        target_compile_definitions(dlna_core PUBLIC DLNA_PLATFORM_NAME=\"Linux\")
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(dlna_core PUBLIC Threads::Threads)

    add_executable(dlna-server src/posix_main.cpp)
    target_link_libraries(dlna-server PRIVATE dlna_core)

    install(TARGETS dlna-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(DLNA_ENABLE_GTK4_GUI)
        configure_file(packaging/linux/dlna-server-gui "${CMAKE_BINARY_DIR}/dlna-server-gui" @ONLY NEWLINE_STYLE UNIX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
        pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)

        add_custom_command(
            OUTPUT "${CMAKE_SOURCE_DIR}/resources/gtk/style.css"
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/gen_gtk_style.py"
            DEPENDS "${CMAKE_SOURCE_DIR}/src/ui_tokens.h" "${CMAKE_SOURCE_DIR}/tools/gen_gtk_style.py"
            COMMENT "Generating GTK4 stylesheet from ui_tokens.h"
        )
        add_custom_target(dlna_gtk_style ALL DEPENDS "${CMAKE_SOURCE_DIR}/resources/gtk/style.css")

        add_executable(dlna-server-gui-gtk4 src/gtk4_gui_main.cpp src/posix_tray.cpp)
        add_dependencies(dlna-server-gui-gtk4 dlna_gtk_style)
        set_target_properties(dlna-server-gui-gtk4 PROPERTIES OUTPUT_NAME dlna-server-gui-bin)
        dlna_enable_warnings(dlna-server-gui-gtk4)
        target_compile_definitions(dlna-server-gui-gtk4 PRIVATE DLNA_POSIX_GUI=1)
        if(DLNA_FLATPAK_BUILD)
            target_compile_definitions(dlna-server-gui-gtk4 PRIVATE DLNA_FLATPAK_BUILD=1)
        endif()
        target_link_libraries(dlna-server-gui-gtk4 PRIVATE dlna_core PkgConfig::GTK4 PkgConfig::X11 Threads::Threads)
        install(TARGETS dlna-server-gui-gtk4 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(PROGRAMS "${CMAKE_BINARY_DIR}/dlna-server-gui" DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES "${CMAKE_SOURCE_DIR}/resources/gtk/style.css" DESTINATION ${CMAKE_INSTALL_DATADIR}/dlna-server/gtk)
        install(FILES "${CMAKE_SOURCE_DIR}/resources/gtk/figma.css" DESTINATION ${CMAKE_INSTALL_DATADIR}/dlna-server/gtk)
    endif()

    if(APPLE)
        if(DLNA_ENABLE_GTK4_GUI)
            set(DLNA_APP_BUNDLE "${CMAKE_BINARY_DIR}/DLNA Server.app")
            configure_file(packaging/macos/Info.plist.in "${CMAKE_BINARY_DIR}/Info.plist" @ONLY)
            add_custom_target(DLNAServerApp ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${DLNA_APP_BUNDLE}/Contents/MacOS"
                        "${DLNA_APP_BUNDLE}/Contents/Resources"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:dlna-server>"
                        "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/packaging/macos/dlna-server-gui"
                        "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server-gui"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:dlna-server-gui-gtk4>"
                        "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server-gui-bin"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/resources/app.ico"
                        "${DLNA_APP_BUNDLE}/Contents/Resources/app.ico"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/resources/server_icon_48.png"
                        "${DLNA_APP_BUNDLE}/Contents/Resources/server_icon_48.png"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/resources/server_icon_120.png"
                        "${DLNA_APP_BUNDLE}/Contents/Resources/server_icon_120.png"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/resources/server_icon_256.png"
                        "${DLNA_APP_BUNDLE}/Contents/Resources/server_icon_256.png"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_BINARY_DIR}/Info.plist"
                        "${DLNA_APP_BUNDLE}/Contents/Info.plist"
                COMMAND chmod +x
                        "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server"
                        "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server-gui"
                        "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server-gui-bin"
                DEPENDS dlna-server dlna-server-gui-gtk4
                COMMENT "Assembling DLNA Server.app"
            )
            install(DIRECTORY "${DLNA_APP_BUNDLE}" DESTINATION . USE_SOURCE_PERMISSIONS)
        endif()
    else()
        if(DLNA_ENABLE_GTK4_GUI)
            configure_file(packaging/linux/install_desktop.cmake.in "${CMAKE_BINARY_DIR}/install_linux_desktop.cmake" @ONLY)
            configure_file(packaging/linux/dlna-server.appdata.xml.in "${CMAKE_BINARY_DIR}/dlna-server.appdata.xml" @ONLY)
            # Install SVG into hicolor scalable tree for desktop-env icon lookup
            install(FILES resources/dlna-server.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
            # Install PNG icons into hicolor fixed-size directories so DEs
            # find the icon by name (Icon=dlna-server in .desktop files).
            install(FILES resources/server_icon_48.png   DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps   RENAME dlna-server.png)
            install(FILES resources/server_icon_120.png  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps RENAME dlna-server.png)
            install(FILES resources/server_icon_256.png  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME dlna-server.png)
            # Also keep a copy in the app-specific data dir for runtime loading
            install(FILES
                resources/server_icon_48.png
                resources/server_icon_120.png
                resources/server_icon_256.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/dlna-server/icons)
            install(FILES "${CMAKE_BINARY_DIR}/dlna-server.appdata.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
            install(SCRIPT "${CMAKE_BINARY_DIR}/install_linux_desktop.cmake")
        endif()
    endif()
endif()

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "dlna-server")
    set(CPACK_PACKAGE_VENDOR "dlna-server contributors")
    set(CPACK_PACKAGE_CONTACT "https://github.com/14ag/dlna-server/issues")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DLNA media server")
    set(CPACK_PACKAGE_DESCRIPTION "DLNA Server shares local video, audio, and image files with DLNA and UPnP clients on the local network.")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "dlna-server contributors")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/14ag/dlna-server")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    include(CPack)
endif()
