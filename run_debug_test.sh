#!/bin/bash
# Test with stderr captured separately
pkill -9 -f UEAdminAPI_drogon 2>/dev/null || true
sleep 2

cd /home/ligb/ueadmin-api-drogon/build

timeout 20 ./UEAdminAPI_drogon > /tmp/ueadmin_stdout.log 2> /tmp/ueadmin_stderr.log || true

echo "=== STDOUT ==="
cat /tmp/ueadmin_stdout.log
echo ""
echo "=== STDERR ==="
cat /tmp/ueadmin_stderr.log
echo ""
echo "=== LOG FILES ==="
cat /tmp/ueadmin_logs/drogon.*.log 2>/dev/null | grep -v 'Pg connection' | tail -25
echo "=== DONE ==="
