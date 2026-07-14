# DLNA Server Agent Rules

- Always use caveman skill in this repo.
- Use `git` for git tasks. Use `gh` for GitHub tasks.
- When user names a skill, follow that skill to the latter.
- No fallbacks when user states a requirement. Ask a yes/no question if requirement conflict exists.
- Do not make proactive decisions outside user scope. Ask first.

## Automation

- Automate viable steps with PowerShell or batch scripts.
- If any uncertainty exists before running a task, ask only yes/no questions.
- If a command or dependency is missing, stop and notify user.
- If same error appears twice, write a detailed description of the problem as if explaining to sb who wont have access to the codebase. mentionwhat you have tries and the results you got. you are asking for info and how how to apply it. write the detailed prompt in c2 english using the right term for everything signal over noise. im sending this to a researching agent. specify exactly what you need.
- Avoid verbose logs. Show only relevant output or error summaries.
- If errors or inconsistencies appear, log and attempt correction.

## Build And Dependencies

- build windows assets using command `& .\build-assets.bat --platform winx64 2>&1 | Select-Object -Last 30`
- CMake requires libcurl. Do not add curl CLI or legacy IO fallback.
- Windows build deps use vcpkg. Expected root is `$env:VCPKG_ROOT` or `$env:USERPROFILE\vcpkg`.
- Windows curl packages: `curl:x64-windows-static` and `curl:x86-windows-static`.
- Windows CMake must pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` and matching `-DVCPKG_TARGET_TRIPLET`.
- POSIX/WSL build deps include `libcurl4-openssl-dev`.
- End-user installs must not require `CURL_LIBRARY` or `CURL_INCLUDE_DIR`; those are build-machine inputs only.

## Release Output

- Every built artifact goes under `output/{platform}/`.
- `output/` root is only for source release zip and release `.md` files.
- Build scripts must not clean `output/` root.
- Build scripts clean selected `output/{platform}` folders by default, then recreate them.
- `--no-clean` preserves selected platform folders.
- `--platform` accepts comma-separated platform names. Default builds all supported platforms; do not add an `all` flag.
- Supported platform names: `winx64`, `winx86`, `linux`, `macos-x64`, `macos-arm64`.

## Testing And Verification

- Use test-first workflow for implementation: failing test, implementation, passing test, refactor.
- Do not write C++ tests. use python tests instead
- Prefer blackbox tests for HTTP, SOAP, SSDP, GENA, media browse, playlists, remote IO, packaging, and build outputs.
- Use source-contract tests only when device/platform behavior cannot run in CI.
- After completing a task, verify each user prompt item was addressed.
- Run `python -m pytest -q --tb=no`.
- Run Windows CMake configure/build for `dlna-server` when Windows build files changed.
- Run POSIX CMake configure/build with `-DDLNA_ENABLE_FLTK_GUI=OFF` when POSIX/shared code changed.
- Run smoke scripts in the tests folder.


## Code Constraints

- Use minimal code.
- Prefer O(1) lookup structures. O(n) is acceptable where data size or order requires it.
- Avoid deadlocks and races. Keep lock hold time short.
- Do not revert unrelated user changes.
