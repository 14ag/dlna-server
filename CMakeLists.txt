cmake_minimum_required(VERSION 3.20)
project(WinDLNAServer VERSION 1.0.0)

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
        src/media_sources.cpp
        src/config.cpp
        src/ipwhitelist.cpp
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
        src/media_sources.h
        src/config.h
        src/ipwhitelist.h
        src/log.h
        resources/resource.h
    )

    add_executable(WinDLNAServer WIN32 ${SOURCES} ${HEADERS} resources/app.rc)
    target_link_libraries(WinDLNAServer
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
    set_property(TARGET WinDLNAServer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(WinDLNAServer PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
    target_include_directories(WinDLNAServer PRIVATE src)

    install(TARGETS WinDLNAServer RUNTIME DESTINATION .)
else()
    include(GNUInstallDirs)

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
        src/contentdirectory.cpp
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
    install(PROGRAMS src/posix_gui.py DESTINATION ${CMAKE_INSTALL_DATADIR}/dlna-server)

    if(APPLE)
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
                    "${CMAKE_SOURCE_DIR}/src/posix_gui.py"
                    "${DLNA_APP_BUNDLE}/Contents/Resources/posix_gui.py"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_BINARY_DIR}/Info.plist"
                    "${DLNA_APP_BUNDLE}/Contents/Info.plist"
            COMMAND chmod +x
                    "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server"
                    "${DLNA_APP_BUNDLE}/Contents/MacOS/dlna-server-gui"
            DEPENDS dlna-server
            COMMENT "Assembling DLNA Server.app"
        )
        install(DIRECTORY "${DLNA_APP_BUNDLE}" DESTINATION . USE_SOURCE_PERMISSIONS)
    else()
        configure_file(packaging/linux/install_desktop.cmake.in "${CMAKE_BINARY_DIR}/install_linux_desktop.cmake" @ONLY)
        install(PROGRAMS packaging/linux/dlna-server-gui DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES resources/dlna-server.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
        install(SCRIPT "${CMAKE_BINARY_DIR}/install_linux_desktop.cmake")
    endif()
endif()
