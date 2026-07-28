#include "netchange_watch.h"
#include "network_change_debounce.h"
#include "log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <mutex>
#include <thread>
#include <atomic>

#pragma comment(lib, "iphlpapi.lib")

namespace {
HANDLE g_ifaceHandle = NULL;
HANDLE g_unicastHandle = NULL;
std::function<void()> g_onChange;
NetworkChangeDebouncer g_debounce(std::chrono::milliseconds(1500));
std::atomic<bool> g_deferredThreadRunning(false);
std::thread g_deferredThread;

void FireNow() {
    if (g_onChange) g_onChange();
}

void ScheduleDeferredFire() {
    bool expected = false;
    if (!g_deferredThreadRunning.compare_exchange_strong(expected, true)) return;
    if (g_deferredThread.joinable()) g_deferredThread.join();
    g_deferredThread = std::thread([]() {
        while (g_debounce.TrailingPending()) {
            auto deadline = g_debounce.TrailingDeadline();
            std::this_thread::sleep_until(deadline);
            if (std::chrono::steady_clock::now() >= deadline && g_debounce.TrailingPending()) {
                g_debounce.MarkTrailingHandled(std::chrono::steady_clock::now());
                FireNow();
                break;
            }
        }
        g_deferredThreadRunning.store(false);
    });
}

void HandleRawEvent() {
    auto now = std::chrono::steady_clock::now();
    if (g_debounce.OnChangeShouldActNow(now)) {
        FireNow();
    } else {
        ScheduleDeferredFire();
    }
}

void NETIOAPI_API_ IfaceCallback(PVOID, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
    HandleRawEvent();
}

void NETIOAPI_API_ UnicastCallback(PVOID, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE) {
    HandleRawEvent();
}
}

bool StartNetworkChangeWatch(std::function<void()> onChange) {
    g_onChange = std::move(onChange);
    DWORD ifaceResult = NotifyIpInterfaceChange(AF_UNSPEC, IfaceCallback, NULL, FALSE, &g_ifaceHandle);
    DWORD unicastResult = NotifyUnicastIpAddressChange(AF_UNSPEC, UnicastCallback, NULL, FALSE, &g_unicastHandle);
    if (ifaceResult != NO_ERROR) {
        LogPrint(L"NotifyIpInterfaceChange registration failed err=%lu", ifaceResult);
    }
    if (unicastResult != NO_ERROR) {
        LogPrint(L"NotifyUnicastIpAddressChange registration failed err=%lu", unicastResult);
    }
    return ifaceResult == NO_ERROR || unicastResult == NO_ERROR;
}

void StopNetworkChangeWatch() {
    if (g_ifaceHandle) {
        CancelMibChangeNotify2(g_ifaceHandle);
        g_ifaceHandle = NULL;
    }
    if (g_unicastHandle) {
        CancelMibChangeNotify2(g_unicastHandle);
        g_unicastHandle = NULL;
    }
    if (g_deferredThread.joinable()) g_deferredThread.join();
    g_onChange = nullptr;
}
