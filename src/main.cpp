#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include "mainwindow.h"
#include "config.h"
#include "network_interface_policy.h"
#include "dlna_utils.h"
#include "http_common.h"
#include "media_scan_common.h"
#include "firewall_access.h"
#include "log.h"
#include "netutils.h"
#include "network_sources.h"
#include "server.h"
#include "media_sources.h"
#include "media_database.h"
#include "browse_page_cap.h"
#include "thread_guard.h"
#include "settings_restart.h"
#include "source_drop_target.h"
#include "startup_mode.h"
#include "access_key_hook.h"
#include "access_keys.h"
#include "hover_focus_state.h"
#include "input_gate.h"
#include "playlist_scan_concurrency.h"
#include "scan_cancellation.h"
#include "transmitfile_chunking.h"
#include "cli_flags.h"
#include "media_source_file_types.h"
#include "contentdirectory.h"
#include "upnp_eventing.h"
#include "server_close_policy.h"
#include "close_pending_state.h"
#include "function_key_action.h"
#include "tray_notify.h"
#include "../resources/resource.h"
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

std::atomic<bool> g_headlessConsoleStop(false);
HWND g_hwndMainForConsole = NULL;

BOOL WINAPI HeadlessConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        g_headlessConsoleStop = true;
        if (g_hwndMainForConsole) {
            PostMessageW(g_hwndMainForConsole, WM_CLOSE, 0, 0);
        }
        return TRUE;
    }
    return FALSE;
}

void PrintUsage() {
    std::wcerr << L"Usage: DLNA Server.exe [--help]\n";
    std::wcerr << L"       DLNA Server.exe [OPTIONS...] --source \"pathA\",\"pathB\"\n";
    for (auto& entry : GetCliFlagTable()) {
        std::wcerr << L"  " << entry.flag << L"  " << entry.meaning << L"\n";
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    bool configureFirewall = false;
    bool startHeadless = false;
    bool showHelp = false;
    bool debugFlag = false;
    bool killServer = false;
    int portArg = 0;
    std::wstring runtimeName;
    std::wstring runtimeUUID;
    std::vector<std::wstring> runtimeSources;

    AppConfig.Load();

    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--configure-firewall") == 0) {
            configureFirewall = true;
        } else if (wcscmp(argv[i], L"--headless") == 0 || wcscmp(argv[i], L"-h") == 0) {
            startHeadless = true;
        } else if (wcscmp(argv[i], L"--help") == 0) {
            showHelp = true;
        } else if (wcscmp(argv[i], L"--port") == 0 && i + 1 < argc) {
            ++i;
            if (!TryParsePortStrict(WideToUtf8(argv[i]), portArg)) portArg = 0;
        } else if (wcscmp(argv[i], L"--name") == 0 && i + 1 < argc) {
            runtimeName = argv[++i];
        } else if (wcscmp(argv[i], L"--uuid") == 0 && i + 1 < argc) {
            runtimeUUID = argv[++i];
        } else if (wcscmp(argv[i], L"--source") == 0 && i + 1 < argc) {
            ++i;
            std::vector<std::wstring> parsedSources = ParseQuotedCommaList(argv[i]);
            std::vector<MediaSource> immediateOverride;
            if (parsedSources.empty()) {
                runtimeSources.push_back(argv[i]);
                immediateOverride.push_back({argv[i]});
            } else {
                for (auto& parsed : parsedSources) {
                    runtimeSources.push_back(parsed);
                    immediateOverride.push_back({parsed});
                }
            }
            // Apply immediately so subsequent --print-* hooks in this same
            // argv see the override; post-loop application below is kept
            // for the non-print normal-Start path and is idempotent here.
            AppConfig.SetRuntimeSourceOverride(immediateOverride);
        } else if (wcscmp(argv[i], L"--kill-server") == 0 || wcscmp(argv[i], L"-k") == 0) {
            killServer = true;
        } else if (wcscmp(argv[i], L"--debug") == 0) {
            debugFlag = true;
        } else if (wcscmp(argv[i], L"--print-scan-cancellation-lifecycle") == 0) {
            AppScanCancel.BeginScan();
            std::wcout << (AppScanCancel.IsCancelled() ? L"1" : L"0") << std::endl;
            AppScanCancel.RequestCancel();
            std::wcout << (AppScanCancel.IsCancelled() ? L"1" : L"0") << std::endl;
            AppScanCancel.BeginScan();
            std::wcout << (AppScanCancel.IsCancelled() ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-single-instance-lifecycle") == 0) {
            HANDLE hMutex = CreateMutexW(NULL, TRUE, L"dlna-server_SingleInstance_Mutex");
            if (!hMutex) {
                LocalFree(argv);
                return 1;
            }

            bool acquired = GetLastError() != ERROR_ALREADY_EXISTS;
            if (!acquired) {
                DWORD waitResult = WaitForSingleObject(hMutex, 0);
                acquired = (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED_0);
            }

            std::wcout << (acquired ? L"lock-acquired" : L"lock-busy") << std::endl;
            if (acquired) {
                ReleaseMutex(hMutex);
                std::wcout << L"released" << std::endl;
            }
            CloseHandle(hMutex);
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-log-since-lifecycle") == 0) {
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
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-scan-concurrency") == 0 && i + 1 < argc) {
            size_t n = static_cast<size_t>(_wtoi(argv[++i]));
            std::cout << ComputePlaylistScanConcurrency(n) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-mnemonics") == 0 && i + 1 < argc) {
            std::wstring arg = argv[++i];
            std::vector<std::wstring> labels;
            size_t start = 0;
            for (size_t j = 0; j <= arg.size(); ++j) {
                if (j == arg.size() || arg[j] == L',') {
                    labels.push_back(arg.substr(start, j - start));
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
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-cue-state") == 0 && i + 1 < argc) {
            std::wstring seq = argv[++i];
            KeyboardCueState cs;
            for (wchar_t ch : seq) {
                if (ch == L'k' || ch == L'K') cs.OnKeyboardInput();
                else if (ch == L'm' || ch == L'M') cs.OnMouseButtonInput();
                std::cout << (cs.HideAccel() ? "1" : "0") << "," << (cs.HideFocus() ? "1" : "0") << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-hover-focus-state") == 0 && i + 1 < argc) {
            std::wstring seq = argv[++i];
            HoverFocusState state;
            size_t start = 0;
            while (start <= seq.size()) {
                size_t comma = seq.find(L',', start);
                std::wstring token = seq.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
                if (!token.empty()) {
                    wchar_t code = token[0];
                    int id = _wtoi(token.c_str() + 1);
                    if (code == L'e') state.OnMouseEnter(id);
                    else if (code == L'l') state.OnMouseLeave(id);
                    else if (code == L'f') state.OnFocusGained(id);
                    else if (code == L'b') state.OnFocusLost(id);
                    std::wcout << state.HighlightedControlId() << std::endl;
                }
                if (comma == std::wstring::npos) break;
                start = comma + 1;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-any-field-has-content") == 0 && i + 1 < argc) {
            std::wstring csv = argv[++i];
            std::vector<int> lens;
            size_t start = 0;
            while (start <= csv.size()) {
                size_t comma = csv.find(L',', start);
                std::wstring token = csv.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
                if (!token.empty()) lens.push_back(_wtoi(token.c_str()));
                if (comma == std::wstring::npos) break;
                start = comma + 1;
            }
            std::wcout << (AnyFieldHasContent(lens) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-is-recognized-playlist") == 0 && i + 2 < argc) {
            std::wstring path = argv[++i];
            std::wstring textFilePath = argv[++i];
            std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
            std::ostringstream ss;
            ss << file.rdbuf();
            std::cout << (IsRecognizedPlaylistText(path, ss.str()) ? "1" : "0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-parse-quoted-comma-list") == 0 && i + 1 < argc) {
            for (const auto& field : ParseQuotedCommaList(argv[++i])) {
                std::wcout << field << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-trim-wide") == 0 && i + 1 < argc) {
            std::wcout << TrimWide(argv[++i]) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-decode-legacy-pipe-sources") == 0 && i + 1 < argc) {
            for (const auto& field : DecodeLegacyPipeDelimitedSources(argv[++i])) {
                std::wcout << field << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-resolve-relative-url") == 0 && i + 2 < argc) {
            std::wstring baseUrl = argv[++i];
            std::wstring relativeUrl = argv[++i];
            std::wcout << ResolveRelativeUrl(baseUrl, relativeUrl) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-resource-url-suffix") == 0 && i + 1 < argc) {
            std::cout << BuildMediaResourceUrlExtensionSuffix(argv[++i]) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-movie-title-from-path") == 0 && i + 1 < argc) {
            // Mirrors settingsdlg.cpp's (private) MovieTitleFromPath
            // exactly: SourceStemName(path) with a "Media item" fallback
            // for an empty stem. See F-01. This flag exists purely so a
            // pytest test can exercise the long-path case without
            // driving the native modal dialog message loop.
            std::wstring path = argv[++i];
            std::wstring stem = SourceStemName(path);
            std::wcout << (stem.empty() ? L"Media item" : stem) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-strip-resource-id-extension") == 0 && i + 1 < argc) {
            std::cout << StripResourceIdExtension(WideToUtf8(argv[++i])) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-display-title") == 0 && i + 3 < argc) {
            bool showFileNames = wcscmp(argv[++i], L"1") == 0;
            std::wstring titleOverride = argv[++i];
            std::wstring path = argv[++i];
            std::wcout << BuildDisplayTitleForMediaFile(showFileNames, titleOverride, path) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-rewrite-hls-manifest") == 0 && i + 2 < argc) {
            std::wstring baseUrl = argv[++i];
            std::wstring textFilePath = argv[++i];
            std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
            std::ostringstream ss;
            ss << file.rdbuf();
            std::cout << RewriteHlsManifestUrisToAbsolute(baseUrl, ss.str()) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-should-start-headless") == 0 && i + 2 < argc) {
            bool explicitFlag = wcscmp(argv[++i], L"1") == 0;
            bool hasSources = wcscmp(argv[++i], L"1") == 0;
            std::wcout << (ShouldStartHeadless(explicitFlag, hasSources) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-should-close-now") == 0 && i + 2 < argc) {
            bool isRunning = wcscmp(argv[++i], L"1") == 0;
            bool isBusy = wcscmp(argv[++i], L"1") == 0;
            std::wcout << (ShouldCloseNow(isRunning, isBusy) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-is-plausible-copydata-size") == 0 && i + 1 < argc) {
            unsigned long cbData = wcstoul(argv[++i], nullptr, 10);
            std::wcout << (IsPlausibleWideStringCopyDataSize(cbData) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-database-id-reuse-lifecycle") == 0) {
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
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-database-id-overflow-guard") == 0 && i + 1 < argc) {
            MediaDatabase db;
            db.Load(argv[++i]);
            db.BeginScanPass();
            int idA = db.GetOrCreateStableId(L"new-key-a");
            int idB = db.GetOrCreateStableId(L"new-key-b");
            std::wcout << idA << std::endl;
            std::wcout << idB << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-clamp-browse-requested-count") == 0 && i + 2 < argc) {
            int requestedCount = _wtoi(argv[++i]);
            int available = _wtoi(argv[++i]);
            std::cout << ClampBrowseRequestedCount(requestedCount, available) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-should-drop-link-local-endpoint") == 0 && i + 2 < argc) {
            bool candidateIsLinkLocal = wcscmp(argv[++i], L"1") == 0;
            bool anyNonLinkLocalExists = wcscmp(argv[++i], L"1") == 0;
            std::wcout << (ShouldDropLinkLocalEndpoint(candidateIsLinkLocal, anyNonLinkLocalExists) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-should-evict-before-cache-insert") == 0 && i + 2 < argc) {
            size_t currentSize = static_cast<size_t>(_wtoi64(argv[++i]));
            size_t capacity = static_cast<size_t>(_wtoi64(argv[++i]));
            std::wcout << (ShouldEvictBeforeCacheInsert(currentSize, capacity) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-transmitfile-chunk-plan") == 0 && i + 1 < argc) {
            long long totalBytes = _wtoi64(argv[++i]);
            for (long long chunkSize : ComputeTransmitFileChunkSizes(totalBytes)) {
                std::wcout << chunkSize << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-function-key-action") == 0 && i + 4 < argc) {
            int vkCode = _wtoi(argv[++i]);
            bool isRunning = wcscmp(argv[++i], L"1") == 0;
            bool isBusy = wcscmp(argv[++i], L"1") == 0;
            bool isScanning = wcscmp(argv[++i], L"1") == 0;
            switch (DecideFunctionKeyAction(vkCode, isRunning, isBusy, isScanning)) {
            case FunctionKeyAction::ShowHelp: std::wcout << L"show-help" << std::endl; break;
            case FunctionKeyAction::Rescan: std::wcout << L"rescan" << std::endl; break;
            case FunctionKeyAction::RefreshSourceList: std::wcout << L"refresh-source-list" << std::endl; break;
            case FunctionKeyAction::ShowSourceListContextMenu: std::wcout << L"show-context-menu" << std::endl; break;
            case FunctionKeyAction::None: std::wcout << L"none" << std::endl; break;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-debug-log-requires-restart") == 0 && i + 2 < argc) {
            ConfigSnapshot before{};
            ConfigSnapshot after{};
            before.debugLog = wcscmp(argv[++i], L"1") == 0;
            after.debugLog = wcscmp(argv[++i], L"1") == 0;
            std::vector<std::wstring> changed = DetermineSettingsRequiringRestart(before, after);
            bool found = false;
            for (const auto& name : changed) {
                if (name == L"Debug Log") found = true;
            }
            std::wcout << (found ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-browsing-restart-required") == 0 && i + 2 < argc) {
            auto parseFlags = [](const std::wstring& bits, ConfigSnapshot& snap) {
                snap.addArtistAlbumFolders        = bits.size() > 0 && bits[0] == L'1';
                snap.doNotShowAllMediaFolders     = bits.size() > 1 && bits[1] == L'1';
                snap.sortByTitle                  = bits.size() > 2 && bits[2] == L'1';
                snap.flatFolderStyle              = bits.size() > 3 && bits[3] == L'1';
                snap.showFileNamesInsteadOfTitles = bits.size() > 4 && bits[4] == L'1';
                snap.proxyStreams                 = bits.size() > 5 && bits[5] == L'1';
                snap.backgroundScanEnabled        = bits.size() > 6 && bits[6] == L'1';
            };
            ConfigSnapshot before{};
            ConfigSnapshot after{};
            parseFlags(argv[++i], before);
            parseFlags(argv[++i], after);
            std::vector<std::wstring> changed = DetermineSettingsRequiringRestart(before, after);
            std::wcout << (changed.empty() ? L"0" : L"1") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-should-allow-source-drop") == 0 && i + 1 < argc) {
            bool busyOrRunning = wcscmp(argv[++i], L"1") == 0;
            std::wcout << (ShouldAllowSourceDrop(busyOrRunning) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-is-supported-source-path") == 0 && i + 1 < argc) {
            std::wcout << (IsSupportedLocalMediaOrPlaylistPath(argv[++i]) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-routable-host-url-twice") == 0 && i + 2 < argc) {
            int portOne = 0;
            int portTwo = 0;
            if (!TryParsePortStrict(WideToUtf8(argv[++i]), portOne)) portOne = 0;
            if (!TryParsePortStrict(WideToUtf8(argv[++i]), portTwo)) portTwo = 0;
            std::string first = GetRoutableHostUrl(portOne, L"");
            std::string second = GetRoutableHostUrl(portTwo, L"");
            std::cout << first << std::endl;
            std::cout << second << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-routable-host-cache-invalidation") == 0) {
            long before = GetRoutableHostUrlRecomputeCountForTest();
            GetRoutableHostUrl(9200, L"");
            long afterFirst = GetRoutableHostUrlRecomputeCountForTest();
            GetRoutableHostUrl(9200, L"");
            long afterSecondSamePort = GetRoutableHostUrlRecomputeCountForTest();
            InvalidateRoutableHostUrlCache();
            GetRoutableHostUrl(9200, L"");
            long afterInvalidate = GetRoutableHostUrlRecomputeCountForTest();
            std::wcout << L"before=" << before << std::endl;
            std::wcout << L"after-first-call=" << afterFirst << std::endl;
            std::wcout << L"after-second-call-same-port=" << afterSecondSamePort << std::endl;
            std::wcout << L"after-invalidate-then-call=" << afterInvalidate << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-should-use-unlisted-interface") == 0 && i + 2 < argc) {
            bool isVirtual = wcscmp(argv[++i], L"1") == 0;
            bool hasGateway = wcscmp(argv[++i], L"1") == 0;
            std::wcout << (ShouldUseUnlistedInterface(isVirtual, hasGateway) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-network-endpoint-count") == 0 && i + 1 < argc) {
            int testPort = 0;
            if (!TryParsePortStrict(WideToUtf8(argv[++i]), testPort)) testPort = 8200;
            std::vector<NetworkEndpoint> endpoints;
            EnumerateNetworkEndpoints(testPort, L"", endpoints);
            std::cout << endpoints.size() << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-sockaddr-length-safety") == 0 && i + 2 < argc) {
            int reportedLength = _wtoi(argv[++i]);
            int destinationCapacity = _wtoi(argv[++i]);
            std::wcout << (IsSockaddrLengthSafeToCopy(reportedLength, static_cast<size_t>(destinationCapacity)) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-notify-pool-worker-count") == 0) {
            std::cout << kMaxUpnpNotifyWorkers << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-max-client-threads") == 0) {
            std::wcout << kMaxClientThreads << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-dlna-server-header") == 0) {
            std::cout << GetDlnaServerHeader() << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-config-path") == 0) {
            std::wcout << AppConfig.GetConfigPath() << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-default-playlist-path") == 0) {
            // F-02 regression hook: confirms GetDefaultPlaylistPath()
            // still equals "<directory of GetConfigPath()>\default.m3u"
            // after removing the fixed wchar_t[MAX_PATH] buffer.
            std::wcout << AppConfig.GetDefaultPlaylistPath() << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-source-file-extensions") == 0) {
            for (const auto& ext : GetMediaSourceFileExtensions()) {
                std::wcout << ext << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-network-interface-allow-list-accessor") == 0) {
            // regression hook for the config accessor change
            // writes a value through Mutate then reads it back through
            // the new shared lock protected accessor
            AppConfig.Mutate([](Config& cfg) {
                cfg.networkInterfaceAllowList = L"eth0,wlan0";
            });
            std::wcout << AppConfig.GetNetworkInterfaceAllowList() << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-format-lookup") == 0 && i + 1 < argc) {
            // regression hook for the extension hash lookup change
            // prints the resolved media format fields on three lines or
            // the single line "no-match" when no format matches
            std::wstring extArg = argv[++i];
            MediaFormatInfo info;
            if (!GetMediaFormatForExtension(extArg, info)) {
                std::wcout << L"no-match" << std::endl;
            } else {
                std::wcout << info.mimeType << std::endl;
                std::wcout << info.upnpClass << std::endl;
                std::wcout << Utf8ToWide(info.dlnaProfile) << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-sources") == 0) {
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.mediaSources) {
                std::wcout << src.path << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-effective-media-sources") == 0) {
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.effectiveMediaSources) {
                std::wcout << src.path << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-clear-override-then-effective") == 0) {
            AppConfig.ClearRuntimeSourceOverride();
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.effectiveMediaSources) {
                std::wcout << src.path << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-source-override-lifecycle") == 0 && i + 1 < argc) {
            std::vector<std::wstring> parsedSources = ParseQuotedCommaList(argv[++i]);
            std::vector<MediaSource> overrideSources;
            for (auto& parsed : parsedSources) {
                if (!parsed.empty()) overrideSources.push_back({parsed});
            }
            AppConfig.SetRuntimeSourceOverride(overrideSources);

            std::wstring reason;
            if (!DLNAServer.Start(reason)) {
                std::wcerr << L"start1 failed: " << reason << std::endl;
                LocalFree(argv);
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
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-concurrent-start-rescan-safety") == 0) {
            // Regression test for F-CRASH-01. Exercises both legal orderings
            // deterministically via two explicit sub-tests.
            //
            // Sub-test A: Start completes first, then Rescan runs.
            // Sub-test B: Rescan races Start (original racy design, kept to
            //             cover the blocking-on-m_rescanMutex path).
            //
            // Run with --source pointing at a folder containing a known number
            // of media files.

            auto _dbg = [](const char* tag) {
                std::cerr << "[DBGCONC] " << tag << std::endl;
            };
            auto waitLeafCount = [&]() -> int {
                while (DLNAServer.IsInitialScanInProgress()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                int n = 0;
                for (const auto& item : AppMedia.GetDescendants(0)) {
                    if (!item.isFolder) ++n;
                }
                return n;
            };

            // Sub-test A: Start first, Rescan only after IsRunning==true.
            _dbg("A-start");
            {
                std::wstring reason;
                bool startOk = DLNAServer.Start(reason);
                _dbg("A-start-done");
                // Wait until running so Rescan deterministically races the
                // already-started server, not the starting server.
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (!DLNAServer.IsRunning() &&
                       std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                std::thread rescanThreadA([]() { DLNAServer.Rescan(); });
                _dbg("A-rescan-launched");
                rescanThreadA.join();
                _dbg("A-rescan-joined");
                int leafA = waitLeafCount();
                DLNAServer.Stop();
                _dbg("A-stop-done");
                std::wcout << L"subtest-a-start-ok=" << (startOk ? L"1" : L"0") << std::endl;
                std::wcout << L"subtest-a-leaf-media-items=" << leafA << std::endl;
            }

            // Sub-test B: original racy design — Rescan thread launched before
            // Start() returns. Pass criterion: IsRunning==true and leaf count
            // is stable (not a specific interleaving).
            _dbg("B-start");
            {
                std::thread rescanThreadB([]() { DLNAServer.Rescan(); });
                _dbg("B-rescan-launched");
                std::wstring reason;
                bool startOk = DLNAServer.Start(reason);
                _dbg("B-start-done");
                rescanThreadB.join();
                _dbg("B-rescan-joined");
                int leafB = waitLeafCount();
                DLNAServer.Stop();
                _dbg("B-stop-done");
                std::wcout << L"subtest-b-start-ok=" << (startOk ? L"1" : L"0") << std::endl;
                std::wcout << L"subtest-b-leaf-media-items=" << leafB << std::endl;
            }

            std::wcout << L"done" << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-single-file-source-scan") == 0) {
            // Regression test for F-FEAT-01. Run with --source pointing
            // at a single supported media file.
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            if (!startOk) {
                std::wcerr << L"start failed: " << reason << std::endl;
                LocalFree(argv);
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
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-search-cache-cleared-on-rescan") == 0) {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            if (!startOk) {
                std::wcerr << L"start failed: " << reason << std::endl;
                LocalFree(argv);
                return 1;
            }
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const std::string searchSoap =
                "<?xml version=\"1.0\"?>\n"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
                "  <s:Body>\n"
                "    <u:Search xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n"
                "      <ContainerID>0</ContainerID>\n"
                "      <SearchCriteria></SearchCriteria>\n"
                "      <Filter>*</Filter>\n"
                "      <StartingIndex>0</StartingIndex>\n"
                "      <RequestedCount>100</RequestedCount>\n"
                "      <SortCriteria></SortCriteria>\n"
                "    </u:Search>\n"
                "  </s:Body>\n"
                "</s:Envelope>\n";
            AppContent.HandleContentDirectoryControl(searchSoap, "http://127.0.0.1:0");
            std::wcout << L"before-rescan-cache-size=" << AppContent.GetSearchCacheSizeForTest() << std::endl;
            DLNAServer.Rescan();
            std::wcout << L"after-rescan-cache-size=" << AppContent.GetSearchCacheSizeForTest() << std::endl;
            DLNAServer.Stop();
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-thread-guard-behavior") == 0) {
            // Regression test for Task 1.1's RunGuarded helper itself:
            // proves an exception thrown inside it does not propagate
            // and does not terminate the process.
            RunGuarded(L"test-thread", []() {
                throw std::runtime_error("synthetic-test-exception");
            });
            std::cout << "guard-caught-exception=1" << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-thread-pool-exception-resilience") == 0) {
            BoundedThreadPool pool(1);
            std::promise<bool> survived;
            std::future<bool> survivedFuture = survived.get_future();
            pool.Submit([]() {
                throw std::runtime_error("synthetic-pool-test-exception");
            });
            pool.Submit([&survived]() {
                survived.set_value(true);
            });
            bool ok = survivedFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready
                       && survivedFuture.get();
            std::wcout << L"pool-survived-exception=" << (ok ? 1 : 0) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-tray-notify-decode") == 0 && i + 2 < argc) {
            unsigned long rawLParam = static_cast<unsigned long>(wcstoul(argv[++i], nullptr, 0));
            unsigned short expectedIconId = static_cast<unsigned short>(_wtoi(argv[++i]));
            switch (DecodeTrayNotifyEvent(rawLParam, expectedIconId)) {
            case TrayNotifyAction::Activate: std::cout << "activate" << std::endl; break;
            case TrayNotifyAction::ShowMenu: std::cout << "showmenu" << std::endl; break;
            case TrayNotifyAction::None: std::cout << "none" << std::endl; break;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-debug-log-session-truncation") == 0 && i + 1 < argc) {
            std::wstring path = argv[++i];
            {
                FILE* leftover = nullptr;
                _wfopen_s(&leftover, path.c_str(), L"w,ccs=UTF-8");
                if (leftover) {
                    fwprintf(leftover, L"leftover line from a previous session\r\n");
                    fclose(leftover);
                }
            }
            FILE* first = OpenOrReuseDebugLogFile(path);
            if (first) {
                fwprintf(first, L"session line one\r\n");
                fflush(first);
            }
            FILE* second = OpenOrReuseDebugLogFile(path);
            if (second) {
                fwprintf(second, L"session line two\r\n");
                fflush(second);
            }
            std::wcout << (first == second ? L"same-handle-reused=1" : L"same-handle-reused=0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-close-pending-lifecycle") == 0) {
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
            LocalFree(argv);
            return 0;
        } else {
            runtimeSources.push_back(argv[i]);
        }
    }

    if (killServer) {
        LocalFree(argv);
        HWND hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
        if (hwndExisting) {
            PostMessageW(hwndExisting, MainWindow::WM_KILL_SERVER, 0, 0);
        }
        return 0;
    }

    if (showHelp) {
        PrintUsage();
        LocalFree(argv);
        return 0;
    }

    if (configureFirewall) {
        LocalFree(argv);
        AppConfig.Load();
        int port = portArg > 0 ? portArg : AppConfig.port;
        std::wstring message;
        return ConfigureFirewallAccessElevated(port, message) ? 0 : 1;
    }

    if (portArg > 0 && portArg <= 65535) AppConfig.port = portArg;
    if (!runtimeName.empty()) AppConfig.serverName = runtimeName;
    if (!runtimeUUID.empty()) AppConfig.deviceUUID = runtimeUUID;
    if (debugFlag) AppConfig.debugLog = true;
    if (!runtimeSources.empty()) {
        HWND hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
        if (hwndExisting) {
            std::wstring payload = BuildQuotedCommaList(runtimeSources);
            COPYDATASTRUCT cds{};
            cds.dwData = MainWindow::kCopyDataSourceReplace;
            cds.cbData = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
            cds.lpData = const_cast<wchar_t*>(payload.c_str());
            SendMessageW(hwndExisting, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
            LocalFree(argv);
            return 0;
        }
        std::vector<MediaSource> overrideSources;
        for (const auto& src : runtimeSources) {
            overrideSources.push_back({src});
        }
        AppConfig.SetRuntimeSourceOverride(overrideSources);
    }

    startHeadless = ShouldStartHeadless(startHeadless, !runtimeSources.empty());

    LocalFree(argv);

    // Check for single instance
    // CreateMutexW with bInitialOwner=TRUE returns ERROR_ALREADY_EXISTS
    // even when the previous owner has terminated (abandoned mutex).
    // Without the WAIT_ABANDONED_0 check below, a killed/crashed process
    // permanently prevents any new instance from starting until the zombie
    // process is manually killed. See workflow: stale-mutex-remediation.
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"dlna-server_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD waitResult = WaitForSingleObject(hMutex, 0);
        if (waitResult != WAIT_ABANDONED_0) {
            // Ask the existing instance to restore itself through
            // MainWindow::RestoreAndFocusMainWindow (see mainwindow.cpp), which
            // is the only code path that clears WS_EX_TOOLWINDOW when the
            // running instance was originally started with --headless. Do NOT
            // call ShowWindow/SetForegroundWindow directly here: this process
            // cannot call methods on the other process's MainWindow instance,
            // and skipping that code path is what left the window with a
            // permanently "lite" frame (no icon, no min/max buttons, tiny
            // close button) in the original bug.
            HWND hwndExisting = NULL;
            for (int attempt = 0; attempt < 25 && !hwndExisting; ++attempt) {
                hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
                if (!hwndExisting) Sleep(200);
            }
            if (hwndExisting) {
                PostMessageW(hwndExisting, MainWindow::WM_SHOW_EXISTING_INSTANCE, 0, 0);
            } else {
                // The mutex is held (another process is starting or running) but
                // its window never appeared within 5 seconds -- it may have
                // crashed between CreateMutexW and MainWindow::Create, or is
                // hung. Do not exit silently; this mirrors the POSIX side's
                // stderr message for the equivalent unreachable-peer case.
                std::wcerr << L"DLNA Server: an existing instance is starting or "
                              L"running but its window could not be found; exiting without action." << std::endl;
            }
            return 0;
        }
        // WAIT_ABANDONED_0: previous instance terminated without releasing
        // mutex. We now own it; continue with startup.
        ReleaseMutex(hMutex);
        // Re-acquire with WaitForSingleObject so the abandoned state is
        // cleared and normal ownership semantics apply going forward.
        WaitForSingleObject(hMutex, INFINITE);
    }

    // Attach console for headless mode output
    bool consoleAttached = false;
    FILE* fpOut = NULL;
    FILE* fpErr = NULL;
    if (startHeadless) {
        consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
        if (consoleAttached && GetLastError() == ERROR_ACCESS_DENIED) {
        } else if (!consoleAttached && GetLastError() == ERROR_INVALID_HANDLE) {
            consoleAttached = false;
        } else if (consoleAttached) {
            _wfreopen_s(&fpOut, L"CONOUT$", L"w", stdout);
            _wfreopen_s(&fpErr, L"CONOUT$", L"w", stderr);
        }

        if (AppConfig.debugLog) {
            SetConsoleCtrlHandler(HeadlessConsoleCtrlHandler, TRUE);
            if (consoleAttached) {
                SetConsoleEchoEnabled(true);
            }
        }
    }

    if (!InstallAccessKeyHook()) {
        // Non-fatal: keyboard cues will default to always-hidden Windows behaviour
    }

    MainWindow app;
    if (!app.Create(hInstance, startHeadless ? SW_HIDE : nCmdShow, startHeadless)) {
        RemoveAccessKeyHook();
        return 0;
    }

    HWND hwndMain = app.GetHwnd();
    g_hwndMainForConsole = hwndMain;
    if (startHeadless) {
        if (!AppConfig.debugLog) {
            std::wcout << L"\nserver is up" << std::flush;
            FreeConsole();
        }
        PostMessageW(hwndMain, WM_COMMAND, IDC_BTN_STARTSTOP, 0);
    }

    HWND hwndMainForNav = hwndMain;
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_CHAR && !(GetKeyState(VK_MENU) < 0)) {
            if (app.TryHandleAccessKeyChar(static_cast<wchar_t>(msg.wParam))) {
                continue; // consumed as an access key trigger, do not also dispatch it
            }
        }
        if (msg.message == WM_KEYDOWN) {
            if (app.TryHandleFunctionKey(msg.wParam)) {
                continue;
            }
        }
        if (!IsDialogMessageW(hwndMainForNav, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    RemoveAccessKeyHook();
    return 0;
}
