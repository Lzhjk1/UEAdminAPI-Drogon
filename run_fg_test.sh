#!/bin/bash
# Foreground run with timeout to see all output
pkill -f UEAdminAPI_drogon 2>/dev/null || true
sleep 1

cd /home/ligb/ueadmin-api-drogon/build

# Run in foreground, capture all output, kill after 15s
timeout 15 ./UEAdminAPI_drogon 2>&1 || true
echo ""
echo "=== EXIT CODE: $? ==="
