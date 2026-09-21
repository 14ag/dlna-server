#include "config.h"
#include "cli_print_hooks_posix.h"
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

namespace {
std::atomic<bool> g_stop(false);

void HandleSignal(int) {
    g_stop = true;
}

void OnSingleInstanceCommand(const std::string& cmd) {
    if (cmd == "kill") {
        g_stop = true;
    } else if (cmd == "show") {
        LogPrint(L"Received single-instance show request");
    }
}

std::string EffectiveSourcesResponse() {
    std::string out;
    auto snap = AppConfig.Snapshot();
    for (const auto& src : snap.effectiveMediaSources) {
        out += WideToUtf8(src.path);
        out += "\n";
    }
    return out;
}
}

int main(int argc, char** argv) {
    AppConfig.Load();
    {
        int printExitCode = 0;
        if (TryRunPrintHook(argc, argv, printExitCode)) {
            return printExitCode;
        }
    }
    std::vector<std::wstring> runtimeSources;
    bool wroteConfigOverride = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            int port = 0;
            if (!TryParsePortStrict(argv[++i], port)) {
                PrintUsage(argv[0]);
                return 2;
            }
            AppConfig.Mutate([&](Config& cfg) { cfg.port = port; });
            wroteConfigOverride = true;
        }
        else if (arg == "--name" && i + 1 < argc) {
            AppConfig.Mutate([&](Config& cfg) { cfg.serverName = Utf8ToWide(argv[++i]); });
            wroteConfigOverride = true;
        }
        else if (arg == "--uuid" && i + 1 < argc) {
            AppConfig.Mutate([&](Config& cfg) { cfg.deviceUUID = Utf8ToWide(argv[++i]); });
            wroteConfigOverride = true;
        }
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
        else if (arg == "--no-debug") {
            // Override a DebugLog=1 read from the config file so daemonize
            // behavior is deterministic for scripted/test launches. Unlike
            // --port/--name/--uuid it deliberately does NOT set
            // wroteConfigOverride, so it never rewrites the user's config.
            AppConfig.debugLog = false;
        }
        else if (arg == "--debug") {
            AppConfig.debugLog = true;
            wroteConfigOverride = true;
        }
        else if (arg == "--help") {
            PrintUsage(argv[0]);
            std::cerr.flush();
            return 0;
        }
        else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            PrintUsage(argv[0]);
            return 2;
        }
        else runtimeSources.push_back(Utf8ToWide(arg));
    }
    if (wroteConfigOverride) {
        AppConfig.Save();
    }
    if (!runtimeSources.empty()) {
        std::vector<MediaSource> overrideSources;
        for (const auto& src : runtimeSources) {
            overrideSources.push_back({src});
        }
        AppConfig.SetRuntimeSourceOverride(overrideSources);
    }
    if (AppConfig.mediaSources.empty() && !AppConfig.defaultPlaylistEnabled && runtimeSources.empty()) {
        std::cerr << "no sources found, please add a source or pass one with the --source flag" << std::endl;
        return 1;
    }

    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    // console echo is only ever turned on for the real foreground debug
    // session every --print-* early return above this line already exited
    // before reaching here so a test flag combined with --debug never
    // gets echo turned on see IsTestOnlyFlag in cli_flags h task 8
    bool sawTestOnlyFlag = false;
    for (int i = 1; i < argc; ++i) {
        if (IsTestOnlyFlag(std::string(argv[i]))) { sawTestOnlyFlag = true; break; }
    }
    if (AppConfig.debugLog && !sawTestOnlyFlag) {
        SetConsoleEchoEnabled(true);
    }

    // Single-instance lock: if another instance is already running,
    // try to show its window and exit.
    if (!SingleInstance::TryAcquireLock()) {
        // a second launch always supersedes the first per the posix non
        // gui restart on relaunch requirement never asks the first
        // instance to just show itself that behavior belongs to the gui
        // build only see OnSingleInstanceCommand in gtk4 gui main cpp
        if (!SingleInstance::KillExistingAndReacquire()) {
            std::cerr << "Another instance of dlna-server is already running "
                         "and did not exit in time; giving up." << std::endl;
            return 1;
        }
        // fall through to the normal startup path below using THIS
        // process's own already parsed args and overrides
    }

    // does not use DetachToBackgroundOrPrintReady from posix daemonize h
    // that helper signals ready immediately after detaching this path must
    // wait for DLNAServer Start to actually succeed first per the posix
    // non gui requirement that server is up means the upnp server is up
    int readyPipe[2] = { -1, -1 };
    bool isDetachedChild = false;
    if (ShouldDetachToBackground(AppConfig.debugLog, false)) {
        if (pipe(readyPipe) == 0) {
            pid_t child = fork();
            if (child > 0) {
                close(readyPipe[1]);
                char ok = 0;
                ssize_t readCount = read(readyPipe[0], &ok, 1);
                close(readyPipe[0]);
                if (readCount == 1 && ok == 1) {
                    std::cout << "server is up" << std::endl;
                    return 0;
                }
                std::cerr << "dlna-server: server failed to start" << std::endl;
                return 1;
            }
            if (child == 0) {
                close(readyPipe[0]);
                setsid();
                int devNull = open("/dev/null", O_RDWR);
                if (devNull >= 0) {
                    dup2(devNull, STDIN_FILENO);
                    dup2(devNull, STDOUT_FILENO);
                    dup2(devNull, STDERR_FILENO);
                    if (devNull > STDERR_FILENO) close(devNull);
                }
                isDetachedChild = true;
            }
        }
    }

    // Listen for IPC commands from short-lived --kill-server/--print-*
    // second instances (a second instance requests a graceful stop via
    // the "kill" command; see OnSingleInstanceCommand).
    //
    // IMPORTANT: StartListening() must happen AFTER the fork above, never
    // before it. g_listenerThread is a std::thread; forking a process that
    // already owns a running thread leaves the child with a joinable handle
    // to a thread that only exists in the parent. The parent would then
    // std::terminate when its global g_listenerThread destructor runs at
    // exit, and the daemon child's ReleaseLock() would terminate on
    // g_listenerThread.join(). Starting the listener in the child (and in
    // the non-detached foreground process) keeps exactly one live listener
    // thread per running instance.
    SingleInstance::StartListening(OnSingleInstanceCommand, EffectiveSourcesResponse);

    std::wstring outReason;
    const bool startOk = DLNAServer.Start(outReason);
    if (isDetachedChild) {
        char signal = startOk ? 1 : 0;
        ssize_t written = write(readyPipe[1], &signal, 1);
        (void)written;
        close(readyPipe[1]);
        if (!startOk) return 1;
    } else if (!startOk) {
        std::wcerr << L"Failed to start server: " << outReason << std::endl;
        return 1;
    }
    bool loggedUnhealthy = false;
    int consecutiveUnhealthyPolls = 0;
    while (!g_stop) {
        const bool runningNow = DLNAServer.IsRunning();
        const bool healthyNow = runningNow ? DLNAServer.IsHealthy() : true;
        consecutiveUnhealthyPolls = healthyNow ? 0 : (consecutiveUnhealthyPolls + 1);
        if (ShouldTreatServerAsUnhealthy(runningNow, healthyNow, consecutiveUnhealthyPolls)) {
            if (!loggedUnhealthy) {
                LogPrint(L"Server reported running but an internal worker thread has stopped unexpectedly stopping cleanly");
                loggedUnhealthy = true;
            }
            DLNAServer.Stop();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    DLNAServer.Stop();
    SingleInstance::ReleaseLock();
    return 0;
}
