#!/bin/bash
set -e
PROJECT=/home/ligb/ueadmin-api-drogon
BUILD=$PROJECT/build
BIN=$BUILD/UEAdminAPI_drogon

echo "Starting server from build dir..."
mkdir -p /tmp/ueadmin_logs
rm -f /tmp/ueadmin_logs/*

cd $BUILD
$BIN > /tmp/ueadmin_server.log 2>&1 &
PID=$!
echo "PID=$PID"

for i in $(seq 1 15); do
    if ss -tlnp 2>/dev/null | grep -q ':8080'; then
        echo "Port 8080 LISTENING after ${i}s"
        break
    fi
    sleep 1
done

if ! ss -tlnp 2>/dev/null | grep -q ':8080'; then
    echo "ERROR: Port 8080 never came up!"
    cat /tmp/ueadmin_server.log 2>/dev/null
    kill $PID 2>/dev/null
    exit 1
fi

echo ""
echo "=== /system/ping ==="
curl -s --max-time 3 http://localhost:8080/system/ping
echo ""

echo "=== /api/system/ping ==="
curl -s --max-time 3 http://localhost:8080/api/system/ping
echo ""

echo "=== /api/user/login/pwd (should NOT be 404) ==="
curl -s --max-time 3 -o /dev/null -w "HTTP %{http_code}" http://localhost:8080/api/user/login/pwd
echo ""

echo "=== /api/oauth2/introspect (should NOT be 404) ==="
curl -s --max-time 3 -o /dev/null -w "HTTP %{http_code}" -X POST http://localhost:8080/api/oauth2/introspect
echo ""

echo "=== /api/user/mfa (should NOT be 404) ==="
curl -s --max-time 3 -o /dev/null -w "HTTP %{http_code}" "http://localhost:8080/api/user/mfa?target=test@test.com&type=Email"
echo ""

echo "=== /.well-known/jwks.json ==="
curl -s --max-time 3 http://localhost:8080/.well-known/jwks.json | head -c 200
echo ""

echo ""
echo "=== Server logs ==="
cat /tmp/ueadmin_logs/drogon*.log 2>/dev/null | grep -v 'Pg connection' | tail -20

echo ""
echo "=== Process alive? ==="
kill -0 $PID 2>/dev/null && echo "ALIVE" || echo "DEAD"

kill $PID 2>/dev/null
echo "=== DONE ==="
