#!/bin/bash
cd /mnt/c/Users/philip/sauce/dlna-server
for i in $(seq 1 10); do
  output/linux/dlna-server-gui-bin --kill-server >/dev/null 2>&1
  GDK_BACKEND=x11 dbus-run-session -- xvfb-run -a bash -c '
    output/linux/dlna-server-gui-bin 2>/tmp/gui.err & PID=$!
    sleep 3
    WID=$(xdotool search --name "DLNA Server" | head -n1)
    xdotool windowclose "$WID"
    timeout 20 tail --pid=$PID -f /dev/null
    kill -0 $PID 2>/dev/null && echo "LINGERS" && kill -9 $PID
  ' || true
  echo "run $i temp-line-count=$(grep -c 'TEMP close request received' /tmp/gui.err 2>/dev/null || echo 0)"
  grep -E 'unexpectedly destroyed|egl_native_window|X Window System error' /tmp/gui.err 2>/dev/null | head -n 3 || true
done
