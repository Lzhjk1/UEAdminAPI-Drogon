#!/bin/bash
# Extended test - wait longer for port binding
pkill -f UEAdminAPI_drogon 2>/dev/null || true
sleep 1

cd /home/ligb/ueadmin-api-drogon/build
rm -f /tmp/ueadmin_logs/drogon.*.log
mkdir -p /tmp/ueadmin_logs

./UEAdminAPI_drogon &
SERVER_PID=$!
echo "Server PID=$SERVER_PID"

# Poll for port binding up to 20 seconds
for i in $(seq 1 20); do
    if ss -tlnp 2>/dev/null | grep -q ':8080 '; then
        echo "Port 8080 LISTENING after ${i}s"
        break
    fi
    echo "Waiting... ${i}s"
    sleep 1
done

echo ""
echo "=== Port check ==="
ss -tlnp 2>/dev/null | grep 8080 || echo "Port 8080 NOT listening"

echo ""
echo "=== /system/ping ==="
curl -s --max-time 3 http://localhost:8080/system/ping || echo "CURL_FAILED"
echo ""

echo "=== /api/system/ping ==="
curl -s --max-time 3 http://localhost:8080/api/system/ping || echo "CURL_FAILED"
echo ""

echo "=== JWKS ==="
curl -s --max-time 3 http://localhost:8080/.well-known/jwks.json || echo "CURL_FAILED"
echo ""

echo ""
echo "=== Process alive? ==="
kill -0 $SERVER_PID 2>/dev/null && echo "ALIVE" || echo "DEAD"

echo ""
echo "=== All logs ==="
cat /tmp/ueadmin_logs/drogon.*.log 2>/dev/null | grep -v 'Pg connection' | tail -30

kill $SERVER_PID 2>/dev/null || true
pkill -f UEAdminAPI_drogon 2>/dev/null || true
echo "=== DONE ==="
