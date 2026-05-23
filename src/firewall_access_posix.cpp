#include "firewall_access.h"
#include "log.h"
#include "netutils.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const int kSsdpPort = 1900;

bool IsRoot() {
    return geteuid() == 0;
}

std::string ShellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

int RunCommand(const std::string& command) {
    int rc = std::system(command.c_str());
    if (rc == -1) {
        return 127;
    }
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    return 1;
}

bool CommandOk(const std::string& command) {
    return RunCommand(command + " >/dev/null 2>&1") == 0;
}

bool HasCommand(const char* command) {
    return CommandOk(std::string("command -v ") + command);
}

std::string ReadCommand(const std::string& command) {
    std::array<char, 256> buffer = {};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return output;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }
    pclose(pipe);
    return output;
}

bool IsSafeZoneName(const std::string& zone) {
    if (zone.empty()) {
        return false;
    }
    for (char ch : zone) {
        const bool ok = (ch >= 'a' && ch <= 'z') ||
                        (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') ||
                        ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string ActiveFirewalldZoneArg() {
    std::istringstream stream(ReadCommand("firewall-cmd --get-active-zones 2>/dev/null"));
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == ' ' || line[0] == '\t') {
            continue;
        }
        if (IsSafeZoneName(line)) {
            return "--zone=" + line + " ";
        }
    }
    return "";
}

std::string FirewalldQueryCommand(const std::string& zoneArg, int port, const char* protocol) {
    return "firewall-cmd " + zoneArg + "--query-port=" + std::to_string(port) + "/" + protocol;
}

std::string FirewalldAddCommand(const std::string& zoneArg, int port, const char* protocol, bool permanent) {
    return "firewall-cmd " + zoneArg + (permanent ? "--permanent " : "") +
           "--add-port=" + std::to_string(port) + "/" + protocol;
}

std::string FirewalldInstallCommand(const std::string& zoneArg, int port) {
    return FirewalldAddCommand(zoneArg, port, "tcp", false) + " && " +
           FirewalldAddCommand(zoneArg, kSsdpPort, "udp", false) + " && " +
           FirewalldAddCommand(zoneArg, port, "tcp", true) + " && " +
           FirewalldAddCommand(zoneArg, kSsdpPort, "udp", true);
}

std::string UfwInstallCommand(int port) {
    return "ufw allow " + std::to_string(port) + "/tcp && ufw allow 1900/udp";
}

bool RunPrivilegedCommand(const std::string& command, FirewallAccessMode mode, std::wstring& message) {
    if (IsRoot()) {
        if (RunCommand(command) == 0) {
            return true;
        }
        message = Utf8ToWide("Firewall command failed: " + command);
        return false;
    }

    if (mode == FirewallAccessMode::Interactive && HasCommand("pkexec")) {
        const std::string elevated = "pkexec /bin/sh -c " + ShellQuote(command);
        if (RunCommand(elevated) == 0) {
            return true;
        }
        message = L"Firewall authorization was cancelled or failed.";
        return false;
    }

    message = Utf8ToWide("Firewall rules missing. Run: sudo sh -c " + ShellQuote(command));
    return false;
}

bool EnsureFirewalld(int port, FirewallAccessMode mode, bool& handled, std::wstring& message) {
    handled = false;
    if (!HasCommand("firewall-cmd") || !CommandOk("firewall-cmd --state")) {
        return false;
    }
    handled = true;

    const std::string zoneArg = ActiveFirewalldZoneArg();
    if (CommandOk(FirewalldQueryCommand(zoneArg, port, "tcp")) &&
        CommandOk(FirewalldQueryCommand(zoneArg, kSsdpPort, "udp"))) {
        return true;
    }

    const std::string installCommand = FirewalldInstallCommand(zoneArg, port);
    if (!RunPrivilegedCommand(installCommand, mode, message)) {
        return false;
    }

    if (!CommandOk(FirewalldQueryCommand(zoneArg, port, "tcp")) ||
        !CommandOk(FirewalldQueryCommand(zoneArg, kSsdpPort, "udp"))) {
        message = L"firewalld rules were added but verification failed.";
        return false;
    }

    message = L"firewalld access configured.";
    LogPrint(L"%ls", message.c_str());
    return true;
}

bool EnsureUfw(int port, FirewallAccessMode mode, bool& handled, std::wstring& message) {
    handled = false;
    if (!HasCommand("ufw")) {
        return false;
    }
    handled = true;

    const std::string status = ReadCommand("ufw status 2>/dev/null");
    if (status.find("Status: inactive") != std::string::npos) {
        return true;
    }
    if (status.find(std::to_string(port) + "/tcp") != std::string::npos &&
        status.find(std::to_string(kSsdpPort) + "/udp") != std::string::npos) {
        return true;
    }

    const std::string installCommand = UfwInstallCommand(port);
    if (!RunPrivilegedCommand(installCommand, mode, message)) {
        return false;
    }

    message = L"ufw access configured.";
    LogPrint(L"%ls", message.c_str());
    return true;
}
}

std::wstring BuildFirewallAccessSummary(int port) {
    return L"dlna-server needs firewall access for TCP " + std::to_wstring(port) +
           L" and UDP 1900 for DLNA/UPnP discovery and playback.";
}

bool ConfigureFirewallAccessElevated(int port, std::wstring& message) {
    return EnsureFirewallAccess(port, FirewallAccessMode::NonInteractive, message);
}

bool EnsureFirewallAccess(int port, FirewallAccessMode mode, std::wstring& message) {
    message.clear();
    if (port <= 0 || port > 65535) {
        message = L"Firewall access failed: invalid TCP port.";
        return false;
    }

#ifdef __APPLE__
    return true;
#else
    bool handled = false;
    if (EnsureFirewalld(port, mode, handled, message)) {
        return true;
    }
    if (handled) {
        return false;
    }

    if (EnsureUfw(port, mode, handled, message)) {
        return true;
    }
    if (handled) {
        return false;
    }

    return true;
#endif
}
