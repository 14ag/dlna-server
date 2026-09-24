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
#include "source_drop_policy.h"
#include "input_gate.h"
#include "network_sources.h"
#include "playlist_scan_concurrency.h"
#include "media_sources.h"
#include "media_source_file_types.h"
#include "contentdirectory.h"
#include "ssdp_common.h"
#include "thread_guard.h"
#include "scan_cancellation.h"
#include "server.h"
#include "upnp_eventing.h"
#include "transmitfile_chunking.h"
#include "close_pending_state.h"
#include "modal_stack.h"
#include "server_close_policy.h"
#include "health_check_policy.h"
#include "geometry_dump_policy.h"
#include "tray_notify.h"
#include "settings_restart.h"
#include "startup_mode.h"
#include "posix_single_instance.h"
#include "posix_daemonize.h"
#include "cli_flags.h"
#include "copydata_validation.h"
#include "media_database.h"
#include "browse_page_cap.h"
#include "httpserver.h"
#include "ssdp.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include "cli_print_hooks_posix.h"

void PrintUsage(const char* exe) {
    std::cerr << "Usage: " << exe << " [--port 8200] [--name NAME] [--uuid UUID] [--debug] [--no-debug] --source \"pathA\",\"pathB\"\n";
    std::cerr << "Sources can be folders, playlist files (.m3u, .m3u8, .pls), smb:// URLs, or ftp:// URLs.\n";
}

namespace {

bool IsPrintHook(const char* arg) {
    return arg && arg[0] == '-' && arg[1] == '-' && strstr(arg, "--print-") == arg;
}

} // namespace

bool TryRunPrintHook(int argc, char** argv, int& exitCode) {
    if (argc < 2) return false;

// Prescan argv in order and apply the same immediate side effects the
// main loop applies before a print flag so port name uuid source debug
// kill and help handling match the old inline behavior exactly
    int i = 1;
    for (; i < argc; ++i) {
        std::string pre = argv[i];
        if (pre == "--port" && i + 1 < argc) {
            int port = 0;
            if (!TryParsePortStrict(argv[++i], port)) {
                PrintUsage(argv[0]);
                exitCode = 2;
                return true;
            }
            AppConfig.Mutate([&](Config& cfg) { cfg.port = port; });
        }
        else if (pre == "--name" && i + 1 < argc) {
            AppConfig.Mutate([&](Config& cfg) { cfg.serverName = Utf8ToWide(argv[++i]); });
        }
        else if (pre == "--uuid" && i + 1 < argc) {
            AppConfig.Mutate([&](Config& cfg) { cfg.deviceUUID = Utf8ToWide(argv[++i]); });
        }
        else if (pre == "--source" && i + 1 < argc) {
            ++i;
            std::vector<std::wstring> parsedSources = ParseQuotedCommaList(Utf8ToWide(argv[i]));
            std::vector<MediaSource> immediateOverride;
            if (parsedSources.empty()) {
                immediateOverride.push_back({Utf8ToWide(argv[i])});
            } else {
                for (auto& parsed : parsedSources) {
                    immediateOverride.push_back({parsed});
                }
            }
            AppConfig.SetRuntimeSourceOverride(immediateOverride);
        }
        else if (pre == "--kill-server" || pre == "-k" || pre == "--kill") {
            if (!SingleInstance::SendKill()) {
                std::cerr << "No running dlna-server instance found." << std::endl;
                exitCode = 1;
                return true;
            }
            exitCode = 0;
            return true;
        }
        else if (pre == "--headless" || pre == "-h") {
            // Accepted for symmetry with the Win32 binary
        }
        else if (pre == "--no-debug") {
            AppConfig.debugLog = false;
        }
        else if (pre == "--debug") {
            AppConfig.debugLog = true;
        }
        else if (pre == "--help") {
            PrintUsage(argv[0]);
            std::cerr.flush();
            exitCode = 0;
            return true;
        }
        else if (IsPrintHook(argv[i])) {
            break;
        }
        else if (!pre.empty() && pre[0] == '-') {
            std::cerr << "Unknown option: " << pre << std::endl;
            PrintUsage(argv[0]);
            exitCode = 2;
            return true;
        }
    }
    if (i >= argc) return false;
    std::string arg = argv[i];
        if (arg == "--print-scan-cancellation-lifecycle") {
            AppScanCancel.BeginScan();
            std::cout << (AppScanCancel.IsCancelled() ? "1" : "0") << std::endl;
            AppScanCancel.RequestCancel();
            std::cout << (AppScanCancel.IsCancelled() ? "1" : "0") << std::endl;
            AppScanCancel.BeginScan();
            std::cout << (AppScanCancel.IsCancelled() ? "1" : "0") << std::endl;
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-single-instance-retry-timing") {
            // Runs against a not-yet-listening socket (fresh XDG_RUNTIME_DIR
            // with no server holding the lock or bound to the domain socket),
            // so every SendShow attempt fails with ECONNREFUSED. The elapsed
            // time proves the retry loop actually slept between attempts and
            // the false result proves it gives up instead of hanging.
            const auto start = std::chrono::steady_clock::now();
            const bool delivered = SingleInstance::SendShowWithRetry(3, 50);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::cout << "delivered=" << (delivered ? 1 : 0) << " elapsed-ms=" << elapsed << std::endl;
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-config-load-lockstate") {
            std::cout << "before load" << std::endl;
            AppConfig.Load();
            std::cout << "after load ok" << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-scan-concurrency" && i + 1 < argc) {
            size_t n = static_cast<size_t>(std::atoll(argv[++i]));
            std::cout << ComputePlaylistScanConcurrency(n) << std::endl;
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-cue-state" && i + 1 < argc) {
            std::string seq = argv[++i];
            KeyboardCueState cs;
            for (char ch : seq) {
                if (ch == 'k' || ch == 'K') cs.OnKeyboardInput();
                else if (ch == 'm' || ch == 'M') cs.OnMouseButtonInput();
                std::cout << (cs.HideAccel() ? "1" : "0") << "," << (cs.HideFocus() ? "1" : "0") << std::endl;
            }
            exitCode = 0; return true;
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
            exitCode = 0; return true;
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
            exitCode = 0; return true;
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
            case FunctionKeyAction::NoAction: std::cout << "none" << std::endl; break;
            }
            exitCode = 0; return true;
        }
        else if (arg == "--print-is-recognized-playlist" && i + 2 < argc) {
            std::wstring path = Utf8ToWide(argv[++i]);
            std::wstring textFilePath = Utf8ToWide(argv[++i]);
            std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
            std::ostringstream ss;
            ss << file.rdbuf();
            std::cout << (IsRecognizedPlaylistText(path, ss.str()) ? "1" : "0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-parse-quoted-comma-list" && i + 1 < argc) {
            for (const auto& field : ParseQuotedCommaList(Utf8ToWide(argv[++i]))) {
                std::wcout << field << std::endl;
            }
            exitCode = 0; return true;
        }
        else if (arg == "--print-trim-wide" && i + 1 < argc) {
            std::wcout << TrimWide(Utf8ToWide(argv[++i])) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-decode-legacy-pipe-sources" && i + 1 < argc) {
            for (const auto& field : DecodeLegacyPipeDelimitedSources(Utf8ToWide(argv[++i]))) {
                std::wcout << field << std::endl;
            }
            exitCode = 0; return true;
        }
        else if (arg == "--print-resolve-relative-url" && i + 2 < argc) {
            std::wstring baseUrl = Utf8ToWide(argv[++i]);
            std::wstring relativeUrl = Utf8ToWide(argv[++i]);
            std::wcout << ResolveRelativeUrl(baseUrl, relativeUrl) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-media-resource-url-suffix" && i + 1 < argc) {
            std::cout << BuildMediaResourceUrlExtensionSuffix(Utf8ToWide(argv[++i])) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-strip-resource-id-extension" && i + 1 < argc) {
            std::cout << StripResourceIdExtension(argv[++i]) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-movie-title-from-path" && i + 1 < argc) {
            // mirrors settingsdlg cpp MovieTitleFromPath on windows
            // uses SourceStemName with a Media item fallback for an empty stem
            std::wstring path = Utf8ToWide(argv[++i]);
            std::wstring stem = SourceStemName(path);
            std::wcout << (stem.empty() ? L"Media item" : stem) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-media-display-title" && i + 3 < argc) {
            bool showFileNames = std::string(argv[++i]) == "1";
            std::wstring titleOverride = Utf8ToWide(argv[++i]);
            std::wstring path = Utf8ToWide(argv[++i]);
            std::wcout << BuildDisplayTitleForMediaFile(showFileNames, titleOverride, path) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-rewrite-hls-manifest" && i + 2 < argc) {
            std::wstring baseUrl = Utf8ToWide(argv[++i]);
            std::wstring textFilePath = Utf8ToWide(argv[++i]);
            std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
            std::ostringstream ss;
            ss << file.rdbuf();
            std::cout << RewriteHlsManifestUrisToAbsolute(baseUrl, ss.str()) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-should-start-headless" && i + 2 < argc) {
            bool explicitFlag = std::string(argv[++i]) == "1";
            bool hasSources = std::string(argv[++i]) == "1";
            std::wcout << (ShouldStartHeadless(explicitFlag, hasSources) ? L"1" : L"0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-should-close-now" && i + 2 < argc) {
            bool isRunning = std::string(argv[++i]) == "1";
            bool isBusy = std::string(argv[++i]) == "1";
            std::wcout << (ShouldCloseNow(isRunning, isBusy) ? L"1" : L"0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-should-dump-dialog-geometry" && i + 1 < argc) {
            bool flagEnabled = std::string(argv[++i]) == "1";
            std::cout << (ShouldDumpDialogGeometry(flagEnabled) ? "1" : "0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-should-treat-server-unhealthy" && i + 3 < argc) {
            bool isRunning = std::string(argv[++i]) == "1";
            bool isHealthy = std::string(argv[++i]) == "1";
            int consecutiveUnhealthyPolls = std::atoi(argv[++i]);
            std::cout << (ShouldTreatServerAsUnhealthy(isRunning, isHealthy, consecutiveUnhealthyPolls) ? "1" : "0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-is-plausible-copydata-size" && i + 1 < argc) {
            unsigned long cbData = std::strtoul(argv[++i], nullptr, 10);
            std::wcout << (IsPlausibleWideStringCopyDataSize(cbData) ? L"1" : L"0") << std::endl;
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-media-database-id-overflow-guard" && i + 1 < argc) {
            MediaDatabase db;
            db.Load(Utf8ToWide(argv[++i]));
            db.BeginScanPass();
            int idA = db.GetOrCreateStableId(L"new-key-a");
            int idB = db.GetOrCreateStableId(L"new-key-b");
            std::wcout << idA << std::endl;
            std::wcout << idB << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-clamp-browse-requested-count" && i + 2 < argc) {
            int requestedCount = std::atoi(argv[++i]);
            int available = std::atoi(argv[++i]);
            std::cout << ClampBrowseRequestedCount(requestedCount, available) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-should-drop-link-local-endpoint" && i + 2 < argc) {
            bool candidateIsLinkLocal = std::string(argv[++i]) == "1";
            bool anyNonLinkLocalExists = std::string(argv[++i]) == "1";
            std::cout << (ShouldDropLinkLocalEndpoint(candidateIsLinkLocal, anyNonLinkLocalExists) ? "1" : "0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-should-evict-before-cache-insert" && i + 2 < argc) {
            size_t currentSize = static_cast<size_t>(std::atoll(argv[++i]));
            size_t capacity = static_cast<size_t>(std::atoll(argv[++i]));
            std::cout << (ShouldEvictBeforeCacheInsert(currentSize, capacity) ? "1" : "0") << std::endl;
            exitCode = 0; return true;
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
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-is-supported-source-path" && i + 1 < argc) {
            std::wcout << (IsSupportedLocalMediaOrPlaylistPath(Utf8ToWide(argv[++i])) ? L"1" : L"0") << std::endl;
            exitCode = 0; return true;
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
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-remote-probe-cache-lifecycle" && i + 1 < argc) {
            std::wstring probeUrl = Utf8ToWide(argv[++i]);
            long before = GetRemoteProbeRecomputeCountForTest();
            ProbeRemoteContentLength(probeUrl);
            long afterFirst = GetRemoteProbeRecomputeCountForTest();
            ProbeRemoteContentLength(probeUrl);
            long afterSecond = GetRemoteProbeRecomputeCountForTest();
            std::cout << "before=" << before << std::endl;
            std::cout << "after-first-probe=" << afterFirst << std::endl;
            std::cout << "after-second-probe=" << afterSecond << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-notify-pool-worker-count") {
            std::cout << kMaxUpnpNotifyWorkers << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-max-client-threads") {
            std::cout << kMaxClientThreads << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-dlna-server-header") {
            std::cout << GetDlnaServerHeader() << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-ssdp-response-delay-bound" && i + 1 < argc) {
            int mx = std::atoi(argv[++i]);
            std::cout << ComputeMaxDelayMilliseconds(mx) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-ssdp-search-response") {
            SsdpSearchResponseFields fields;
            fields.date = "Sat, 20 Sep 2026 00:00:00 GMT";
            fields.serverHeader = GetDlnaServerHeader();
            fields.locationUrl = "http://192.0.2.10:8200/description.xml";
            fields.st = "urn:schemas-upnp-org:device:MediaServer:1";
            fields.usn = "uuid:test::urn:schemas-upnp-org:device:MediaServer:1";
            fields.bootId = 1234;
            fields.configId = 1;
            std::cout << BuildSearchResponseMessage(fields);
            exitCode = 0; return true;
        }
        else if (arg == "--print-ssdp-search-response-send-count") {
            std::cout << kSearchResponseSendCount << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-should-allow-source-drop" && i + 1 < argc) {
            // ShouldAllowSourceDrop now lives in source drop policy h
            // no ole drop target exists on posix today
            // this only regression tests the pure boolean rule itself
            bool busyOrRunning = std::string(argv[++i]) == "1";
            std::cout << (ShouldAllowSourceDrop(busyOrRunning) ? "1" : "0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-outbound-interface-sockopt-name" && i + 1 < argc) {
            // mirrors the branch inside SetOutboundInterface in posix_ssdp cpp
            // prints the sockopt name that branch would choose for the given mode
            // exists so a change to that branch has a fast deterministic test
            // without needing a live multicast capable network in ci
            bool multicast = std::string(argv[++i]) == "1";
            std::cout << (multicast ? "IP_MULTICAST_IF" : "IP_UNICAST_IF") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-should-use-unlisted-interface" && i + 2 < argc) {
            bool isVirtual = std::string(argv[++i]) == "1";
            bool hasGateway = std::string(argv[++i]) == "1";
            std::wcout << (ShouldUseUnlistedInterface(isVirtual, hasGateway) ? L"1" : L"0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-transmitfile-chunk-plan" && i + 1 < argc) {
            // ComputeTransmitFileChunkSizes has no windows types
            // exercised here on posix purely for cross platform coverage
            // posix httpserver cpp itself calls sendfile not TransmitFile
            // see TrySendFile in posix httpserver cpp for the actual posix transfer path
            long long totalBytes = std::atoll(argv[++i]);
            for (long long chunkSize : ComputeTransmitFileChunkSizes(totalBytes)) {
                std::cout << chunkSize << std::endl;
            }
            exitCode = 0; return true;
        }
        else if (arg == "--print-network-endpoint-count" && i + 1 < argc) {
            int testPort = 0;
            if (!TryParsePortStrict(argv[++i], testPort)) testPort = 8200;
            std::vector<NetworkEndpoint> endpoints;
            EnumerateNetworkEndpoints(testPort, L"", endpoints);
            std::cout << endpoints.size() << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-sockaddr-length-safety" && i + 2 < argc) {
            // IsSockaddrLengthSafeToCopy has no windows types
            // lives in netutils h shared by both platforms already
            int reportedLength = std::atoi(argv[++i]);
            int destinationCapacity = std::atoi(argv[++i]);
            std::cout << (IsSockaddrLengthSafeToCopy(reportedLength, static_cast<size_t>(destinationCapacity)) ? "1" : "0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-tray-notify-decode" && i + 2 < argc) {
            unsigned long rawLParam = static_cast<unsigned long>(std::strtoul(argv[++i], nullptr, 0));
            unsigned short expectedIconId = static_cast<unsigned short>(std::atoi(argv[++i]));
            switch (DecodeTrayNotifyEvent(rawLParam, expectedIconId)) {
            case TrayNotifyAction::Activate: std::cout << "activate" << std::endl; break;
            case TrayNotifyAction::ShowMenu: std::cout << "showmenu" << std::endl; break;
            case TrayNotifyAction::None: std::cout << "none" << std::endl; break;
            }
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-media-item-stays-resolvable-during-rescan") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            int targetId = -1;
            for (const auto& item : AppMedia.GetDescendants(0)) {
                if (!item.isFolder) { targetId = item.id; break; }
            }
            std::cout << "start-ok=" << (startOk ? "1" : "0") << std::endl;
            std::cout << "target-id=" << targetId << std::endl;
            if (targetId == -1) {
                DLNAServer.Stop();
                exitCode = 1; return true;
            }
            std::atomic<bool> rescanDone(false);
            std::thread rescanThread([&rescanDone]() {
                DLNAServer.Rescan();
                rescanDone.store(true);
            });
            bool everMissing = false;
            while (!rescanDone.load()) {
                if (AppMedia.GetItem(targetId).id == -1) {
                    everMissing = true;
                }
            }
            if (AppMedia.GetItem(targetId).id == -1) {
                everMissing = true;
            }
            rescanThread.join();
            std::cout << "ever-missing=" << (everMissing ? "1" : "0") << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-server-health-detects-http-death") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::cout << "start-ok=" << (startOk ? "1" : "0") << std::endl;
            std::cout << "healthy-before=" << (DLNAServer.IsHealthy() ? "1" : "0") << std::endl;
            HttpServer::Get().Stop();
            std::cout << "running-after-http-death=" << (DLNAServer.IsRunning() ? "1" : "0") << std::endl;
            std::cout << "healthy-after-http-death=" << (DLNAServer.IsHealthy() ? "1" : "0") << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-modal-stack-lifecycle") {
            ModalStack<int> stack;
            std::cout << "empty=" << (stack.Empty() ? 1 : 0) << std::endl;
            stack.Push(0);                       // HandleT{} is ignored
            std::cout << "after-null-push-depth=" << stack.Depth() << std::endl;
            stack.Push(1);
            stack.Push(2);
            stack.Push(3);
            std::cout << "top=" << stack.Top() << " depth=" << stack.Depth() << std::endl;
            stack.Push(1);                       // re-push moves to top
            std::cout << "repush-top=" << stack.Top() << " depth=" << stack.Depth() << std::endl;
            stack.Remove(1);
            std::cout << "after-remove-top=" << stack.Top() << " depth=" << stack.Depth() << std::endl;
            stack.Remove(99);                    // absent handle is a no-op
            std::cout << "remove-absent-depth=" << stack.Depth() << std::endl;
            stack.Clear();
            std::cout << "cleared-top=" << stack.Top() << " empty=" << (stack.Empty() ? 1 : 0) << std::endl;
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-config-path") {
            std::wcout << AppConfig.GetConfigPath() << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-default-playlist-path") {
            // mirrors the windows F-02 regression hook in main cpp
            // confirms GetDefaultPlaylistPath still resolves next to the config file
            std::wcout << AppConfig.GetDefaultPlaylistPath() << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-ssdp-alive-interval-bounds") {
            // Regression test for the Phase 2 timing change. Samples the
            // interval generator many times and asserts every sample
            // falls inside [4min, 6min] and never inside the previous
            // [12min, 14.5min] window, catching an accidental partial
            // revert.
            unsigned int minSeen = UINT32_MAX;
            unsigned int maxSeen = 0;
            for (int i = 0; i < 2000; ++i) {
                unsigned int sample = ComputeSsdpNextAliveIntervalMilliseconds();
                if (sample < minSeen) minSeen = sample;
                if (sample > maxSeen) maxSeen = sample;
            }
            std::cout << "min-ms=" << minSeen << std::endl;
            std::cout << "max-ms=" << maxSeen << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-source-scan-pool-worker-count") {
            std::cout << SourceScanPool::Get().WorkerCount() << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-icon-cache-lifecycle") {
            std::string bytesFirst;
            std::string bytesSecond;
            long before = GetIconLoadRecomputeCountForTest();
            bool okFirst = LoadServerIconPngForTest("server_icon_48.png", bytesFirst);
            long afterFirst = GetIconLoadRecomputeCountForTest();
            bool okSecond = LoadServerIconPngForTest("server_icon_48.png", bytesSecond);
            long afterSecond = GetIconLoadRecomputeCountForTest();
            std::cout << "ok-first=" << (okFirst ? 1 : 0) << std::endl;
            std::cout << "ok-second=" << (okSecond ? 1 : 0) << std::endl;
            std::cout << "before=" << before << std::endl;
            std::cout << "after-first-load=" << afterFirst << std::endl;
            std::cout << "after-second-load=" << afterSecond << std::endl;
            std::cout << "bytes-equal=" << (bytesFirst == bytesSecond ? 1 : 0) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-resolve-bundled-resource" && i + 1 < argc) {
            std::cout << ResolveBundledResourcePath(argv[++i]) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-media-source-file-extensions") {
            for (const auto& ext : GetMediaSourceFileExtensions()) {
                std::wcout << ext << std::endl;
            }
            exitCode = 0; return true;
        }
        else if (arg == "--print-network-interface-allow-list-accessor") {
            // regression hook for the config accessor change
            // writes a value through Mutate then reads it back through
            // the new shared lock protected accessor
            AppConfig.Mutate([](Config& cfg) {
                cfg.networkInterfaceAllowList = L"eth0,wlan0";
            });
            std::wcout << AppConfig.GetNetworkInterfaceAllowList() << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-media-format-lookup" && i + 1 < argc) {
            // regression hook for the extension hash lookup change
            // prints the resolved media format fields on three lines or
            // the single line "no-match" when no format matches
            std::wstring extArg = Utf8ToWide(argv[++i]);
            MediaFormatInfo info;
            if (!GetMediaFormatForExtension(extArg, info)) {
                std::wcout << L"no-match" << std::endl;
            } else {
                std::wcout << info.mimeType << std::endl;
                std::wcout << info.upnpClass << std::endl;
                std::wcout << Utf8ToWide(info.dlnaProfile) << std::endl;
            }
            exitCode = 0; return true;
        }
        else if (arg == "--print-media-sources") {
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.mediaSources) {
                std::wcout << src.path << std::endl;
            }
            exitCode = 0; return true;
        }
        else if (arg == "--print-effective-media-sources") {
            // Reflect the RUNNING instance's effectiveMediaSources when one
            // is up (a follow-up print hook after a relaunch), otherwise this
            // process's own state. First see whether another instance holds
            // the single-instance lock; if so, ask it directly over the IPC
            // socket. When we are the only instance, just print our own
            // config as this hook has always done.
            if (!SingleInstance::TryAcquireLock()) {
                std::string resp;
                if (SingleInstance::SendEffectiveSourcesRequest(&resp)) {
                    std::cout << resp;
                    exitCode = 0; return true;
                }
            } else {
                SingleInstance::ReleaseLock();
            }
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.effectiveMediaSources) {
                std::wcout << src.path << std::endl;
            }
            exitCode = 0; return true;
        }
        else if (arg == "--print-clear-override-then-effective") {
            AppConfig.ClearRuntimeSourceOverride();
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.effectiveMediaSources) {
                std::wcout << src.path << std::endl;
            }
            exitCode = 0; return true;
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
                exitCode = 1; return true;
            }
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::wcout << L"--override-active--" << std::endl;
            for (auto& s : AppConfig.Snapshot().effectiveMediaSources) std::wcout << s.path << std::endl;

            DLNAServer.Stop();
            std::wcout << L"--after-stop--" << std::endl;
            for (auto& s : AppConfig.Snapshot().effectiveMediaSources) std::wcout << s.path << std::endl;
            exitCode = 0; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-concurrent-start-start-safety") {
            std::wstring reasonA;
            std::wstring reasonB;
            bool startOkA = false;
            bool startOkB = false;
            std::thread threadA([&]() { startOkA = DLNAServer.Start(reasonA); });
            std::thread threadB([&]() { startOkB = DLNAServer.Start(reasonB); });
            threadA.join();
            threadB.join();
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const bool exactlyOneSucceeded = (startOkA != startOkB) || (startOkA && startOkB);
            std::wcout << L"a-ok=" << (startOkA ? L"1" : L"0") << std::endl;
            std::wcout << L"b-ok=" << (startOkB ? L"1" : L"0") << std::endl;
            std::wcout << L"a-reason=" << reasonA << std::endl;
            std::wcout << L"b-reason=" << reasonB << std::endl;
            std::wcout << L"is-running=" << (DLNAServer.IsRunning() ? L"1" : L"0") << std::endl;
            (void)exactlyOneSucceeded;
            DLNAServer.Stop();
            std::wcout << L"after-stop-running=" << (DLNAServer.IsRunning() ? L"1" : L"0") << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-ssdp-concurrent-start-stop-safety") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            std::cout << "start-ok=" << (startOk ? "1" : "0") << std::endl;
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::atomic<int> rejectedCount(0);
            auto restartOnce = [&]() {
                SSDP::Get().Stop();
                const ConfigSnapshot cfg = AppConfig.Snapshot();
                const bool ok = SSDP::Get().Start(DLNAServer.GetEndpoints(), cfg.port, cfg.serverName, cfg.deviceUUID);
                if (!ok) rejectedCount.fetch_add(1);
            };
            std::thread threadA(restartOnce);
            std::thread threadB([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                restartOnce();
            });
            threadA.join();
            threadB.join();
            std::cout << "rejected-count=" << rejectedCount.load() << std::endl;
            std::cout << "final-running=" << (DLNAServer.IsRunning() ? "1" : "0") << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-httpserver-concurrent-start-stop-safety") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            std::cout << "start-ok=" << (startOk ? "1" : "0") << std::endl;
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const ConfigSnapshot cfg = AppConfig.Snapshot();
            std::atomic<int> rejectedCount(0);
            auto cycleOnce = [&]() {
                HttpServer::Get().Stop();
                const bool ok = HttpServer::Get().Start(cfg.port);
                if (!ok) rejectedCount.fetch_add(1);
            };
            std::thread threadA(cycleOnce);
            std::thread threadB([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                cycleOnce();
            });
            threadA.join();
            threadB.join();
            HttpServer::Get().Start(cfg.port);
            std::cout << "rejected-count=" << rejectedCount.load() << std::endl;
            std::cout << "is-healthy=" << (HttpServer::Get().IsHealthy() ? "1" : "0") << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-ssdp-bootid-persistence") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            std::cout << "start-ok=" << (startOk ? "1" : "0") << std::endl;
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const unsigned int firstBootId = SSDP::Get().GetBootIdForTest();
            DLNAServer.Stop();
            bool startOk2 = DLNAServer.Start(reason);
            std::cout << "start-ok2=" << (startOk2 ? "1" : "0") << std::endl;
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const unsigned int secondBootId = SSDP::Get().GetBootIdForTest();
            std::cout << "first=" << firstBootId << " second=" << secondBootId << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-network-change-restart-coalescing") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            std::cout << "start-ok=" << (startOk ? "1" : "0") << std::endl;
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::thread threadA([&]() { DLNAServer.RestartSsdpForNetworkChange(); });
            std::thread threadB([&]() { DLNAServer.RestartSsdpForNetworkChange(); });
            threadA.join();
            threadB.join();
            std::cout << "is-running=" << (DLNAServer.IsRunning() ? "1" : "0") << std::endl;
            std::cout << "is-healthy=" << (DLNAServer.IsHealthy() ? "1" : "0") << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-network-change-restart-noop-detection") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            LogSnapshot before = GetSystemLogSince(0);
            DLNAServer.RestartSsdpForNetworkChange();
            std::this_thread::sleep_for(std::chrono::milliseconds(2100));
            DLNAServer.RestartSsdpForNetworkChange();
            LogSnapshot after = GetSystemLogSince(before.latestSequence);
            const bool sawSkip = after.text.find(L"SSDP restart skipped") != std::wstring::npos ||
                                 after.text.find(L"Endpoint set unchanged") != std::wstring::npos;
            const bool sawJoin = after.text.find(L"multicast join ok") != std::wstring::npos;
            std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
            std::wcout << L"saw-skip-message=" << (sawSkip ? L"1" : L"0") << std::endl;
            std::wcout << L"saw-fresh-multicast-join=" << (sawJoin ? L"1" : L"0") << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-ssdp-stop-flushes-due-response") {
            AppConfig.debugLog = true;
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            LogSnapshot before = GetSystemLogSince(0);
            SSDP::Get().QueueTestResponseDueNow();
            SSDP::Get().Stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            LogSnapshot after = GetSystemLogSince(before.latestSequence);
            const bool sawResponseSent = after.text.find(L"SSDP response sent") != std::wstring::npos;
            std::cout << "start-ok=" << (startOk ? "1" : "0") << std::endl;
            std::cout << "ran=" << 1 << std::endl;
            std::cout << "saw-response-sent=" << (sawResponseSent ? "1" : "0") << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-ssdp-start-latency") {
            // regression hook for the ssdp initial burst thread fix
            // measures how long Start takes to return with a real
            // source configured before this fix this included the
            // full jitter delay plus three synchronous notify rounds
            // after this fix it should only include socket and
            // endpoint setup work see posix_ssdp cpp SSDP Start
            const auto startedAt = std::chrono::steady_clock::now();
            std::wstring reason;
            const bool startOk = DLNAServer.Start(reason);
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startedAt).count();
            std::wcout << L"start-ok=" << (startOk ? L"1" : L"0") << std::endl;
            std::wcout << L"elapsed-ms=" << elapsedMs << std::endl;
            while (DLNAServer.IsInitialScanInProgress()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-single-file-source-scan") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            if (!startOk) {
                std::wcerr << L"start failed: " << reason << std::endl;
                exitCode = 1; return true;
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
            exitCode = 0; return true;
        }
        else if (arg == "--print-search-cache-cleared-on-rescan") {
            std::wstring reason;
            bool startOk = DLNAServer.Start(reason);
            if (!startOk) {
                std::wcerr << L"start failed: " << reason << std::endl;
                exitCode = 1; return true;
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
            std::wcout << L"before-rescan-total-items=" << AppContent.GetSearchCacheTotalItemsForTest() << std::endl;
            DLNAServer.Rescan();
            std::wcout << L"after-rescan-cache-size=" << AppContent.GetSearchCacheSizeForTest() << std::endl;
            std::wcout << L"after-rescan-total-items=" << AppContent.GetSearchCacheTotalItemsForTest() << std::endl;
            DLNAServer.Stop();
            exitCode = 0; return true;
        }
        else if (arg == "--print-thread-guard-behavior") {
            RunGuarded(L"test-thread", []() {
                throw std::runtime_error("synthetic-test-exception");
            });
            std::cout << "guard-caught-exception=1" << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-thread-pool-exception-resilience") {
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
            std::cout << "pool-survived-exception=" << (ok ? 1 : 0) << std::endl;
            exitCode = 0; return true;
        }
        else if (arg == "--print-select-best-endpoint-scope-match") {
            // Regression test for F-DISCOVERY-01. Builds two synthetic
            // link-local IPv6 endpoints on different interfaces (index 2
            // and index 5) and a synthetic remote address scoped to
            // interface 5, then asserts SelectBestEndpoint picks the
            // interface-5 endpoint rather than simply whichever endpoint
            // happens to be first in the vector.
            auto makeLinkLocalEndpoint = [](unsigned long ifIndex, unsigned char lastByte) {
                NetworkEndpoint ep{};
                ep.family = AF_INET6;
                ep.interfaceIndex = ifIndex;
                ep.prefixLength = 64;
                ep.isLinkLocal = true;
                sockaddr_in6 addr{};
                addr.sin6_family = AF_INET6;
                addr.sin6_addr.s6_addr[0] = 0xfe;
                addr.sin6_addr.s6_addr[1] = 0x80;
                addr.sin6_addr.s6_addr[15] = lastByte;
                addr.sin6_scope_id = ifIndex;
                std::memcpy(&ep.sockaddr, &addr, sizeof(addr));
                ep.sockaddrLen = sizeof(addr);
                return ep;
            };
            std::vector<NetworkEndpoint> endpoints;
            endpoints.push_back(makeLinkLocalEndpoint(2, 0x02));
            endpoints.push_back(makeLinkLocalEndpoint(5, 0x05));

            sockaddr_in6 remote{};
            remote.sin6_family = AF_INET6;
            remote.sin6_addr.s6_addr[0] = 0xfe;
            remote.sin6_addr.s6_addr[1] = 0x80;
            remote.sin6_addr.s6_addr[15] = 0x99;
            remote.sin6_scope_id = 5;

            const NetworkEndpoint* picked = SelectBestEndpoint(endpoints, reinterpret_cast<SOCKADDR*>(&remote));
            std::cout << "picked-interface-index=" << (picked ? picked->interfaceIndex : 0) << std::endl;
            exitCode = 0; return true;
        }

    return false;
}
