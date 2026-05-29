#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include "server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> g_stop(false);

void HandleSignal(int) {
    g_stop = true;
}

void PrintUsage(const char* exe) {
    std::cerr << "Usage: " << exe << " [--port 8200] [--name NAME] [--uuid UUID] [--debug] --source PATH_OR_URL [--source PATH_OR_URL...]\n";
    std::cerr << "Sources can be folders, playlist files (.m3u, .m3u8, .pls), smb:// URLs, or ftp:// URLs.\n";
}
}

int main(int argc, char** argv) {
    AppConfig.Load();
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            int port = 0;
            if (!TryParsePortStrict(argv[++i], port)) {
                PrintUsage(argv[0]);
                return 2;
            }
            AppConfig.port = port;
        }
        else if (arg == "--name" && i + 1 < argc) AppConfig.serverName = Utf8ToWide(argv[++i]);
        else if (arg == "--uuid" && i + 1 < argc) AppConfig.deviceUUID = Utf8ToWide(argv[++i]);
        else if (arg == "--source" && i + 1 < argc) AppConfig.mediaSources.push_back({Utf8ToWide(argv[++i]), true});
        else if (arg == "--debug") AppConfig.debugLog = true;
        else if (arg == "--help") { PrintUsage(argv[0]); return 0; }
        else AppConfig.mediaSources.push_back({Utf8ToWide(arg), true});
    }
    if (AppConfig.mediaSources.empty() && !AppConfig.defaultPlaylistEnabled) {
        PrintUsage(argv[0]);
        return 2;
    }
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    if (!DLNAServer.Start()) return 1;
    while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    DLNAServer.Stop();
    return 0;
}
