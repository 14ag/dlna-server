#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include "access_keys.h"
#include "hover_focus_state.h"
#include "network_sources.h"
#include "playlist_scan_concurrency.h"
#include "server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
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
        else if (arg == "--source" && i + 1 < argc) AppConfig.mediaSources.push_back({Utf8ToWide(argv[++i])});
        else if (arg == "--print-scan-concurrency" && i + 1 < argc) {
            size_t n = static_cast<size_t>(std::atoll(argv[++i]));
            std::cout << ComputePlaylistScanConcurrency(n) << std::endl;
            return 0;
        }
        else if (arg == "--print-mnemonics" && i + 1 < argc) {
            std::string argVal = argv[++i];
            std::vector<std::wstring> labels;
            size_t start = 0;
            for (size_t j = 0; j <= argVal.size(); ++j) {
                if (j == argVal.size() || argVal[j] == ',') {
                    labels.push_back(Utf8ToWide(argVal.substr(start, j - start)));
                    start = j + 1;
                }
            }
            std::vector<wchar_t> result = AssignMnemonics(labels);
            for (size_t j = 0; j < result.size(); ++j) {
                if (j > 0) std::cout << ",";
                if (result[j] != L'\0') {
                    std::cout << static_cast<char>(result[j]);
                }
            }
            std::cout << std::endl;
            return 0;
        }
        else if (arg == "--print-cue-state" && i + 1 < argc) {
            std::string seq = argv[++i];
            KeyboardCueState cs;
            for (char ch : seq) {
                if (ch == 'k' || ch == 'K') cs.OnKeyboardInput();
                else if (ch == 'm' || ch == 'M') cs.OnMouseButtonInput();
                std::cout << (cs.HideAccel() ? "1" : "0") << "," << (cs.HideFocus() ? "1" : "0") << std::endl;
            }
            return 0;
        }
        else if (arg == "--print-hover-focus-state" && i + 1 < argc) {
            std::string seq = argv[++i];
            HoverFocusState state;
            size_t start = 0;
            while (start <= seq.size()) {
                size_t comma = seq.find(',', start);
                std::string token = seq.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!token.empty()) {
                    char code = token[0];
                    int id = std::atoi(token.c_str() + 1);
                    if (code == 'e') state.OnMouseEnter(id);
                    else if (code == 'l') state.OnMouseLeave(id);
                    else if (code == 'f') state.OnFocusGained(id);
                    else if (code == 'b') state.OnFocusLost(id);
                    std::cout << state.HighlightedControlId() << std::endl;
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            return 0;
        }
        else if (arg == "--print-is-recognized-playlist" && i + 2 < argc) {
            std::wstring path = Utf8ToWide(argv[++i]);
            std::wstring textFilePath = Utf8ToWide(argv[++i]);
            std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
            std::ostringstream ss;
            ss << file.rdbuf();
            std::cout << (IsRecognizedPlaylistText(path, ss.str()) ? "1" : "0") << std::endl;
            return 0;
        }
        else if (arg == "--debug") AppConfig.debugLog = true;
        else if (arg == "--help") { PrintUsage(argv[0]); return 0; }
        else AppConfig.mediaSources.push_back({Utf8ToWide(arg)});
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
