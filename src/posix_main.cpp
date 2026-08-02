#include "config.h"
#include "dlna_utils.h"
#include "network_interface_policy.h"
#include "http_common.h"
#include "media_scan_common.h"
#include "log.h"
#include "netutils.h"
#include "access_keys.h"
#include "function_key_action.h"
#include "hover_focus_state.h"
#include "input_gate.h"
#include "network_sources.h"
#include "playlist_scan_concurrency.h"
#include "media_sources.h"
#include "media_source_file_types.h"
#include "thread_guard.h"
#include "scan_cancellation.h"
#include "server.h"
#include "upnp_eventing.h"
#include "close_pending_state.h"
#include "server_close_policy.h"
#include "tray_notify.h"
#include "settings_restart.h"
#include "startup_mode.h"
#include "posix_single_instance.h"
#include "copydata_validation.h"
#include "media_database.h"
#include "browse_page_cap.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
std::atomic<bool> g_stop(false);

void HandleSignal(int) {
    g_stop = true;
}

void OnSingleInstanceCommand(const std::string& cmd) {
    if (cmd == "kill") {
        g_stop = true;
    }
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
            if (!SingleInstance::SendKill()) {
                std::cerr << "No running dlna-server instance found." << std::endl;
                return 1;
            }
            return 0;
        }
        else if (arg == "--headless" || arg == "-h") {
            // No-op: the POSIX build has no GUI/console distinction, the
            // server always runs without a window. Accepted for symmetry
            // with the Win32 binary (main.cpp) so tests and wrappers that
            // pass --headless do not have it mis-parsed as a media source.
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
        else if (arg == "--print-single-instance-lifecycle") {
            const bool acquired = SingleInstance::TryAcquireLock();
            std::cout << (acquired ? "lock-acquired" : "lock-busy") << std::endl;
            if (acquired) {
                SingleInstance::StartListening([](const std::string&) {});
                const bool sent = SingleInstance::SendShow();
                std::cout << (sent ? "show-sent" : "show-failed") << std::endl;
                SingleInstance::ReleaseLock();
                std::cout << "released" << std::endl;
            }
            return 0;
        }
        else if (arg == "--print-log-since-lifecycle") {
            LogPrint(L"line-one");
            LogPrint(L"line-two");
            LogSnapshot first = GetSystemLogSince(0);
            std::wcout << L"first-latest=" << first.latestSequence << std::endl;
            LogPrint(L"line-three");
            LogSnapshot second = GetSystemLogSince(first.latestSequence);
            std::wcout << L"second-latest=" << second.latestSequence << std::endl;
            std::wcout << (second.text.find(L"line-three") != std::wstring::npos
                                ? L"has-new-line" : L"missing-new-line") << std::endl;
            std::wcout << (second.text.find(L"line-one") != std::wstring::npos
                                ? L"leaked-old-line" : L"no-old-line-leak") << std::endl;
            return 0;
        }
        else if (arg == "--print-config-load-lockstate") {
            std::cout << "before load" << std::endl;
            AppConfig.Load();
            std::cout << "after load ok" << std::endl;
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
        else if (arg == "--print-function-key-action" && i + 4 < argc) {
            int vkCode = std::atoi(argv[++i]);
            bool isRunning = std::string(argv[++i]) == "1";
            bool isBusy = std::string(argv[++i]) == "1";
            bool isScanning = std::string(argv[++i]) == "1";
            switch (DecideFunctionKeyAction(vkCode, isRunning, isBusy, isScanning)) {
            case FunctionKeyAction::ShowHelp: std::cout << "show-help" << std::endl; break;
            case FunctionKeyAction::Rescan: std::cout << "rescan" << std::endl; break;
            case FunctionKeyAction::RefreshSourceList: std::cout << "refresh-source-list" << std::endl; break;
            case FunctionKeyAction::ShowSourceListContextMenu: std::cout << "show-context-menu" << std::endl; break;
            case FunctionKeyAction::None: std::cout << "none" << std::endl; break;
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
        else if (arg == "--print-parse-quoted-comma-list" && i + 1 < argc) {
            for (const auto& field : ParseQuotedCommaList(Utf8ToWide(argv[++i]))) {
                std::wcout << field << std::endl;
            }
            return 0;
        }
        else if (arg == "--print-trim-wide" && i + 1 < argc) {
            std::wcout << TrimWide(Utf8ToWide(argv[++i])) << std::endl;
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
        else if (arg == "--print-media-resource-url-suffix" && i + 1 < argc) {
            std::cout << BuildMediaResourceUrlExtensionSuffix(Utf8ToWide(argv[++i])) << std::endl;
            return 0;
        }
        else if (arg == "--print-strip-resource-id-extension" && i + 1 < argc) {
            std::cout << StripResourceIdExtension(argv[++i]) << std::endl;
            return 0;
        }
        else if (arg == "--print-media-display-title" && i + 3 < argc) {
            bool showFileNames = std::string(argv[++i]) == "1";
            std::wstring titleOverride = Utf8ToWide(argv[++i]);
            std::wstring path = Utf8ToWide(argv[++i]);
            std::wcout << BuildDisplayTitleForMediaFile(showFileNames, titleOverride, path) << std::endl;
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
        else if (arg == "--print-should-close-now" && i + 2 < argc) {
            bool isRunning = std::string(argv[++i]) == "1";
            bool isBusy = std::string(argv[++i]) == "1";
            std::wcout << (ShouldCloseNow(isRunning, isBusy) ? L"1" : L"0") << std::endl;
            return 0;
        }
        else if (arg == "--print-is-plausible-copydata-size" && i + 1 < argc) {
            unsigned long cbData = std::strtoul(argv[++i], nullptr, 10);
            std::wcout << (IsPlausibleWideStringCopyDataSize(cbData) ? L"1" : L"0") << std::endl;
            return 0;
        }
        else if (arg == "--print-media-database-id-reuse-lifecycle") {
            MediaDatabase db;
            std::wstring dbPath = L"test-media-cache-reuse.tsv"; // pytest passes a per-test tmp_path-scoped CWD
            db.Load(dbPath); // fresh/empty file is fine, Load() tolerates a missing file
            db.BeginScanPass();
            int idA = db.GetOrCreateStableId(L"key-a");
            int idB = db.GetOrCreateStableId(L"key-b");
            int idC = db.GetOrCreateStableId(L"key-c");
            std::wcout << L"initial=" << idA << L"," << idB << L"," << idC << std::endl;
            // Simulate a second pass where key-b is no longer present.
            db.BeginScanPass();
            db.GetOrCreateStableId(L"key-a");
            db.GetOrCreateStableId(L"key-c");
            size_t pruned = db.PruneUntouched();
            std::wcout << L"pruned=" << pruned << std::endl;
            // A brand-new key in the THIRD pass should reuse key-b's freed ID
            // rather than allocating a fresh one.
            db.BeginScanPass();
            db.GetOrCreateStableId(L"key-a");
            db.GetOrCreateStableId(L"key-c");
            int idD = db.GetOrCreateStableId(L"key-d");
            std::wcout << L"reused=" << idD << std::endl;
            return 0;
        }
        else if (arg == "--print-media-database-id-overflow-guard" && i + 1 < argc) {
            MediaDatabase db;
            db.Load(Utf8ToWide(argv[++i]));
            db.BeginScanPass();
            int idA = db.GetOrCreateStableId(L"new-key-a");
            int idB = db.GetOrCreateStableId(L"new-key-b");
            std::wcout << idA << std::endl;
            std::wcout << idB << std::endl;
            return 0;
        }
        else if (arg == "--print-clamp-browse-requested-count" && i + 2 < argc) {
            int requestedCount = std::atoi(argv[++i]);
            int available = std::atoi(argv[++i]);
            std::cout << ClampBrowseRequestedCount(requestedCount, available) << std::endl;
            return 0;
        }
        else if (arg == "--print-should-drop-link-local-endpoint" && i + 2 < argc) {
            bool candidateIsLinkLocal = std::string(argv[++i]) == "1";
            bool anyNonLinkLocalExists = std::string(argv[++i]) == "1";
            std::cout << (ShouldDropLinkLocalEndpoint(candidateIsLinkLocal, anyNonLinkLocalExists) ? "1" : "0") << std::endl;
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
        else if (arg == "--print-routable-host-url-twice" && i + 2 < argc) {
            int portOne = 0;
            int portTwo = 0;
            if (!TryParsePortStrict(argv[++i], portOne)) portOne = 0;
            if (!TryParsePortStrict(argv[++i], portTwo)) portTwo = 0;
            std::string first = GetRoutableHostUrl(portOne, L"");
            std::string second = GetRoutableHostUrl(portTwo, L"");
            std::cout << first << std::endl;
            std::cout << second << std::endl;
            return 0;
        }
        else if (arg == "--print-routable-host-cache-invalidation") {
            long before = GetRoutableHostUrlRecomputeCountForTest();
            GetRoutableHostUrl(9200, L"");
            long afterFirst = GetRoutableHostUrlRecomputeCountForTest();
            GetRoutableHostUrl(9200, L"");
            long afterSecondSamePort = GetRoutableHostUrlRecomputeCountForTest();
            InvalidateRoutableHostUrlCache();
            GetRoutableHostUrl(9200, L"");
            long afterInvalidate = GetRoutableHostUrlRecomputeCountForTest();
            std::cout << "before=" << before << std::endl;
            std::cout << "after-first-call=" << afterFirst << std::endl;
            std::cout << "after-second-call-same-port=" << afterSecondSamePort << std::endl;
            std::cout << "after-invalidate-then-call=" << afterInvalidate << std::endl;
            return 0;
        }
        else if (arg == "--print-notify-pool-worker-count") {
            std::cout << kMaxUpnpNotifyWorkers << std::endl;
            return 0;
        }
        else if (arg == "--print-should-use-unlisted-interface" && i + 2 < argc) {
            bool isVirtual = std::string(argv[++i]) == "1";
            bool hasGateway = std::string(argv[++i]) == "1";
            std::wcout << (ShouldUseUnlistedInterface(isVirtual, hasGateway) ? L"1" : L"0") << std::endl;
            return 0;
        }
        else if (arg == "--print-network-endpoint-count" && i + 1 < argc) {
            int testPort = 0;
            if (!TryParsePortStrict(argv[++i], testPort)) testPort = 8200;
            std::vector<NetworkEndpoint> endpoints;
            EnumerateNetworkEndpoints(testPort, L"", endpoints);
            std::cout << endpoints.size() << std::endl;
            return 0;
        }
        else if (arg == "--print-tray-notify-decode" && i + 2 < argc) {
            unsigned long rawLParam = static_cast<unsigned long>(std::strtoul(argv[++i], nullptr, 0));
            unsigned short expectedIconId = static_cast<unsigned short>(std::atoi(argv[++i]));
            switch (DecodeTrayNotifyEvent(rawLParam, expectedIconId)) {
            case TrayNotifyAction::Activate: std::cout << "activate" << std::endl; break;
            case TrayNotifyAction::ShowMenu: std::cout << "showmenu" << std::endl; break;
            case TrayNotifyAction::None: std::cout << "none" << std::endl; break;
            }
            return 0;
        }
        else if (arg == "--print-debug-log-session-truncation" && i + 1 < argc) {
            std::string path = argv[++i];
            {
                std::ofstream leftover(path, std::ios::binary | std::ios::trunc);
                leftover << "leftover line from a previous session\n";
            }
            FILE* first = OpenOrReuseDebugLogFile(path);
            if (first) {
                std::fprintf(first, "session line one\n");
                std::fflush(first);
            }
            FILE* second = OpenOrReuseDebugLogFile(path);
            if (second) {
                std::fprintf(second, "session line two\n");
                std::fflush(second);
            }
            std::cout << (first == second ? "same-handle-reused=1" : "same-handle-reused=0") << std::endl;
            return 0;
        }
        else if (arg == "--print-close-pending-lifecycle") {
            ClosePendingState state;
            std::wcout << L"initial-pending=" << (state.IsPending() ? 1 : 0) << std::endl;

            state.RequestCloseOnceStopped();
            std::wcout << L"after-request-pending=" << (state.IsPending() ? 1 : 0) << std::endl;

            bool closeNow = state.ShouldCloseNowAfterOperation(true);
            std::wcout << L"stop-completes-close-now=" << (closeNow ? 1 : 0) << std::endl;
            std::wcout << L"after-close-pending=" << (state.IsPending() ? 1 : 0) << std::endl;

            ClosePendingState restartCase;
            restartCase.RequestCloseOnceStopped();
            bool stopAgain = restartCase.ShouldStopAgainAfterOperation(true);
            std::wcout << L"restart-reaches-running-stop-again=" << (stopAgain ? 1 : 0) << std::endl;
            std::wcout << L"restart-still-pending=" << (restartCase.IsPending() ? 1 : 0) << std::endl;
            bool closeNowAfterSecondStop = restartCase.ShouldCloseNowAfterOperation(true);
            std::wcout << L"second-stop-completes-close-now=" << (closeNowAfterSecondStop ? 1 : 0) << std::endl;

            ClosePendingState neverRequested;
            std::wcout << L"never-requested-pending=" << (neverRequested.IsPending() ? 1 : 0) << std::endl;
            return 0;
        }
        else if (arg == "--print-config-path") {
            std::wcout << AppConfig.GetConfigPath() << std::endl;
            return 0;
        }
        else if (arg == "--print-resolve-bundled-resource" && i + 1 < argc) {
            std::cout << ResolveBundledResourcePath(argv[++i]) << std::endl;
            return 0;
        }
        else if (arg == "--print-media-source-file-extensions") {
            for (const auto& ext : GetMediaSourceFileExtensions()) {
                std::wcout << ext << std::endl;
            }
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
        else if (arg == "--print-concurrent-start-rescan-safety") {
            std::thread rescanThread([]() { DLNAServer.Rescan(); });
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            rescanThread.join();
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
            int leafMediaItems = 0;
            for (const auto& item : AppMedia.GetDescendants(0)) {
                if (!item.isFolder) ++leafMediaItems;
            }
            std::wcout << L"leaf-media-items=" << leafMediaItems << std::endl;
            DLNAServer.Stop();
            std::wcout << L"done" << std::endl;
            return 0;
        }
        else if (arg == "--print-single-file-source-scan") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            if (!startOk) {
                std::wcerr << L"start failed: " << reason << std::endl;
                return 1;
            }
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            int leafMediaItems = 0;
            std::wstring firstPath, firstMime, firstClass;
            for (const auto& item : AppMedia.GetDescendants(0)) {
                if (!item.isFolder) {
                    ++leafMediaItems;
                    if (leafMediaItems == 1) {
                        firstPath = item.path;
                        firstMime = item.mimeType;
                        firstClass = item.upnpClass;
                    }
                }
            }
            std::wcout << L"leaf-media-items=" << leafMediaItems << std::endl;
            std::wcout << L"first-path=" << firstPath << std::endl;
            std::wcout << L"first-mime=" << firstMime << std::endl;
            std::wcout << L"first-class=" << firstClass << std::endl;
            DLNAServer.Stop();
            return 0;
        }
        else if (arg == "--print-thread-guard-behavior") {
            RunGuarded(L"test-thread", []() {
                throw std::runtime_error("synthetic-test-exception");
            });
            std::cout << "guard-caught-exception=1" << std::endl;
            return 0;
        }
        else if (arg == "--debug") AppConfig.debugLog = true;
        else if (arg == "--help") {
            PrintUsage(argv[0]);
            std::cerr.flush();
            return 0;
        }
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

    // Single-instance lock: if another instance is already running,
    // try to show its window and exit.
    if (!SingleInstance::TryAcquireLock()) {
        if (SingleInstance::SendShow()) {
            return 0;
        }
        std::cerr << "Another instance of dlna-server is already running." << std::endl;
        return 1;
    }
    // Listen for IPC commands from short-lived --kill-server/--print-*
    // second instances (a second instance requests a graceful stop via
    // the "kill" command; see OnSingleInstanceCommand).
    SingleInstance::StartListening(OnSingleInstanceCommand);
    std::wstring outReason;
    if (!DLNAServer.Start(outReason)) {
        std::wcerr << L"Failed to start server: " << outReason << std::endl;
        return 1;
    }
    while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    DLNAServer.Stop();
    SingleInstance::ReleaseLock();
    return 0;
}
