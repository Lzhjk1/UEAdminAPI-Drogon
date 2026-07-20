#!/bin/bash
# Full API test script - remote PG version
set -e

pkill -f UEAdminAPI_drogon 2>/dev/null || true
sleep 1

cd /home/ligb/ueadmin-api-drogon/build
rm -f /tmp/ueadmin_logs/drogon.log
mkdir -p /tmp/ueadmin_logs

echo "Starting server..."
./UEAdminAPI_drogon &
SERVER_PID=$!
echo "PID=$SERVER_PID"

echo "Waiting for startup (12s)..."
sleep 12

echo ""
echo "=== Port check ==="
ss -tlnp 2>/dev/null | grep 8080 || echo "Port 8080 not listening"

echo ""
echo "=== /system/ping ==="
curl -s --max-time 5 http://localhost:8080/system/ping || echo "FAILED"
echo ""

echo "=== /api/system/ping ==="
curl -s --max-time 5 http://localhost:8080/api/system/ping || echo "FAILED"
echo ""

echo "=== /system/info ==="
curl -s --max-time 5 http://localhost:8080/system/info || echo "FAILED"
echo ""

echo "=== JWKS ==="
curl -s --max-time 5 http://localhost:8080/.well-known/jwks.json || echo "FAILED"
echo ""

echo ""
echo "=== NON-PG LOGS ==="
cat /tmp/ueadmin_logs/drogon.log 2>/dev/null | grep -v 'Pg connection' | tail -25

echo ""
echo "=== PROCESS STATUS ==="
kill -0 $SERVER_PID 2>/dev/null && echo "Server ALIVE" || echo "Server DEAD"

kill $SERVER_PID 2>/dev/null || true
pkill -f UEAdminAPI_drogon 2>/dev/null || true
echo "=== DONE ==="
