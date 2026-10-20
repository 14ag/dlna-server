# WORKFLOW REPORT 20-10-26

Workflow: `.vscode/dlna-server-titlebar-menu-manpages-completion-localhost-fallback-workflow-20-10-26.md`

Baseline pytest: 468 passed / 11 failed / 116 deselected / 1 error (2 collection-error files excluded: tests/test_vlc_discovery_browse.py IndentationError line 401, tests/test_vlc_discovery_browse_selfcheck.py imports from it; both pre-existing)
Final pytest: 496 passed / 3 failed / 120 deselected / 1 error (same 2 collection-error files excluded)

Final-suite remaining failures are all pre-existing baseline failures, unchanged by this work:
- tests/test_hardening_fixes.py::HardeningFixSourceTests::test_ports_backups_ci_and_docs_are_hardened
- tests/test_review_6_6_26_sources.py::ReviewSixJuneSourceContracts::test_debug_logs_are_written_beside_config_on_all_platforms
- tests/test_wakelock_playlist_icon_sources.py::WakeLockPlaylistIconSourceTests::test_windows_ui_uses_busy_state_and_wake_lock
- ERROR tests/test_release_pipeline.py::test_release_notes_handle_gemini_response_without_jq

Improvements vs baseline: 7 test_status_text_shared.py failures fixed by T6 + rebuilt binary; test_add_media_source_if_new now passes. Deselected 116 -> 120 (+3 T4 posix_only, +1 T11 posix_only).

## Task status

| Task | Status | Tests | Notes |
|---|---|---|---|
| T1 titlebar menu | done | tests/test_windows_titlebar_context_menu.py (1 passed) | WM_CONTEXTMENU guards sourceHwnd == m_hListSources, else break to system menu |
| T2 gen_cli_docs.py | done | exercised via T4 suite | 8 files generated, verified |
| T3 CMake install | done | deb content check | appimage grep result: scripts/build_appimage.sh has no cmake --install/DESTDIR; it copies output bin+share into appdir, installed files land under usr/share inside the image, not visible to system man (expected, no code added). flatpak grep result: scripts/build_flatpak.sh uses flatpak-builder manifest; DLNA_FLATPAK_BUILD guard excludes docs install |
| T4 man and completion tests | done | tests/test_posix_man_and_completion.py | Windows: 4 passed 3 deselected (posix_only). WSL: 7 passed, no skips (mandoc/groff present) |
| T5 network_mode.h | done | via T11 tests | new pure header, added to WIN32 HEADERS after src/server_ui_state.h |
| T6 status text | done | tests/test_status_text_shared.py (7 passed after rebuild) | StatusTextFor 4th param localhostMode; both hook files read optional argv[i+1] == "1" |
| T7 server.h and ssdp.h | done | via T11 source-contract tests | include + 2 methods + 3 atomics; SSDP::ActiveEndpoints() added |
| T8 Start x2 | done | via T11 source-contract tests | both server.cpp and posix_server.cpp Start() localhost logic; SSDP start gated on Normal mode |
| T9 server_common.cpp | done | tests/test_ssdp_network_change_noop.py + tests/test_network_change_restart_serialization.py (4 passed) | two restart hooks still pass: yes |
| T10 hooks | done | smoke-tested both platforms | 3 hidden hooks print expected values; lifecycle hook start-ok=1 localhost=0 healthy=1 on machine with NICs |
| T11 localhost tests | done | tests/test_network_localhost_mode.py | Windows: 15 passed 1 deselected. WSL: 16 passed. netns test ran: unshare no-network start gives localhost=1 healthy=1 |
| T12 Win32 GUI | done | build gate | build.bat --x64 succeeded; PollNetworkMode + status band + WM_TIMER call |
| T13 GTK4 GUI | done | build gate | build.sh --install succeeded; RefreshStatus + OnPollTick localhost handling |
| T14 POSIX CLI | done | build gate | build.sh --install succeeded; ReportNetworkMode helper + loop call |
| T15 docs | done | full suite | files edited: docs/Architecture.md (localhost mode paragraph). docs/BUILD.md does not exist - BUILD items skipped. docs/ARCHITECTURE.md does not exist under that exact name; docs/Architecture.md (case differs) used |

## Manual checklist results (section 21)

1. Windows titlebar right-click: NOT PERFORMED by agent (needs interactive GUI session). Source-contract test passed; binary rebuilt with the fix. User must verify: system menu on title bar, "Remove selected source" on source list, toolbar right-click harmless.
2. POSIX GUI GTK title bar right-click: NOT PERFORMED. Not changed by this work (GtkWindowHandle custom title bar).
3. man pages after .deb install: VERIFIED. `man dlna-server` and `man dlna-server-gui` open (DLNA-SERVER(1) headers shown). dpkg shows all 8 files under /usr/share (man1 x2, bash-completion/completions x2, zsh vendor-completions x2, fish vendor_completions.d x2). lintian: not run (not verified available).
4. bash tab completion: files installed in correct dirs and bash-completion loader is sourced by ~/.bashrc (lines 112-115). Interactive Tab test NOT PERFORMED by agent; user must verify dlna-ser Tab, --he Tab, --source Tab in bash/zsh/fish.
5. GUI network-off localhost warning + status: NOT PERFORMED (needs interactive GUI + physical network change). Automated equivalent passed: unshare no-network start gives localhost=1 healthy=1 (T11).
6. Network restore restart prompt: NOT PERFORMED (interactive). Logic covered by DecideNetworkModeEvent truth-table tests and source-contract tests.
7. GUI start with no network: NOT PERFORMED (interactive). Automated equivalent passed (T11 netns test).
8. Windows sleep/wake without stop message: NOT PERFORMED (interactive).
9. CLI --debug no-network lines: NOT PERFORMED with real network flap. Lifecycle hook verified start/stop behavior on both platforms; debug text paths come from network_mode.h accessors covered by T11 tests.
10. Network flap dedup: NOT PERFORMED (interactive). Logic covered: DecideNetworkModeEvent returns None while already Localhost with no endpoints, and announced flag prevents repeat Restored.

## Anchors that did not match exactly once

- docs/ARCHITECTURE.md: absent by exact name; docs/Architecture.md used (case-insensitive match).
- docs/BUILD.md: absent; BUILD documentation items skipped entirely.
- All code anchors in T1-T14 matched exactly once as specified.

## Extra findings (outside scope, not fixed)

- Server::Start() bodies in src/server.cpp and src/posix_server.cpp are near-duplicates (also noted in workflow section 24). Candidate for shared helper later.
- Pre-existing test collection errors: tests/test_vlc_discovery_browse.py has an IndentationError at line 401/407 (def browse_didl_root after a def), and tests/test_vlc_discovery_browse_selfcheck.py imports from it. Both excluded from all runs this session; per AGENTS.md pre-existing failures are fixed only at the very end and were not part of this workflow's scope.
- Pre-existing baseline failures listed above were not touched.
- LSP/clangd diagnostics seen during edits (winsock sockaddr redefinition, LoadIconMetric/GetWindowTextW conversions, GTK headers parsed by MSVC clangd, curl/curl.h include path) are all pre-existing clangd environment noise, not compile errors; both platform builds succeeded after every source task.

## Build verification

- Windows: `build.bat --x64` succeeded after T6, T8, T9, T10, T12. output/winx64 binaries current (T1+T5-T12).
- Linux: `wsl ./build.sh --install` succeeded after T3, T10, T13, T14. deb dlna-server_2.26.7662_amd64.deb generated and installed; output/linux current.
