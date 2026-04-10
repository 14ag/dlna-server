cmake_minimum_required(VERSION 3.20)
project(WinDLNAServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure source files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/settingsdlg.cpp
    src/logdlg.cpp
    src/server.cpp
    src/ssdp.cpp
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
    src/httpserver.h
    src/contentdirectory.h
    src/media_sources.h
    src/config.h
    src/ipwhitelist.h
    src/log.h
    resources/resource.h
)

set(RESOURCES
    resources/app.rc
)

# WIN32 ensures it's built as a GUI subsystem application (no console window)
add_executable(WinDLNAServer WIN32 ${SOURCES} ${HEADERS} ${RESOURCES})

# Required Win32 libraries per spec
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

# Use static CRT linkage /MT
set_property(TARGET WinDLNAServer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Enable Unicode mapping for C runtime and Win32
target_compile_definitions(WinDLNAServer PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)

target_include_directories(WinDLNAServer PRIVATE src)
