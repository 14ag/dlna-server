#include "firewall_access.h"
#include "log.h"

#include <windows.h>
#include <netfw.h>
#include <shellapi.h>
#include <oleauto.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace {
const wchar_t* kTcpRuleName = L"DLNA Server HTTP TCP";
const wchar_t* kUdpRuleName = L"DLNA Server SSDP UDP";
const wchar_t* kRuleGroup = L"DLNA Server";
const LONG kProfiles = NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_PUBLIC;
const LONG kProtocolTcp = 6;
const LONG kProtocolUdp = 17;
const int kSsdpPort = 1900;

struct ComInit {
    HRESULT hr;
    bool initialized;

    ComInit() : hr(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)), initialized(false) {
        initialized = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) {
            initialized = false;
            hr = S_OK;
        }
    }

    ~ComInit() {
        if (initialized) {
            CoUninitialize();
        }
    }
};

std::wstring FormatHresult(HRESULT hr) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

std::wstring GetModulePath() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return L"";
    }
    return path;
}

std::wstring ToLowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring BstrToWide(BSTR value) {
    if (!value) {
        return L"";
    }
    return std::wstring(value, SysStringLen(value));
}

bool IsElevated() {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool SetBstr(HRESULT hr, BSTR value) {
    SysFreeString(value);
    return SUCCEEDED(hr);
}

bool PutBstr(INetFwRule* rule, HRESULT (__stdcall INetFwRule::*setter)(BSTR), const std::wstring& value) {
    BSTR bstr = SysAllocString(value.c_str());
    HRESULT hr = (rule->*setter)(bstr);
    return SetBstr(hr, bstr);
}

HRESULT GetFirewallRules(INetFwRules** rules) {
    *rules = NULL;
    INetFwPolicy2* policy = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(NetFwPolicy2),
                                  NULL,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwPolicy2),
                                  reinterpret_cast<void**>(&policy));
    if (FAILED(hr)) {
        return hr;
    }

    hr = policy->get_Rules(rules);
    policy->Release();
    return hr;
}

bool StringEqualsNoCase(const std::wstring& left, const std::wstring& right) {
    return ToLowerWide(left) == ToLowerWide(right);
}

bool GetRuleString(INetFwRule* rule, HRESULT (__stdcall INetFwRule::*getter)(BSTR*), std::wstring& value) {
    BSTR bstr = NULL;
    HRESULT hr = (rule->*getter)(&bstr);
    if (FAILED(hr)) {
        value.clear();
        return false;
    }
    value = BstrToWide(bstr);
    SysFreeString(bstr);
    return true;
}

bool RuleMatchesAppProtocol(INetFwRule* rule, const std::wstring& exePath, LONG protocol) {
    NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_MAX;
    LONG ruleProtocol = 0;
    std::wstring applicationName;

    if (FAILED(rule->get_Direction(&direction)) || direction != NET_FW_RULE_DIR_IN) {
        return false;
    }
    if (FAILED(rule->get_Protocol(&ruleProtocol)) || ruleProtocol != protocol) {
        return false;
    }
    if (!GetRuleString(rule, &INetFwRule::get_ApplicationName, applicationName) ||
        !StringEqualsNoCase(applicationName, exePath)) {
        return false;
    }
    return true;
}

bool RuleMatchesAppInbound(INetFwRule* rule, const std::wstring& exePath) {
    NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_MAX;
    std::wstring applicationName;

    if (FAILED(rule->get_Direction(&direction)) || direction != NET_FW_RULE_DIR_IN) {
        return false;
    }
    return GetRuleString(rule, &INetFwRule::get_ApplicationName, applicationName) &&
           StringEqualsNoCase(applicationName, exePath);
}

bool RuleMatchesLocalPort(INetFwRule* rule, int port) {
    std::wstring localPorts;
    if (!GetRuleString(rule, &INetFwRule::get_LocalPorts, localPorts)) {
        return false;
    }
    return localPorts == std::to_wstring(port);
}

bool RuleAllowsAnyLocalPort(INetFwRule* rule) {
    std::wstring localPorts;
    if (!GetRuleString(rule, &INetFwRule::get_LocalPorts, localPorts)) {
        return true;
    }
    return localPorts.empty() || localPorts == L"*";
}

bool RuleHasCompleteAllowProperties(INetFwRule* rule) {
    VARIANT_BOOL enabled = VARIANT_FALSE;
    NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
    LONG profiles = 0;
    std::wstring remoteAddresses;

    if (FAILED(rule->get_Enabled(&enabled)) || enabled != VARIANT_TRUE) {
        return false;
    }
    if (FAILED(rule->get_Action(&action)) || action != NET_FW_ACTION_ALLOW) {
        return false;
    }
    if (FAILED(rule->get_Profiles(&profiles)) || (profiles & kProfiles) != kProfiles) {
        return false;
    }
    if (!GetRuleString(rule, &INetFwRule::get_RemoteAddresses, remoteAddresses)) {
        return false;
    }

    return StringEqualsNoCase(remoteAddresses, L"LocalSubnet");
}

bool RuleIsCompleteTcpAllow(INetFwRule* rule, const std::wstring& exePath) {
    return RuleMatchesAppProtocol(rule, exePath, kProtocolTcp) &&
           RuleAllowsAnyLocalPort(rule) &&
           RuleHasCompleteAllowProperties(rule);
}

bool RuleIsCompleteUdpAllow(INetFwRule* rule, const std::wstring& exePath) {
    return RuleMatchesAppProtocol(rule, exePath, kProtocolUdp) &&
           RuleMatchesLocalPort(rule, kSsdpPort) &&
           RuleHasCompleteAllowProperties(rule);
}

bool RuleIsMatchingBlock(INetFwRule* rule, const std::wstring& exePath) {
    NET_FW_ACTION action = NET_FW_ACTION_ALLOW;
    LONG protocol = 0;

    if (!RuleMatchesAppInbound(rule, exePath)) {
        return false;
    }
    if (FAILED(rule->get_Action(&action)) || action != NET_FW_ACTION_BLOCK) {
        return false;
    }
    if (FAILED(rule->get_Protocol(&protocol))) {
        return false;
    }
    return protocol == kProtocolTcp ||
           protocol == kProtocolUdp ||
           protocol == NET_FW_IP_PROTOCOL_ANY;
}

bool RuleIsOldPortScopedTcpAllow(INetFwRule* rule, const std::wstring& exePath) {
    NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
    std::wstring remoteAddresses;

    if (!RuleMatchesAppProtocol(rule, exePath, kProtocolTcp) ||
        RuleAllowsAnyLocalPort(rule)) {
        return false;
    }
    if (FAILED(rule->get_Action(&action)) || action != NET_FW_ACTION_ALLOW) {
        return false;
    }
    if (!GetRuleString(rule, &INetFwRule::get_RemoteAddresses, remoteAddresses)) {
        return false;
    }
    return StringEqualsNoCase(remoteAddresses, L"LocalSubnet");
}

void AddRemoval(std::vector<std::wstring>& removals, const std::wstring& name) {
    if (name.empty() || std::find(removals.begin(), removals.end(), name) != removals.end()) {
        return;
    }
    removals.push_back(name);
}

bool EvaluateFirewallRules(int port,
                           bool collectRemovals,
                           bool& hasTcpAllow,
                           bool& hasUdpAllow,
                           std::vector<std::wstring>& removals,
                           std::wstring& message) {
    hasTcpAllow = false;
    hasUdpAllow = false;
    removals.clear();
    message.clear();
    (void)port;

    ComInit com;
    if (FAILED(com.hr)) {
        message = L"Firewall access check failed: COM initialization failed (" + FormatHresult(com.hr) + L").";
        return false;
    }

    std::wstring exePath = GetModulePath();
    if (exePath.empty()) {
        message = L"Firewall access check failed: executable path unavailable.";
        return false;
    }

    INetFwRules* rules = NULL;
    HRESULT hr = GetFirewallRules(&rules);
    if (FAILED(hr) || !rules) {
        message = L"Firewall access check failed: INetFwPolicy2 unavailable (" + FormatHresult(hr) + L").";
        return false;
    }

    IUnknown* unknown = NULL;
    hr = rules->get__NewEnum(&unknown);
    if (FAILED(hr) || !unknown) {
        rules->Release();
        message = L"Firewall access check failed: rule enumeration unavailable (" + FormatHresult(hr) + L").";
        return false;
    }

    IEnumVARIANT* enumVariant = NULL;
    hr = unknown->QueryInterface(IID_IEnumVARIANT, reinterpret_cast<void**>(&enumVariant));
    unknown->Release();
    if (FAILED(hr) || !enumVariant) {
        rules->Release();
        message = L"Firewall access check failed: rule enumeration interface unavailable (" + FormatHresult(hr) + L").";
        return false;
    }

    VARIANT item;
    VariantInit(&item);
    ULONG fetched = 0;
    while (enumVariant->Next(1, &item, &fetched) == S_OK) {
        if (item.vt == VT_DISPATCH && item.pdispVal) {
            INetFwRule* rule = NULL;
            if (SUCCEEDED(item.pdispVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void**>(&rule))) && rule) {
                std::wstring name;
                std::wstring grouping;
                GetRuleString(rule, &INetFwRule::get_Name, name);
                GetRuleString(rule, &INetFwRule::get_Grouping, grouping);

                if (RuleIsCompleteTcpAllow(rule, exePath)) {
                    hasTcpAllow = true;
                }
                if (RuleIsCompleteUdpAllow(rule, exePath)) {
                    hasUdpAllow = true;
                }
                if (collectRemovals &&
                    (name == kTcpRuleName ||
                     name == kUdpRuleName ||
                     grouping == kRuleGroup ||
                     RuleIsMatchingBlock(rule, exePath) ||
                     RuleIsOldPortScopedTcpAllow(rule, exePath))) {
                    AddRemoval(removals, name);
                }
                rule->Release();
            }
        }
        VariantClear(&item);
        VariantInit(&item);
    }

    enumVariant->Release();
    rules->Release();
    return true;
}

bool RemoveNamedRules(INetFwRules* rules, const std::vector<std::wstring>& names, std::wstring& message) {
    for (const auto& name : names) {
        if (name.empty()) {
            continue;
        }
        BSTR bstr = SysAllocString(name.c_str());
        HRESULT hr = rules->Remove(bstr);
        SysFreeString(bstr);
        if (FAILED(hr)) {
            message = L"Firewall rule removal failed for " + name + L" (" + FormatHresult(hr) + L").";
            return false;
        }
    }
    return true;
}

bool AddRule(INetFwRules* rules, const wchar_t* name, LONG protocol, int port, bool limitPort, const std::wstring& exePath, std::wstring& message) {
    INetFwRule* rule = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(NetFwRule),
                                  NULL,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwRule),
                                  reinterpret_cast<void**>(&rule));
    if (FAILED(hr) || !rule) {
        message = L"Firewall rule creation failed (" + FormatHresult(hr) + L").";
        return false;
    }

    bool ok =
        PutBstr(rule, &INetFwRule::put_Name, name) &&
        PutBstr(rule, &INetFwRule::put_Description, L"Allow DLNA Server on local subnet for DLNA/UPnP discovery and playback.") &&
        PutBstr(rule, &INetFwRule::put_ApplicationName, exePath) &&
        PutBstr(rule, &INetFwRule::put_Grouping, kRuleGroup) &&
        PutBstr(rule, &INetFwRule::put_RemoteAddresses, L"LocalSubnet") &&
        SUCCEEDED(rule->put_Protocol(protocol)) &&
        SUCCEEDED(rule->put_Direction(NET_FW_RULE_DIR_IN)) &&
        SUCCEEDED(rule->put_Action(NET_FW_ACTION_ALLOW)) &&
        SUCCEEDED(rule->put_Profiles(kProfiles)) &&
        SUCCEEDED(rule->put_EdgeTraversal(VARIANT_FALSE)) &&
        SUCCEEDED(rule->put_Enabled(VARIANT_TRUE));

    if (ok && limitPort) {
        ok = PutBstr(rule, &INetFwRule::put_LocalPorts, std::to_wstring(port));
    }

    if (!ok) {
        rule->Release();
        message = L"Firewall rule property setup failed.";
        return false;
    }

    hr = rules->Add(rule);
    rule->Release();
    if (FAILED(hr)) {
        message = L"Firewall rule add failed for " + std::wstring(name) + L" (" + FormatHresult(hr) + L").";
        return false;
    }
    return true;
}

bool LaunchElevatedFirewallHelper(int port, std::wstring& message) {
    std::wstring exePath = GetModulePath();
    std::wstring args = L"--configure-firewall --port " + std::to_wstring(port);

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            message = L"Firewall access was not granted.";
        } else {
            message = L"Failed to open Windows elevation prompt for firewall access.";
        }
        return false;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);

    if (exitCode != 0) {
        message = L"Elevated firewall helper failed.";
        return false;
    }
    return true;
}

bool MessageIndicatesAccessDenied(const std::wstring& message) {
    return message.find(FormatHresult(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))) != std::wstring::npos;
}
}

std::wstring BuildFirewallAccessSummary(int port) {
    (void)port;
    return L"DLNA Server needs Windows Firewall access for this app on TCP and UDP 1900"
           L" on Domain, Private, and Public profiles, restricted to LocalSubnet.";
}

bool ConfigureFirewallAccessElevated(int port, std::wstring& message) {
    message.clear();
    if (port <= 0 || port > 65535) {
        message = L"Firewall access failed: invalid TCP port.";
        return false;
    }

    ComInit com;
    if (FAILED(com.hr)) {
        message = L"Firewall access failed: COM initialization failed (" + FormatHresult(com.hr) + L").";
        return false;
    }

    std::wstring exePath = GetModulePath();
    if (exePath.empty()) {
        message = L"Firewall access failed: executable path unavailable.";
        return false;
    }

    bool hasTcpAllow = false;
    bool hasUdpAllow = false;
    std::vector<std::wstring> removals;
    if (!EvaluateFirewallRules(port, true, hasTcpAllow, hasUdpAllow, removals, message)) {
        return false;
    }
    if (hasTcpAllow && hasUdpAllow) {
        message = L"Firewall access already configured.";
        return true;
    }

    INetFwRules* rules = NULL;
    HRESULT hr = GetFirewallRules(&rules);
    if (FAILED(hr) || !rules) {
        message = L"Firewall access failed: INetFwPolicy2 rules unavailable (" + FormatHresult(hr) + L").";
        return false;
    }

    bool ok = RemoveNamedRules(rules, removals, message) &&
              AddRule(rules, kTcpRuleName, kProtocolTcp, 0, false, exePath, message) &&
              AddRule(rules, kUdpRuleName, kProtocolUdp, kSsdpPort, true, exePath, message);
    rules->Release();
    if (!ok) {
        return false;
    }

    bool verifiedTcp = false;
    bool verifiedUdp = false;
    std::vector<std::wstring> unused;
    if (!EvaluateFirewallRules(port, false, verifiedTcp, verifiedUdp, unused, message)) {
        return false;
    }
    if (!verifiedTcp || !verifiedUdp) {
        message = L"Firewall access failed: rules were added but verification failed.";
        return false;
    }

    message = L"Firewall access configured.";
    LogPrint(L"%ls", message.c_str());
    return true;
}

bool EnsureFirewallAccess(int port, FirewallAccessMode mode, std::wstring& message) {
    message.clear();

    bool hasTcpAllow = false;
    bool hasUdpAllow = false;
    std::vector<std::wstring> removals;
    bool canReadRules = EvaluateFirewallRules(port, false, hasTcpAllow, hasUdpAllow, removals, message);
    bool readDenied = !canReadRules && MessageIndicatesAccessDenied(message);
    if (!canReadRules && !readDenied) {
        return false;
    }
    if (canReadRules && hasTcpAllow && hasUdpAllow) {
        return true;
    }

    if (mode == FirewallAccessMode::NonInteractive) {
        message = BuildFirewallAccessSummary(port) +
                  L" Run dlna-server elevated once with --configure-firewall --port " +
                  std::to_wstring(port) + L".";
        return false;
    }

    std::wstring prompt = BuildFirewallAccessSummary(port) + L"\n\nAllow access now?";
    int answer = MessageBoxW(NULL, prompt.c_str(), L"DLNA Server firewall access", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON1);
    if (answer != IDYES) {
        message = L"Firewall access was declined.";
        return false;
    }

    if (IsElevated()) {
        return ConfigureFirewallAccessElevated(port, message);
    }

    if (!LaunchElevatedFirewallHelper(port, message)) {
        return false;
    }

    hasTcpAllow = false;
    hasUdpAllow = false;
    if (!EvaluateFirewallRules(port, false, hasTcpAllow, hasUdpAllow, removals, message)) {
        if (MessageIndicatesAccessDenied(message)) {
            message = L"Firewall access configured.";
            return true;
        }
        return false;
    }
    if (!hasTcpAllow || !hasUdpAllow) {
        message = L"Firewall access was requested but required rules are still missing.";
        return false;
    }

    message = L"Firewall access configured.";
    return true;
}
