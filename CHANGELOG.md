# Changelog

Windows app

- The status line shows `Server running (localhost mode)` when there is no network (see "Finding the server" below).
- Adding or deleting a source now starts the rescan in one place. Two rescans cannot run at the same time.

Linux app (GTK4)

- Closing the window no longer causes an X11 error when the display server has already removed it.
- `dlna-server-gui` no longer holds the terminal. It starts in the background.

Docker

- Added a Linux container image, `Dockerfile`, `.dockerignore` and `compose.yaml`. The image runs the headless `dlna-server` command as a non-root user with a read-only root filesystem and a health check.
- Added the `--foreground` command-line option. It keeps the POSIX server attached without turning on debug logging, so the container stays alive under `tini`.
- Added `.github/workflows/docker.yml`. Pull requests build and smoke-test the image; pushes to `main` publish `linux/amd64` and `linux/arm64` images to GHCR.
- Added [Docker](docs/Docker.md) with installation, host networking, multi-adapter, permissions, media mounting and troubleshooting steps.
- Added Docker asset tests and a POSIX foreground test.

Command line

- When the network is lost, the Linux `dlna-server` command prints a warning, but only with `--debug`. When the network returns, it tells you to run the same command again.

<details>
<summary>

### Finding the server (discovery) and playing media

</summary>
- **Faster discovery after start.** The server now sends "alive" announcements at 0, 1, 3, 8, and 20 seconds after start.
- **One address per network card.** The server now advertises one address for each adapter, on both Windows and Linux. Before, a second address on the same adapter could replace a good address on the player and break the connection.
- **Better address choice on IPv6 (Linux).** The server now picks the right network card for link-local IPv6 devices.
- **No restart storms.** When the network changes (cable, Wi-Fi, sleep and resume), the server waits a short time and restarts discovery only if the real address list changed. Rapid changes are joined into one restart. A change that arrives during the wait is handled once the wait ends, so it is not lost.
- **Localhost mode.** If there is no usable network, the server keeps running at `127.0.0.1` and stops discovery. A message tells you that only this computer can reach it. When the network returns, the app asks if you want to restart the server so other devices can see it again.
- **Search** is faster and uses a memory limit (256 folders and 200,000 items).
- The address that is shown to players is saved and refreshed when the network changes.

Crashes and data races

- Work on background threads now logs an error and goes on. An error no longer ends the whole app.
- The HTTP and SSDP `Start` and `Stop` calls now reject a second call at the same time. This removes a race.
- Config values are read through a shared lock, so many threads can read them safely.
- Windows folder watching now guards its folder list with a lock.

Memory and speed

- The catalog list reserves memory when a scan starts.
- Removed old and unused code: `fs_change_debounce.h` and the unused `FileServerPort` setting. Old config files that still have `FileServerPort` load without problems.

---
</details>
<details>
<summary>

### For developers and maintainers

</summary>
Build and release

- New scripts: `build_linux.sh`, `build_deb.sh`, `build_appimage.sh`, `build_flatpak.sh`, `install_linux.sh`, `lib_identity.sh`. Old scripts `build-linux.sh`, `install-dlna-server-linux.sh`, `release-linux.sh` and `release-windows.ps1` are removed.
- `build.sh` options: `--deb`, `--appimage`, `--flatpak`, `--install`, `--version=<tag>`, `--build <N>`. `build.bat` accepts `--x86`, `--x64`, `--version` and `--build`. The `--release`, `--notes` and `--update` options are removed.
- The release is now one GitHub workflow, "Create a release" (`release-all.yml`). You start it by hand. It makes the version itself, builds Windows (x64 and x86) and Linux, and publishes all files. It no longer needs a tag. The old `release-assets.yml` is removed.
- The release notes step uses `CHANGELOG.md` as the release notes. The Gemini script `scripts/release-notes.sh` is kept, but it is turned off in the workflow.
- Windows and Linux builds are separate reusable workflows (`build-windows.yml`, `build-linux.yml`).
- A winget manifest for version `2.26.7640` was added in `manifests/`.
- The Flatpak files are now built from one template, `packaging/flatpak/manifest.yml.in`. The Linux desktop and app details templates moved to `packaging/linux/`.
- Fonts (`resources/fonts/`), the 16 px icon, and five GTK style sheets (`styles0.css` to `styles4.css`) were added. `resources/gtk/style.css` was removed.
- The generated file `generated/version.h` was removed from the repository, with the old launcher `dlna-server-gui` in the repository root, the issue templates, and old test and report files (`posix-full-suite-log.txt`, `problem_report.md`, `redir-test.txt`).
- Agent tool files moved or added: `.opencode/opencode.json`, `.github/copilot-instructions.md`, `.github/instructions/mermaid.instructions.md`, `start_opencode.sh`.
- New tools: `tools/gen_cli_docs.py` makes the command-line docs from `cli_flags.h`. `scripts/dump-review-diff.ps1` and `scripts/release-diff.sh` write diffs for review and release notes.

Tests

- Tests use Python (`pytest`). The test folder under `tmp/` is cleaned after each session.
- New small logic headers hold the rules these flags test: `health_check_policy.h`, `modal_stack.h`, `network_mode.h`, `search_cache_budget.h`, `geometry_dump_policy.h`.

</details>
