# DLNA Server Agent Rules

- never compress this file
- never use absolute paths in this repo. always use relative paths
- Always use caveman skill in this repo.
- Use `git` for git tasks. Use `gh` for GitHub tasks. that the user explicitly approves. never create a branch, never commit
- When user names a skill, follow that skill to the latter.
- No fallbacks when user states a requirement. Ask a yes/no question in B1 english if requirement conflict exists.
- sudo password for wsl is in the .env file
- Do not make proactive decisions outside user scope. Ask first.
- no such thing as a pre-existing failiure. those must be fixed at the very end after implementing user's requirements

## Automation

- Automate viable steps with PowerShell or batch scripts.
- If any uncertainty exists before running a task, ask only yes/no questions.
- If a command or dependency is missing, stop and notify user.
- If same error appears twice, write a detailed description of the problem as if explaining to sb who wont have access to the codebase. mention what you have tried and the results you got. you are asking for info and how how to apply it. write the detailed prompt in c2 english using the right term for everything signal over noise. im sending this to a researching agent. specify exactly what you need.
- Avoid verbose logs. Show only relevant output or error summaries.
- If errors or inconsistencies appear, log and attempt correction.

## Build And Dependencies

- never build manually. always use the build scripts. never edit the build scripts unless it has bugs
- build windows assets using command `& .\build.bat --x64 2>&1 | Select-Object -Last 30`
- build linux assets using command `wsl.exe -d Ubuntu -- bash -lc 'DLNA_SUDO_PASSWORD=" " ./build.sh --install'` with no extra parameters. this script will handle everything (copying assets, building and installing)
- `build.sh --install` runs one cmake configure+build that produces both the raw binaries and the .deb package in the same temp build dir. there is no second build.
- `build_deb.sh` is a thin wrapper that calls `build_linux.sh --deb`. do not call it directly in automation.
- CMake requires libcurl. Do not add curl CLI or legacy IO fallback.
- Windows build deps use vcpkg. Expected root is `$env:VCPKG_ROOT` or `$env:USERPROFILE\vcpkg`.
- Windows curl packages: `curl:x64-windows-static` and `curl:x86-windows-static`.
- Windows CMake must pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` and matching `-DVCPKG_TARGET_TRIPLET`.
- POSIX/WSL build deps include `libcurl4-openssl-dev`.
- End-user installs must not require `CURL_LIBRARY` or `CURL_INCLUDE_DIR`; those are build-machine inputs only.

## WSLg Integration

- The desktop file `Exec` line uses `env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/dlna-server-gui` so the wrapper skips the gtk-launch re-entry block (gtk-launch spawns a fresh process without env inheritance, which causes an infinite loop otherwise).
- `TryExec` points to `dlna-server-gui-bin` (the actual binary). WSLg's DVCPlugin uses `TryExec` for the Start Menu shortcut target, so the shortcut calls the binary directly via `wslg.exe`.
- `StartupWMClass=com.github.dlna-server-14ag` matches the GTK application ID set in `gtk_application_new`. This is what WSLg uses to associate the running window with its taskbar icon.
- Icons are installed in `/usr/share/icons/hicolor/{16x16,48x48,128x128,256x256,scalable}/apps/dlna-server.*`. The `Icon=dlna-server` key in the desktop file resolves to these.
- `gtk_window_set_icon_name(window, "dlna-server")` in the source sets the window icon name to match the hicolor entries.
- `output/linux/share/` files on DrvFS may be root-owned from a prior build. `build_linux.sh` uses `sudo_run rm` to clear them before staging new ones.


## Release Output

- Every built artifact goes under `output/{platform}/`.
- `output/` root is only for source release zip and release `.md` files.
- Build scripts must not clean `output/` root.
- Build scripts clean selected `output/{platform}` folders by default, then recreate them.
- `--platform` accepts comma-separated platform names. Default builds all supported platforms; do not add an `all` flag.
- Supported platform names: `winx64`, `winx86`, `linux`, `macos-x64`, `macos-arm64`.

## Testing And Verification

- never run tests before touching the code.
- Use test-first workflow for implementation: failing test, implementation, passing test, refactor.
- Do not write C++ tests. use python tests instead
- Prefer blackbox tests for HTTP, SOAP, SSDP, GENA, media browse, playlists, remote IO, packaging, and build outputs.
- Use source-contract tests only when device/platform behavior cannot run in CI.
- After completing a task, verify each user prompt item was addressed.
- Run `python -m pytest -q --tb=no` for new tests that have just been added 
- Run 
```powershell
python -m pytest -v --tb=no 2>&1 | Select-String -Pattern " FAILED | ERROR | SKIP " 

```
or

```bash
python3 -m pytest -v --tb=no 2>&1 | tee >(result=$(grep "100"); if [ -n "$result" ]; then top -b -n 1 | grep "dlna-server" | awk '{print $1}' | xargs kill -9; fi) | grep -E '(^|[[:space:]])(FAILED|ERROR|SKIP)([[:space:]]|$)'

```
when executing the full test suite so as to filter out passed tests and see failed tests only.
- there should never be skipped tests. all tests should be marked with platform so that they are deselected when they can not run in the curent platform. if a test is skipped, it should be fixed or removed
- Run Windows CMake configure/build for `dlna-server` when Windows build files changed.
- Run POSIX CMake configure/build with `-DDLNA_ENABLE_GTK4_GUI=OFF` when POSIX/shared code changed.
- tests dependent on the linux(and there is proof that they completetly can not run on windows) environment must be tagged as posix only and tests dependent on the windows(and there is proof that they completetly can not run on linux) environment must be tagged as windows only so that tests only run in appropriate environments


## Code Constraints

- Use minimal code.
- Prefer O(1) lookup structures. O(n) is acceptable where data size or order requires it.
- Avoid deadlocks and races. Keep lock hold time short.
- Do not revert unrelated user changes.
