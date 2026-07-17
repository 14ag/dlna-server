#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include "access_keys.h"
#include "hover_focus_state.h"
#include "input_gate.h"
#include "network_sources.h"
#include "playlist_scan_concurrency.h"
#include "scan_cancellation.h"
#include "server.h"
#include "settings_restart.h"
#include "startup_mode.h"

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
    std::cerr << "Usage: " << exe << " [--port 8200] [--name NAME] [--uuid UUID] [--debug] --source \"pathA\",\"pathB\"\n";
    std::cerr << "Sources can be folders, playlist files (.m3u, .m3u8, .pls), smb:// URLs, or ftp:// URLs.\n";
}
}

int main(int argc, char** argv) {
    AppConfig.Load();
    std::vector<std::wstring> runtimeSources;
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
        else if (arg == "--source" && i + 1 < argc) {
            ++i;
            std::vector<std::wstring> parsedSources = ParseQuotedCommaList(Utf8ToWide(argv[i]));
            std::vector<MediaSource> immediateOverride;
            if (parsedSources.empty()) {
                runtimeSources.push_back(Utf8ToWide(argv[i]));
                immediateOverride.push_back({Utf8ToWide(argv[i])});
            } else {
                for (auto& parsed : parsedSources) {
                    runtimeSources.push_back(parsed);
                    immediateOverride.push_back({parsed});
                }
            }
            AppConfig.SetRuntimeSourceOverride(immediateOverride);
        }
        else if (arg == "--kill-server" || arg == "-k") {
            std::cerr << "kill-server is not supported on this platform" << std::endl;
            return 1;
        }
        else if (arg == "--print-scan-cancellation-lifecycle") {
            AppScanCancel.BeginScan();
            std::cout << (AppScanCancel.IsCancelled() ? "1" : "0") << std::endl;
            AppScanCancel.RequestCancel();
            std::cout << (AppScanCancel.IsCancelled() ? "1" : "0") << std::endl;
            AppScanCancel.BeginScan();
            std::cout << (AppScanCancel.IsCancelled() ? "1" : "0") << std::endl;
            return 0;
        }
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
        else if (arg == "--print-any-field-has-content" && i + 1 < argc) {
            std::string csv = argv[++i];
            std::vector<int> lens;
            size_t start = 0;
            while (start <= csv.size()) {
                size_t comma = csv.find(',', start);
                std::string token = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!token.empty()) lens.push_back(std::atoi(token.c_str()));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            std::cout << (AnyFieldHasContent(lens) ? "1" : "0") << std::endl;
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
        else if (arg == "--print-parse-quoted-comma-list" && i + 1 < argc) {
            for (const auto& field : ParseQuotedCommaList(Utf8ToWide(argv[++i]))) {
                std::wcout << field << std::endl;
            }
            return 0;
        }
        else if (arg == "--print-decode-legacy-pipe-sources" && i + 1 < argc) {
            for (const auto& field : DecodeLegacyPipeDelimitedSources(Utf8ToWide(argv[++i]))) {
                std::wcout << field << std::endl;
            }
            return 0;
        }
        else if (arg == "--print-resolve-relative-url" && i + 2 < argc) {
            std::wstring baseUrl = Utf8ToWide(argv[++i]);
            std::wstring relativeUrl = Utf8ToWide(argv[++i]);
            std::wcout << ResolveRelativeUrl(baseUrl, relativeUrl) << std::endl;
            return 0;
        }
        else if (arg == "--print-rewrite-hls-manifest" && i + 2 < argc) {
            std::wstring baseUrl = Utf8ToWide(argv[++i]);
            std::wstring textFilePath = Utf8ToWide(argv[++i]);
            std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
            std::ostringstream ss;
            ss << file.rdbuf();
            std::cout << RewriteHlsManifestUrisToAbsolute(baseUrl, ss.str()) << std::endl;
            return 0;
        }
        else if (arg == "--print-should-start-headless" && i + 2 < argc) {
            bool explicitFlag = std::string(argv[++i]) == "1";
            bool hasSources = std::string(argv[++i]) == "1";
            std::wcout << (ShouldStartHeadless(explicitFlag, hasSources) ? L"1" : L"0") << std::endl;
            return 0;
        }
        else if (arg == "--print-debug-log-requires-restart" && i + 2 < argc) {
            ConfigSnapshot before{};
            ConfigSnapshot after{};
            before.debugLog = std::string(argv[++i]) == "1";
            after.debugLog = std::string(argv[++i]) == "1";
            std::vector<std::wstring> changed = DetermineSettingsRequiringRestart(before, after);
            bool found = false;
            for (const auto& name : changed) {
                if (name == L"Debug Log") found = true;
            }
            std::wcout << (found ? L"1" : L"0") << std::endl;
            return 0;
        }
        else if (arg == "--print-media-browsing-restart-required" && i + 2 < argc) {
            // Fixed field order: AddArtistAlbum,DoNotShowAllMedia,SortByTitle,
            // FlatFolders,ShowFileNames,ProxyStreams,BackgroundScan.
            // Each argument is a 7-character string of '0'/'1' in that order.
            auto parseFlags = [](const std::string& bits, ConfigSnapshot& snap) {
                snap.addArtistAlbumFolders        = bits.size() > 0 && bits[0] == '1';
                snap.doNotShowAllMediaFolders     = bits.size() > 1 && bits[1] == '1';
                snap.sortByTitle                  = bits.size() > 2 && bits[2] == '1';
                snap.flatFolderStyle              = bits.size() > 3 && bits[3] == '1';
                snap.showFileNamesInsteadOfTitles = bits.size() > 4 && bits[4] == '1';
                snap.proxyStreams                 = bits.size() > 5 && bits[5] == '1';
                snap.backgroundScanEnabled        = bits.size() > 6 && bits[6] == '1';
            };
            ConfigSnapshot before{};
            ConfigSnapshot after{};
            parseFlags(argv[++i], before);
            parseFlags(argv[++i], after);
            std::vector<std::wstring> changed = DetermineSettingsRequiringRestart(before, after);
            std::wcout << (changed.empty() ? L"0" : L"1") << std::endl;
            return 0;
        }
        else if (arg == "--print-is-supported-source-path" && i + 1 < argc) {
            std::wcout << (IsSupportedLocalMediaOrPlaylistPath(Utf8ToWide(argv[++i])) ? L"1" : L"0") << std::endl;
            return 0;
        }
        else if (arg == "--print-media-sources") {
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.mediaSources) {
                std::wcout << src.path << std::endl;
            }
            return 0;
        }
        else if (arg == "--print-effective-media-sources") {
            // Reflects effectiveMediaSources, i.e. what Scan() will actually
            // publish for this process's current state. Differs from
            // --print-media-sources whenever --source appeared earlier in
            // argv than this flag (argument order matters: --source must be
            // parsed first to install the override before this flag runs).
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.effectiveMediaSources) {
                std::wcout << src.path << std::endl;
            }
            return 0;
        }
        else if (arg == "--print-clear-override-then-effective") {
            AppConfig.ClearRuntimeSourceOverride();
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.effectiveMediaSources) {
                std::wcout << src.path << std::endl;
            }
            return 0;
        }
        else if (arg == "--print-source-override-lifecycle" && i + 1 < argc) {
            std::vector<std::wstring> parsedSources = ParseQuotedCommaList(Utf8ToWide(argv[++i]));
            std::vector<MediaSource> overrideSources;
            for (auto& parsed : parsedSources) {
                if (!parsed.empty()) overrideSources.push_back({parsed});
            }
            AppConfig.SetRuntimeSourceOverride(overrideSources);

            std::wstring reason;
            if (!DLNAServer.Start(reason)) {
                std::wcerr << L"start1 failed: " << reason << std::endl;
                return 1;
            }
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::wcout << L"--override-active--" << std::endl;
            for (auto& s : AppConfig.Snapshot().effectiveMediaSources) std::wcout << s.path << std::endl;

            DLNAServer.Stop();
            std::wcout << L"--after-stop--" << std::endl;
            for (auto& s : AppConfig.Snapshot().effectiveMediaSources) std::wcout << s.path << std::endl;
            return 0;
        }
        else if (arg == "--debug") AppConfig.debugLog = true;
        else if (arg == "--help") { PrintUsage(argv[0]); return 0; }
        else runtimeSources.push_back(Utf8ToWide(arg));
    }
    if (!runtimeSources.empty()) {
        std::vector<MediaSource> overrideSources;
        for (const auto& src : runtimeSources) {
            overrideSources.push_back({src});
        }
        AppConfig.SetRuntimeSourceOverride(overrideSources);
    }
    if (AppConfig.mediaSources.empty() && !AppConfig.defaultPlaylistEnabled && runtimeSources.empty()) {
        PrintUsage(argv[0]);
        return 2;
    }
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    std::wstring outReason;
    if (!DLNAServer.Start(outReason)) {
        std::wcerr << L"Failed to start server: " << outReason << std::endl;
        return 1;
    }
    while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    DLNAServer.Stop();
    return 0;
}
