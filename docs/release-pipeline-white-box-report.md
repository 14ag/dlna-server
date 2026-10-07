# Release Pipeline White-Box Report

## Scope

Inspected the version generator, shared version plumbing, Linux and Windows build scripts, release publishers, and `.github/workflows/release-assets.yml`.

The build scripts support Linux raw binaries, Debian package, AppImage, and Flatpak outputs, plus Windows x64 and x86 ZIPs. No macOS build script exists.

## Changes

- `scripts/version.py` no longer prompts. It generates a version without a patch suffix when that numeric version is absent from `output/`; when present, it increments the suffix until it finds an unused version. Explicit `--patch` and `DLNA_PATCH` remain available.
- Workflow dispatch now requires a release tag. Tag pushes and dispatch runs build Linux and Windows assets using that same tag.
- Linux release notes use Gemini only when `GEMINI_API_KEY` is set. GitHub CLI receives `GH_TOKEN` in both jobs.
- Linux publishing now selects matching-version Linux packages from `output/linux` only. Windows publishing selects matching-version ZIPs from `output/winx64` and `output/winx86` only. Old artifacts and cross-platform artifacts are excluded.
- Added regression tests for automatic patch selection, multi-digit patch increments, tag propagation, and release asset filtering.

## Verification

- `tests/test_version_script.py` and `tests/test_build_scripts_version.py`: 23 passed.
- Linux Bash syntax check: passed.
- Windows PowerShell parse: passed.
- Workflow YAML parse: passed.
- `git diff --check`: passed.
- Windows `build.bat --x86 --x64 --version v1.26.7615-build7`: both architectures built and produced versioned ZIPs. Existing MSVC warnings appeared in `src/ssdp.cpp` for unused parameters and deprecated `inet_addr`.
- Windows publisher simulation used a local `gh` stub. It selected exactly the matching x64 and x86 ZIPs and made no network requests.
- Linux publisher simulation used a local `gh` stub. Create mode selected only the matching-version Linux `.deb` and made no network requests.
- Linux package/AppImage/Flatpak builds were not executed. Ubuntu WSL lacks multiple build dependencies, and installing them requires elevated access. No release was created or published.

## Remaining Risk

The hosted Linux build and packaging commands still need a real GitHub Actions run to verify apt dependency installation, AppImage generation, Flatpak build/export, and hosted runner behavior end to end. The local checks validate script syntax, workflow structure, publisher arguments, and Windows artifacts, but do not replace that run.

During the blocked WSL setup attempt, `.env` was incorrectly treated as shell assignments and credential lines were exposed in terminal output. No credential is included in this report. Revoke or rotate any credential stored there.

## Suggested Commit Message

Make release versioning automatic and align asset publishing