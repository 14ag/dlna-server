# Agent Task: POSIX Build + pytest Verification for dlna-server (F-CMR-01..05)

You are working on an Ubuntu machine on the dlna-server repository. Complete the build and test steps below exactly as specified. Do NOT commit or push anything. Leave the working tree uncommitted. Do NOT modify source code -- your job is build + test + report. If you find a real defect, report it precisely with file/line/observed-vs-expected; do not fix it yourself.

## Context

- Repo (WSL path, if you are in WSL): `/mnt/c/Users/philip/sauce/dlna-server`. If you are on a standalone Ubuntu box, clone/rsync the repo there first (branch `review`).
- The working tree contains uncommitted changes implementing FIVE findings from the workflow doc `.vscode/dlna-server-concurrency-memory-perf-review-02-8-26.md`:
  - F-CMR-01: `src/copydata_validation.h` (new header, `IsPlausibleWideStringCopyDataSize`). `src/mainwindow.cpp` WM_COPYDATA handler now uses explicit-length wstring construction. `src/main.cpp` and `src/posix_main.cpp` gained `--print-is-plausible-copydata-size <cbData>`.
  - F-CMR-02: `src/fltk_gui_main.cpp` BeginRescan() now uses a JOINABLE `m_rescanWorker` thread (no more `.detach()`); `~MainWindow()` joins it.
  - F-CMR-03: `src/mainwindow.cpp` WM_KILL_SERVER handler now joins `m_worker` before `DLNAServer.Stop()`.
  - F-CMR-04: `src/media_database.h` gained `std::vector<int> m_freedIds`; `src/media_database.cpp` PruneUntouched() reclaims IDs, GetOrCreateStableIdLocked() reuses freed IDs and guards INT_MAX overflow. Both main files gained `--print-media-database-id-reuse-lifecycle` and `--print-media-database-id-overflow-guard <tsvPath>`.
  - F-CMR-05: `src/browse_page_cap.h` (new header, `kMaxBrowseResponseItems = 16000`, `ClampBrowseRequestedCount`). `src/contentdirectory.cpp` BuildDIDL uses it. Both main files gained `--print-clamp-browse-requested-count <requested> <available>`.
  - New pytest files: `tests/test_copydata_validation.py`, `tests/test_fltk_rescan_lifecycle.py`, `tests/test_kill_server_race.py` (Win32-only, will be skipped on POSIX), `tests/test_media_database_id_reuse.py`, `tests/test_media_database_overflow_guard.py`, `tests/test_browse_page_cap.py`.

## Task A - POSIX build

Run from the repo root inside WSL:

```
cd /mnt/c/Users/philip/sauce/dlna-server
DLNA_SUDO_PASSWORD=" " ./build-assets.sh
```

This script handles asset copying, CMake configure, build (targets `dlna_core`, `dlna-server`, `dlna-server-gui-native` FLTK target when `DLNA_ENABLE_FLTK_GUI=ON`, the default), packaging a `.deb`, and installing it via apt (needs sudo; the script uses the env var for the password). If you are on a standalone Ubuntu box without the script, configure with CMake `-DDLNA_ENABLE_FLTK_GUI=ON` (default) and build the `dlna-server` target.

Acceptance:
- Script exits 0.
- No NEW compiler warnings vs the pre-change build, especially no "unused variable"/"unused parameter" warnings tied to the new hooks in `posix_main.cpp`.
- If the build fails, capture the failing compiler output verbatim and report it. Do not retry with hacks.

## Task B - Ensure the pytest POSIX binary exists

The tests need the plain executable at `output/linux/dlna-server`. After a successful build the binary typically lives at:

```
/mnt/c/Users/philip/sauce/dlna-server/build-release-linux-stage/install/bin/dlna-server
```

Copy it into place:

```
cp /mnt/c/Users/philip/sauce/dlna-server/build-release-linux-stage/install/bin/dlna-server /mnt/c/Users/philip/sauce/dlna-server/output/linux/dlna-server
```

If the stage path differs, find the built binary (`find build-release-linux* -name dlna-server -type f`) and copy it to `output/linux/dlna-server`. Then confirm `test -x output/linux/dlna-server` passes.

## Task C - pytest, in this exact order

Run from the repo root. Use `python3 -m pytest` (install pytest via `python3 -m pip install pytest` if missing; also needs `requests` for the browse functional test: `python3 -m pip install requests`).

1. F-CMR-01 boundary tests:
   ```
   python3 -m pytest -q --tb=no tests/test_copydata_validation.py
   ```
   IMPORTANT: the parametrized file hardcodes expectations for a 2-byte wchar_t (Windows). On Linux/macOS `sizeof(wchar_t)` is 4, so `IsPlausibleWideStringCopyDataSize` is divisible-by-4 logic. The correct POSIX expectations for inputs (0,1,2,3,4,4294967295,4294967294) are (0,0,0,0,1,0,0). Verify the hook prints exactly those, then report which (if any) parametrized case mismatched and confirm the mismatch is solely the wchar_t-size difference (2-byte vs 4-byte), not a logic bug.

2. F-CMR-02 structural tests (source-regex, platform-independent):
   ```
   python3 -m pytest -q --tb=no tests/test_fltk_rescan_lifecycle.py
   ```
   Expected: 2 passed.

3. F-CMR-04 tests:
   ```
   python3 -m pytest -q --tb=no tests/test_media_database_id_reuse.py tests/test_media_database_overflow_guard.py
   ```
   Expected: all pass. Also run the overflow-guard scenario under a UBSan build if you can do so cheaply (this workflow's validation criterion): configure a throwaway build dir with `-DCMAKE_CXX_FLAGS="-fsanitize=undefined"` (plus the same include/link deps), build `dlna-server`, and run `output/linux/dlna-server --print-media-database-id-overflow-guard <tsv>` on a TSV whose highest id is 2147483646. Confirm no UBSan diagnostic appears. If UBSan setup is not feasible, say so explicitly -- the deterministic pytest assertion (id_a == 2147483647, id_b == 1000000) is the primary gate.

4. F-CMR-05 tests:
   ```
   python3 -m pytest -q --tb=no tests/test_browse_page_cap.py
   ```
   Expected: 7 passed (6 parametrized boundary cases + 1 functional browse test).

5. F-CMR-03 test:
   ```
   python3 -m pytest -q --tb=no tests/test_kill_server_race.py
   ```
   Expected: SKIPPED on POSIX (Win32-only, marked `@pytest.mark.skipif(sys.platform != "win32")`). If it runs or fails, that is unexpected -- report it.

6. Full suite:
   ```
   python3 -m pytest -q --tb=no
   ```
   Report the exact summary line. Known environment-artifact caveats, NOT regressions (do not fix, just report):
   - If `tests/test_media_source_dialog.py::test_media_source_file_extensions_symmetric_across_platforms` fails with an Exec-format/OSError while trying to run the WINDOWS exe (`output/winx64/DLNA Server.exe`) on Linux, that is the same cross-platform artifact already documented in `pre_existing_fails.txt` (Windows side gets the mirror-image failure). If the winx64 dir is absent, that test runs the POSIX binary only and should pass.
   - `test_release_output_layout.py` failure listed in older notes no longer exists in the suite; ignore any stale reference.
   If any OTHER test fails, that is a regression -- report it with full traceback.

## Report back

Return a concise report containing:
1. Build: exit code, targets built, any warning lines.
2. Task B: confirmation the binary was copied and is executable.
3. Each test group (1-6) with observed pass/fail/skip counts and, for any failure, the test name and assertion message.
4. Full-suite summary line exactly as pytest printed it.
5. The POSIX wchar_t-size adjustment you observed in group 1 (actual stdout values for the 7 inputs).
6. UBSan result for the overflow guard (or explicit statement that it was not feasible).
7. Anything you could NOT verify.

Do not commit. Do not push. Do not touch any source file.
