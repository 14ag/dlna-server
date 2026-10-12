# dlna-server documentation

dlna-server shares media from your computer to DLNA and UPnP players. It runs on Windows and Linux.

## Words used in these pages

| Word | Meaning |
|---|---|
| **app** | The program you run: the window, the tray icon, the settings dialogs, and the command-line tools. |
| **server** | The part of the app that does the sharing: device discovery (SSDP), the HTTP server, the media catalog, and events. The app contains the server. |
| **source** | A folder, file, playlist or network URL that you add to the app. |
| **player** | A TV, phone or other device that browses and plays your media. |
| **catalog** | The list of folders and items the server builds from your sources. |

## Pages

### For users

| Page | Read it to |
|---|---|
| [Getting Started](Getting-Started.md) | Install the app and share your first folder. |
| [Using the App](Using-the-App.md) | Learn the window, tray icon, dialogs and keyboard keys. |
| [Configuration](Configuration.md) | Find every setting, command-line option and file the app uses. |
| [Troubleshooting](Troubleshooting.md) | Fix a player that cannot find the server or cannot play a file. |
| [Platform Differences](Platform-Differences.md) | See where Windows and Linux behave differently. |
| [Known Issues](Known-Issues.md) | See the list of known issues and their workarounds. |
| [Specs](Specs.md) | Find the handwritten technical specifications for the app. |

### For developers

| Page | Read it to |
|---|---|
| [Development Guide](Development-Guide.md) | Build, package, release and extend the project. |
| [Architecture](Architecture.md) | Understand components, threads, locks and limits. |
| [Protocol Reference](Protocol-Reference.md) | Find every HTTP route, SOAP action and event the server supports. |
| [Discovery Deep Dive](Discovery.md) | Study how the server advertises itself and answers searches. |
| [Media Library Deep Dive](Media-Library.md) | Study scanning, object IDs, playlists, remote sources and rescans. |
| [Testing](Testing.md) | Learn how to test the app and its components. |
| [UI](UI.md) | Understand the user interface and how to fix launch and icon issues. |

## Scope

These pages describe the code on the `review` branch. If a page and the code disagree, the code is correct. Please open an issue.
