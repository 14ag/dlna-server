#ifndef CONTEXT_MENU_INTEGRATION_H
#define CONTEXT_MENU_INTEGRATION_H

#include <string>
#include <vector>

// pure and cheap to call from a test does not touch the registry
// builds the command line stored in the verb s command default value
std::wstring BuildContextMenuCommandLine(const std::wstring& exePath);

// pure list of extensions the context menu is registered against
// reuses the same set the media scanner already treats as playable
// or as a playlist so the two lists cannot silently drift apart
std::vector<std::wstring> ContextMenuTargetExtensions();

// writes or removes the HKEY_CURRENT_USER Software Classes verb entries
// for every extension in ContextMenuTargetExtensions and for Directory
// returns false and fills message with a human readable reason on failure
bool EnsureContextMenuIntegration(bool enabled, std::wstring& message);

// true if the verb currently appears to be registered for at least the
// Directory entry used by the settings checkbox to reflect actual state
bool IsContextMenuIntegrationRegistered();

#endif // CONTEXT_MENU_INTEGRATION_H
