#!/bin/bash
# Test all possible route prefixes
pkill -9 -f UEAdminAPI_drogon 2>/dev/null || true
sleep 2

cd /home/ligb/ueadmin-api-drogon/build
./UEAdminAPI_drogon &
SERVER_PID=$!
echo "PID=$SERVER_PID"
sleep 3

echo "=== Exact routes ==="
for path in \
    "/" \
    "/system/ping" \
    "/api/system/ping" \
    "/system/info" \
    "/api/system/info" \
    "/.well-known/jwks.json" \
    "/.well-known/openid-configuration" \
    "/oauth2/token" \
    "/api/oauth2/token" \
    "/oauth2/authorize" \
    "/api/oauth2/authorize"; do
    code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 3 "http://localhost:8080${path}")
    echo "$code $path"
done

echo ""
echo "=== Verbose /system/ping ==="
curl -v --max-time 3 http://localhost:8080/system/ping 2>&1

echo ""
echo "=== Registered routes from logs ==="
cat /tmp/ueadmin_logs/drogon.*.log 2>/dev/null | grep -i 'listen\|bind\|route\|register\|path' | head -15

kill $SERVER_PID 2>/dev/null || true
pkill -f UEAdminAPI_drogon 2>/dev/null || true
echo "=== DONE ==="
