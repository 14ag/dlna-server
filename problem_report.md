# Problem Report: Headless GTK4 GUI launch hangs the shell tool (pipe-inheritance deadlock)

## Summary

When driving the headless GTK4 build of `dlna-server-gui-bin` under Xvfb from an
automated bash session, the launching shell command repeatedly hung (eventually
timed out) and left orphaned GUI/Xvfb processes. The same hang occurred on every
attempt until the launch harness was changed to redirect all child stdout/stderr
to files and to use `timeout -s KILL` (SIGKILL) plus `wait`.

This is an infrastructure/harness problem, not a code problem. The two code
phases in the workflow
(GTK4 CSS selector fix in `src/gtk4_gui_main.cpp`, and the
`GetDlnaServerHeader()` SERVER-header fix in `src/dlna_utils.cpp` plus the
`--print-dlna-server-header` CLI hooks in `src/main.cpp` / `src/posix_main.cpp`)
are complete and verified. This report documents only the harness failure so a
researcher can confirm the root cause.

## What I tried and the results

All commands below ran from the repo working directory on this Linux host
(`id -u` = 1000, not root). `Xvfb`, ImageMagick `import`, and `xdotool` are
installed.

1. `run_gtk4_screenshot.sh`: start `Xvfb :99 -screen 0 440x600x24 &`, launch
   `dlna-server-gui-bin` with `GDK_BACKEND=x11 DISPLAY=:99` and an XDG config
   that contains one media source, poll `/tmp/opencode/gtk4_dbg.txt` for the
   `OnAppActivate enter` marker, then `import -window root`.
   Result: black screenshot (every sampled pixel `gray(0)` = `rgb(0,0,0)`).
   Debug log contained only `main enter` -- `OnAppStartup` and
   `OnAppActivate` never fired, so the window never mapped. No GTK error on
   stderr.

2. Foreground run `timeout 6 .../dlna-server-gui-bin 2>&1 | tail -30`:
   Result: the bash command did not return; the shell tool killed it at its
   120s timeout. `timeout` (SIGTERM) did not retire the GUI.

3. Cleanup command `pkill -9 -f Xvfb; ...; cat /tmp/xvfb.log`:
   Result: also hung to the 120s tool timeout. `cat` never returned despite the
   process list being clean afterward.

4. `ssdp_tcpdump.sh` (early SSDP capture): `printf '\n' | sudo -S tcpdump ...`.
   Result: empty capture -- wrong sudo password (empty instead of the
   documented single-space `" "`, per AGENTS.md `DLNA_SUDO_PASSWORD=" "`).
   Fixed by using `printf '%s\n' " "`.

## Root-cause analysis (signal, not noise)

Two interacting defects in the harness:

A. Stale X socket. `Xvfb :99` logged:
   `_XSERVTransmkdir: Mode of /tmp/.X11-unix should be set to 1777` and
   `XSERVTransSocketUNIXCreateListener: ...SocketCreateListener() failed`.
   A previous orphaned Xvfb had left a bad `/tmp/.X11-unix` state, so a new
   Xvfb could not create its UNIX listener. The GUI then connected to a dead/
   stale `:99` (or none) and stalled at `gtk_application_new`/`g_application_run`.
   Fix: use a fresh display number (`:55`) so there is no stale socket.

B. Pipe-inheritance deadlock (the repeated hang). When a backgrounded child is
   launched with `&<cmd>` and its stdout/stderr are NOT redirected to a file,
   the child inherits the shell tool's output pipe. If the child blocks
   (GTK main loop) or ignores SIGTERM, the pipe stays open, so `cat`/output
   reads never see EOF and the shell tool waits until its own timeout. This
   reproduced in attempts 2 and 3 (hang at 120s both times). Fix: redirect every
   backgrounded process's stdout/stderr to files (`>/tmp/g.out 2>/tmp/g.err`)
   and ensure the GUI is reaped with `timeout -s KILL <N>` (SIGKILL, not
   SIGTERM) followed by `wait 2>/dev/null`.

With both fixes (`/tmp/gtk4_clean.sh`: fresh `:55`, file-redirected GUI +
`timeout -s KILL 14`, poll debug log for `BuildMainWindow exit present done`,
then `import`), the window rendered. Source-list pixels: `gray(31)` =
`rgb(31,31,31)` at all sampled points (list background and row), toolbar band
`gray(143)` (dark, unchanged).

## Exact repro / what to validate

Reproduce the hang with a pipe-bound background child:
```
Xvfb :99 -screen 0 440x600x24 >/tmp/x.log 2>&1 &
timeout 6 /path/dlna-server-gui-bin 2>&1 | tail -30   # <- hangs; SIGTERM ignored
```
Confirm it clears when stdout/stderr are moved off the pipe and SIGKILL is used:
```
Xvfb :55 -screen 0 440x600x24 >/tmp/x.log 2>&1 &
timeout -s KILL 14 /path/dlna-server-gui-bin >/tmp/g.out 2>/tmp/g.err &
wait 2>/dev/null
```
Also confirm `sudo` here requires the single-space password from
`DLNA_SSUDO_PASSWORD=" "` (not empty) for any `tcpdump`/`apt` capture steps.

## What I need from a researcher

1. Confirm this is the expected pipe-inheritance behavior for this shell/tool
   wrapper, and whether there is a preferred way to fully detach a backgrounded
   GUI (e.g. `setsid ... </dev/null >file 2>&1 <&-`) so pipe inheritance can be
   avoided entirely.
2. Confirm the stale `/tmp/.X11-unix` handling (`chmod 1777`, or always use a
   fresh display number) so headless GUI runs are deterministic.

## Outcome

Resolved in-harness. Code changes are unaffected; see final report for
verification results. No code change is required for this problem -- it is
purely a test-harness/launch-script issue.
