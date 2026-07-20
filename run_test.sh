#!/bin/bash
set -e

echo 'app:
  number_of_threads: 2
  log:
    log_path: /tmp/logs
  listeners:
    - address: 0.0.0.0
      port: 8080
      https: false

UserManage:
  jwt_secret: test_secret_key_123456
  jwt_issuer: ueadmin-api-test

ThirdPartyPlatformInfo: {}' > /tmp/config.yaml

sudo cp /tmp/config.yaml /config.yaml
mkdir -p /tmp/logs
cd /home/ligb/ueadmin-api-drogon
pkill -f UEAdminAPI_drogon 2>/dev/null || true
sleep 1
./build/UEAdminAPI_drogon > /tmp/server.log 2>&1 &
sleep 2
echo "=== /system/ping ==="
curl -s http://localhost:8080/system/ping
echo ""
echo "=== /api/system/ping ==="
curl -s http://localhost:8080/api/system/ping
echo ""
pkill -f UEAdminAPI_drogon 2>/dev/null || true
