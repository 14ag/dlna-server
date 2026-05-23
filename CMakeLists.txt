cmake_minimum_required(VERSION 3.20)
project(dlna-server VERSION 1.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    set(SOURCES
        src/main.cpp
        src/mainwindow.cpp
        src/settingsdlg.cpp
        src/logdlg.cpp
        src/server.cpp
        src/ssdp.cpp
        src/netutils.cpp
        src/httpserver.cpp
        src/contentdirectory.cpp
        src/dlna_utils.cpp
        src/media_sources.cpp
        src/config.cpp
        src/ipwhitelist.cpp
        src/firewall_access_win.cpp
        src/log.cpp
    )

    set(HEADERS
        src/mainwindow.h
        src/settingsdlg.h
        src/logdlg.h
        src/server.h
        src/ssdp.h
        src/netutils.h
        src/httpserver.h
        src/contentdirectory.h
        src/dlna_utils.h
        src/media_sources.h
        src/config.h
        src/ipwhitelist.h
        src/firewall_access.h
        src/log.h
        resources/resource.h
    )

    add_executable(dlna-server WIN32 ${SOURCES} ${HEADERS} resources/app.rc)
    target_link_libraries(dlna-server
        ws2_32
        shell32
        ole32
        oleaut32
        uuid
        comctl32
        shlwapi
        rpcrt4
        iphlpapi
    )
    set_property(TARGET dlna-server PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(dlna-server PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
    target_include_directories(dlna-server PRIVATE src)

    install(TARGETS dlna-server RUNTIME DESTINATION .)
else()
    include(GNUInstallDirs)
    include(FetchContent)
    option(DLNA_ENABLE_FLTK_GUI "Build the native FLTK Linux GUI" OFF)
    set(DLNA_FLTK_VERSION "release-1.4.5" CACHE STRING "FLTK git tag used when system FLTK is unavailable")

    add_executable(dlna-server
        src/posix_main.cpp
        src/posix_server.cpp
        src/posix_ssdp.cpp
        src/posix_httpserver.cpp
        src/posix_netutils.cpp
        src/posix_media_sources.cpp
        src/posix_config.cpp
        src/posix_log.cpp
        src/ipwhitelist.cpp
        src/firewall_access_posix.cpp
        src/contentdirectory.cpp
        src/dlna_utils.cpp
    )
    target_include_directories(dlna-server PRIVATE src)
    target_compile_definitions(dlna-server PRIVATE DLNA_POSIX=1)
    if(APPLE)
        target_compile_definitions(dlna-server PRIVATE DLNA_PLATFORM_NAME=\"macOS\")
    else()
        target_compile_definitions(dlna-server PRIVATE DLNA_PLATFORM_NAME=\"Linux\")
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(dlna-server PRIVATE Threads::Threads)

    install(TARGETS dlna-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(DLNA_ENABLE_FLTK_GUI)
        find_package(FLTK QUIET)
        if(NOT FLTK_FOUND)
            FetchContent_Declare(
                fltk
                GIT_REPOSITORY https://github.com/fltk/fltk.git
                GIT_TAG ${DLNA_FLTK_VERSION}
                GIT_SHALLOW TRUE
            )
            set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
            set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
            set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
            FetchContent_GetProperties(fltk)
            if(NOT fltk_POPULATED)
                FetchContent_Populate(fltk)
                add_subdirectory(${fltk_SOURCE_DIR} ${fltk_BINARY_DIR} EXCLUDE_FROM_ALL)
            endif()
            set(DLNA_FLTK_TARGET fltk)
        else()
            set(DLNA_FLTK_TARGET ${FLTK_LIBRARIES})
        endif()
        add_executable(dlna-server-gui-native
            src/fltk_gui_main.cpp
            src/posix_server.cpp
            src/posix_ssdp.cpp
            src/posix_httpserver.cpp
            src/posix_netutils.cpp
            src/posix_media_sources.cpp
            src/posix_config.cpp
            src/posix_log.cpp
            src/ipwhitelist.cpp
            src/firewall_access_posix.cpp
            src/contentdirectory.cpp
            src/dlna_utils.cpp
        )
        set_target_properties(dlna-server-gui-native PROPERTIES OUTPUT_NAME dlna-server-gui-bin)
        target_compile_definitions(dlna-server-gui-native PRIVATE DLNA_POSIX=1 DLNA_PLATFORM_NAME=\"Linux\")
        target_include_directories(dlna-server-gui-native PRIVATE src)
        if(FLTK_FOUND)
            target_include_directories(dlna-server-gui-native PRIVATE ${FLTK_INCLUDE_DIR})
        endif()
        target_link_libraries(dlna-server-gui-native PRIVATE ${DLNA_FLTK_TARGET} Threads::Threads)
        install(TARGETS dlna-server-gui-native RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(PROGRAMS packaging/linux/dlna-server-gui DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        install(PROGRAMS packaging/linux/dlna-server-gui DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(PROGRAMS src/posix_gui.py DESTINATION ${CMAKE_INSTALL_DATADIR}/dlna-server)
    endif()

    if(APPLE)
        set(DLNA_APP_BUNDLE "${CMAKE_BINARY_DIR}/dlna-server.app")
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
                    "${CMAKE_SOURCE_DIR}/src/posix_gui.py"
                    "${DLNA_APP_BUNDLE}/Contents/Resources/posix_gui.py"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_BINARY_DIR}/Info.plist"
                    "${DLNA_APP_BUNDLE}/Contents/Info.plist"
            COMMAND chmod +x
                    "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server"
                    "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server-gui"
            DEPENDS dlna-server
            COMMENT "Assembling dlna-server.app"
        )
        install(DIRECTORY "${DLNA_APP_BUNDLE}" DESTINATION . USE_SOURCE_PERMISSIONS)
    else()
        configure_file(packaging/linux/install_desktop.cmake.in "${CMAKE_BINARY_DIR}/install_linux_desktop.cmake" @ONLY)
        configure_file(packaging/linux/dlna-server.appdata.xml.in "${CMAKE_BINARY_DIR}/dlna-server.appdata.xml" @ONLY)
        install(FILES resources/dlna-server.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
        install(FILES "${CMAKE_BINARY_DIR}/dlna-server.appdata.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
        install(SCRIPT "${CMAKE_BINARY_DIR}/install_linux_desktop.cmake")
    endif()
endif()
