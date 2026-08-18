#!/bin/sh
cd /mnt/c/Users/philip/sauce/dlna-server
for i in 1 2 3; do
  python3 -m pytest tests/test_posix_single_instance.py::TestSecondLaunchSupersedesFirst::test_second_launch_replaces_first -v --tb=line 2>&1 | tail -3
  echo "=== run $i done ==="
done
