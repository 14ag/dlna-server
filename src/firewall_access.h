#ifndef FIREWALL_ACCESS_H
#define FIREWALL_ACCESS_H

#include <string>

enum class FirewallAccessMode {
    NonInteractive,
    Interactive
};

bool EnsureFirewallAccess(int port, FirewallAccessMode mode, std::wstring& message);
bool ConfigureFirewallAccessElevated(int port, std::wstring& message);
std::wstring BuildFirewallAccessSummary(int port);

#endif // FIREWALL_ACCESS_H
