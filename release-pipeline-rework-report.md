# Release pipeline rework completion report

## 1 Task status

1. T1 version.py build suffix only on request: done. Replaced scripts/version.py with spec block so plain tag has no suffix and suffix appears only via --build or DLNA_BUILD.
2. T2 build.sh add --build remove release options: done. Replaced build.sh whole file per spec, removed release handling, added --build export.
3. T3 build.bat add --build remove release options: done. Replaced build.bat whole file per spec, removed release handling, added --build handling.
4. T4 delete local release scripts: done. Deleted scripts/release-linux.sh and scripts/release-windows.ps1, kept scripts/release-diff.sh.
5. T5 add scripts/release-notes.sh: done. Created file exactly per spec with Gemini header and commit list fallback.
6. T6 add four workflow files: done. Created release.yml build-windows.yml build-linux.yml release-notes.yml exactly per spec.
7. T7 remove old release assets workflow: done. Deleted .github/workflows/release-assets.yml, no live refs to deleted scripts remain.
8. T8 line endings gitattributes: done. Added *.in and identity.env entries and ran git add --renormalize.
9. T9 tests find suffix asserts add test file run suite: done. Updated stale version and build script tests, deleted tests asserting deleted files, added tests/test_release_pipeline.py.
10. T10 owner manual steps report only: done. No code change, steps relayed below.

## 2 Verification results observed

```
T1 version.py: done, verify output:
  python scripts/version.py printed v2.26.7640
  python scripts/version.py --build 6 printed v2.26.7640-build6
T2 build.sh: done, verify output:
  bash -n build.sh exit code 0
  bash build.sh --build exited 1 printing --build needs a number
T3 build.bat: done, verify output:
  Select-String for RELEASE NOTES UPDATE HAS_WIN_ARCH --release --notes --update findstr-update found no matches
  --build token present
  DLNA_BUILD=6 python scripts/version.py printed v2.26.7640-build6
  Full build.bat --x64 --build 6 not run because it triggers a full Windows build
  STOP gate at spec line 351 reached
T4 release scripts deleted: done, extra references found:
  grep for release-linux and release-windows found no live refs
  only hits were done-reports pycache the workflow doc itself and clean .github
T5 release-notes.sh: done:
  bash -n scripts/release-notes.sh exit code 0
T6 workflows created: done, previous workflow names:
  ci.yml readme-banner-reset.yml readme-banner-update.yml release-assets.yml release-notes-banner.yml
  created .github/workflows/release.yml .github/workflows/build-windows.yml .github/workflows/build-linux.yml .github/workflows/release-notes.yml
T7 old workflow removed: done:
  deleted .github/workflows/release-assets.yml
  grep .github/workflows for release-linux release-windows Gemini left only new release-notes.yml GEMINI use which is expected per spec
  grep scripts found no refs
T8 line endings: files renormalized:
  git ls-files --eol showed *.in worktree files as w/crlf with index already lf after renormalize so Linux checkout is LF
  .gitattributes already had *.sh AppRun dlna-server-gui entries, added *.in and identity.env lines
  git add --renormalize needed git rm of T4 and T7 deleted paths first because renormalize fataled on missing worktree files
  full git status captured, many pre-existing user worktree mods left uncommitted, no commit made
T9 tests: edited tests, new tests, pytest result:
  edited tests/test_version_script.py with 7 suffix expects changed to suffix-free tags, fixed stale RELEASE ordering assert in test_build_sh, deleted test_release_workflow_builds_all and test_release_publishers_only
  new tests/test_release_pipeline.py added verbatim per spec
  pip install pyyaml ran to satisfy workflow test dependency
  pytest tests/test_release_pipeline.py -q: 14 passed 4 deselected for posix_only on Windows
  edited files run: 21 passed
  full suite ignoring 2 broken-collection files: 476 passed 104 deselected
  2 collection errors in tests/test_vlc_discovery_browse.py IndentationError line 407 and selfcheck come from pre-existing staged user mods with git status M untouched this session
T10 owner steps relayed: yes
Out of scope findings:
  .gitignore line 88 build* ignores new T6 files build-linux.yml and build-windows.yml so git status hides them, check-ignore confirmed, left untouched per scope
  pre-existing staged edits to tests/test_vlc_discovery_browse.py and selfcheck break collection, untouched per scope
  workflow doc out of scope list relayed below
Assumptions that proved false:
  git add --renormalize does not tolerate already deleted worktree paths, it fataled and needed git rm first
```

## 3 T10 owner manual steps

1. Repository secret: Settings, Secrets and variables, Actions, add GEMINI_API_KEY. Without it the notes fall back to the commit list.
2. The Run workflow button lists a workflow_dispatch workflow only when the file exists on the default branch. Merge the .github/workflows files to the default branch once. In the dropdown pick the branch to build, which is review. The run uses the workflow file from the selected branch.
3. First run: Actions, release, Run workflow, leave build blank. Expected: jobs version, windows, linux, publish, notes all green. The release holds 2 zips, 1 deb, 1 AppImage, 1 flatpak. The release body is generated notes.
4. Second run the same day. Expected: tag patch digit bumps and no tag collision.
5. Run with build set to 6. Expected: tag ends in -build6.

## 4 Out of scope findings documented not fixed

- scripts/build_flatpak.sh runs flatpak-builder with or true. A failed flatpak build is masked until a later command fails.
- scripts/build_appimage.sh runs linuxdeploy --output appimage after downloading only linuxdeploy-x86_64.AppImage. Unverified here whether the downloaded AppImage bundles the appimage plugin. If CI reports a missing plugin, that is the cause.
- ubuntu-22.04 runner images are deprecated from 2026-09-17 and unsupported from 2027-04-17. Brownouts begin 2027-03-23. Plan a move to ubuntu-24.04. The .deb then links against newer GTK 4 and glibc, so it will not install on 22.04.
- scripts/build_linux.sh deletes everything in output/linux at start. Fine in CI. Local builds lose earlier assets.
- Flatpak runtime installs run on every CI run with no cache. Adds minutes, not failures.
- Extra session findings: .gitignore line 88 build* hides new build-windows.yml and build-linux.yml from git status. Pre-existing staged user modifications to tests/test_vlc_discovery_browse.py and a selfcheck file break pytest collection with an IndentationError at line 407.

## 5 Commit message

release: build suffix on request plus CI release pipeline rework

All changes left as uncommitted working tree, no commit made.
