#!/usr/bin/env bash
set -u
cd /tmp
rm -rf guitest && mkdir -p guitest/a guitest/b
Xvfb :199 -screen 0 1280x800x24 > /dev/null 2>&1 &
XVFB_PID=$!
sleep 2
export DISPLAY=:199
cd /mnt/c/Users/philip/sauce/dlna-server
./output/linux/dlna-server-gui-bin --source "/tmp/guitest/a" > /tmp/gui_first.log 2>&1 &
FIRST=$!
sleep 3
timeout 10 ./output/linux/dlna-server-gui-bin --source "/tmp/guitest/b" > /tmp/gui_second.log 2>&1
echo SECOND_RC=$?
sleep 3
./output/linux/dlna-server-gui-bin --kill-server
echo KILL_RC=$?
sleep 4
if kill -0 $FIRST 2>/dev/null; then
  echo "=== gdb backtrace ==="
  gdb -p $FIRST -batch -ex "set pagination off" -ex "thread apply all bt" > /tmp/gui_bt.txt 2>&1
  grep -E "^Thread|#[0-9]+ " /tmp/gui_bt.txt | head -80
  kill -9 $FIRST
else
  echo FIRST_EXITED=yes
fi
kill $XVFB_PID 2>/dev/null