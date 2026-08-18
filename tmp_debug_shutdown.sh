#!/bin/bash
killall -9 dlna-server 2>/dev/null
sleep 1
rm -rf /tmp/dlna-server-1000 /tmp/dbg-test4
mkdir -p /tmp/dbg-test4/config/dlna-server /tmp/dbg-test4/media /tmp/dbg-test4/runtime
printf '[Settings]\nPort=19096\nMediaSources=/tmp/dbg-test4/media\n' > /tmp/dbg-test4/config/dlna-server/config.ini
export DLNA_SERVER_SKIP_FIREWALL=1
export XDG_CONFIG_HOME=/tmp/dbg-test4/config
export HOME=/tmp/dbg-test4/config
export XDG_RUNTIME_DIR=/tmp/dbg-test4/runtime

/mnt/c/Users/philip/sauce/dlna-server/output/linux/dlna-server --headless &
PARENT_PID=$!

# Wait for parent to exit (daemonization)
wait $PARENT_PID 2>/dev/null
echo "Parent exited with code $?"
sleep 2

# Find daemon process by comm name
DAEMON_PIDS=$(pgrep -x dlna-server 2>/dev/null)
echo "Daemon PIDs: $DAEMON_PIDS (count: $(echo $DAEMON_PIDS | wc -w))"

if [ -z "$DAEMON_PIDS" ]; then
    echo "No daemon found!"
    exit 1
fi

for pid in $DAEMON_PIDS; do
    echo "Sending SIGTERM to PID $pid"
    kill -TERM $pid 2>/dev/null
done
echo "SIGTERM sent"

T0=$(date +%s)
for i in $(seq 1 30); do
    sleep 0.5
    REMAINING=""
    for pid in $DAEMON_PIDS; do
        if kill -0 $pid 2>/dev/null; then
            REMAINING="$REMAINING $pid"
        fi
    done
    if [ -z "$REMAINING" ]; then
        T1=$(date +%s)
        echo "Daemon exited after $((T1 - T0))s"
        break
    fi
done

if [ -n "$REMAINING" ]; then
    echo "Daemon STILL RUNNING after 15s: $REMAINING"
    for pid in $REMAINING; do
        kill -9 $pid 2>/dev/null
    done
fi

rm -rf /tmp/dlna-server-1000 /tmp/dbg-test4
