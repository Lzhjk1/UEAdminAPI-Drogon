#!/bin/bash
# ueadmin-api-drogon 配置验证脚本 (无外部依赖版)
# 用法: ./test_config.sh [config_path]

set -e

CONFIG="${1:-$HOME/ueadmin-api-drogon/config.yaml}"
BUILD="$HOME/ueadmin-api-drogon/build/UEAdminAPI_drogon"
TIMEOUT=10
CHECK_PY="$(dirname "$0")/check_config.py"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAILS=$((FAILS+1)); }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
FAILS=0

echo "========================================="
echo " UEAdmin API Drogon - 配置验证"
echo "========================================="
echo "Config: $CONFIG"
echo ""

# ---- 1. 文件检查 ----
echo "--- 1. 文件检查 ---"
if [ ! -f "$CONFIG" ]; then
    fail "config.yaml 不存在: $CONFIG"
    exit 1
fi
pass "config.yaml 存在"

# ---- 2. Python 解析配置 ----
echo "--- 2. 配置字段验证 ---"
if [ ! -f "$CHECK_PY" ]; then
    # 尝试从脚本同目录找
    CHECK_PY="${BASH_SOURCE[0]%/*}/check_config.py"
fi

PYOUT=""
if [ -f "$CHECK_PY" ]; then
    # 在 WSL 里运行 python3
    PYOUT=$(python3 "$CHECK_PY" "$CONFIG" 2>&1)
else
    warn "check_config.py 未找到，跳过字段检查"
fi

if [ -n "$PYOUT" ]; then
    # 解析 YAML 语法
    YAML_STATUS=$(echo "$PYOUT" | grep "^YAML_SYNTAX:" | cut -d: -f2)
    if [ "$YAML_STATUS" = "OK" ]; then
        pass "YAML 语法正确"
    else
        fail "YAML 语法错误: $(echo "$PYOUT" | grep "^YAML_SYNTAX:FAIL:" | cut -d: -f3-)"
    fi

    # JWT
    JWT_S=$(echo "$PYOUT" | grep "^JWT_SECRET:" | cut -d: -f2)
    JWT_I=$(echo "$PYOUT" | grep "^JWT_ISSUER:" | cut -d: -f2)
    [ "$JWT_S" = "OK" ] && pass "JWT 密钥已配置" || fail "JWT 密钥未配置 (custom_config.UserManage.jwt_secret)"
    [ "$JWT_I" = "OK" ] && pass "JWT 签发者已配置" || fail "JWT 签发者未配置 (custom_config.UserManage.jwt_issuer)"

    # DB
    DB_HOST=$(echo "$PYOUT" | grep "^DB_HOST:" | cut -d: -f2)
    DB_PORT=$(echo "$PYOUT" | grep "^DB_PORT:" | cut -d: -f2)
    DB_NAME=$(echo "$PYOUT" | grep "^DB_NAME:" | cut -d: -f2)
    if [ -n "$DB_HOST" ]; then
        pass "数据库配置: $DB_HOST:$DB_PORT/$DB_NAME"
    else
        fail "db_clients 未配置"
    fi

    # Optional
    SMS_CFG=$(echo "$PYOUT" | grep "^SMS_CONFIGURED:" | cut -d: -f2)
    EMAIL_CFG=$(echo "$PYOUT" | grep "^EMAIL_CONFIGURED:" | cut -d: -f2)
    GITLAB_CFG=$(echo "$PYOUT" | grep "^GITLAB_CONFIGURED:" | cut -d: -f2)
    QQ_CFG=$(echo "$PYOUT" | grep "^QQ_CONFIGURED:" | cut -d: -f2)
    WECHAT_CFG=$(echo "$PYOUT" | grep "^WECHAT_CONFIGURED:" | cut -d: -f2)

    [ "$SMS_CFG" = "YES" ] && pass "腾讯云短信已配置" || warn "腾讯云短信未配置，短信功能不可用"
    [ "$EMAIL_CFG" = "YES" ] && pass "邮件服务已配置" || warn "邮件服务未配置，邮件功能不可用"
    [ "$GITLAB_CFG" = "YES" ] && pass "GitLab已配置" || warn "GitLab未配置，用户注册功能不可用"
    [ "$QQ_CFG" = "YES" ] && pass "QQ登录已配置" || warn "QQ登录未配置"
    [ "$WECHAT_CFG" = "YES" ] && pass "微信登录已配置" || warn "微信登录未配置"
fi

# ---- 3. 数据库连通性 ----
echo "--- 3. 数据库连通性 ---"
if [ -n "$DB_HOST" ]; then
    if command -v nc &>/dev/null; then
        if nc -z -w3 "$DB_HOST" "${DB_PORT:-5432}" 2>/dev/null; then
            pass "PostgreSQL 端口可达 ($DB_HOST:$DB_PORT)"
        else
            fail "PostgreSQL 端口不可达 ($DB_HOST:$DB_PORT)"
        fi
    elif command -v pg_isready &>/dev/null; then
        if pg_isready -h "$DB_HOST" -p "${DB_PORT:-5432}" &>/dev/null; then
            pass "PostgreSQL 连接正常"
        else
            fail "PostgreSQL 连接失败"
        fi
    else
        warn "无 nc/pg_isready，跳过数据库端口检查"
    fi
fi

# ---- 4. 启动测试 ----
echo "--- 4. 服务启动测试 ---"
if [ ! -x "$BUILD" ]; then
    fail "编译产物不存在: $BUILD"
    echo "请先编译: cd ~/ueadmin-api-drogon/build && cmake --build ."
else
    # 停掉残留进程
    pkill -f UEAdminAPI_drogon 2>/dev/null || true
    sleep 1

    TMPLOG=$(mktemp /tmp/ueadmin_test.XXXXX.log)
    # 从项目根目录运行，因为二进制硬编码了 ../../../config.yaml
    PROJ_ROOT="$(dirname "$(dirname "$BUILD")")"
    cd "$PROJ_ROOT"
    "$BUILD" > "$TMPLOG" 2>&1 &
    SERVER_PID=$!
    echo -n "等待服务启动"
    ELAPSED=0
    STARTED=0
    CRASHED=0

    while [ $ELAPSED -lt $TIMEOUT ]; do
        sleep 1
        ELAPSED=$((ELAPSED + 1))
        echo -n "."

        if ! kill -0 $SERVER_PID 2>/dev/null; then
            CRASHED=1
            break
        fi

        if grep -qi "Listen\|running" "$TMPLOG" 2>/dev/null; then
            STARTED=1
            break
        fi
    done
    echo ""

    if [ $CRASHED -eq 1 ]; then
        fail "服务启动崩溃!"
        echo "=== 日志 ==="
        cat "$TMPLOG"
    elif [ $STARTED -eq 1 ]; then
        pass "服务已启动 (PID: $SERVER_PID)"

        # ---- 5. API 测试 ----
        echo "--- 5. API 端点测试 ---"
        PORT=$(python3 -c "
import yaml
c=yaml.safe_load(open('$CONFIG'))
lst=c.get('app',{}).get('listeners',[{}])
print(lst[0].get('port',8080) if lst else 8080)
" 2>/dev/null || echo 8080)

        test_endpoint() {
            local url="$1"
            local label="$2"
            local code
            code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 3 "$url" 2>/dev/null || echo "000")
            if [ "$code" = "200" ]; then
                pass "$label -> HTTP 200"
            elif [ "$code" = "404" ]; then
                fail "$label -> 404 (路由未注册)"
            elif [ "$code" = "000" ]; then
                fail "$label -> 无法连接"
            else
                warn "$label -> HTTP $code"
            fi
        }

        test_endpoint "http://localhost:$PORT/system/ping" "/system/ping"
        test_endpoint "http://localhost:$PORT/api/system/ping" "/api/system/ping"
        test_endpoint "http://localhost:$PORT/.well-known/jwks.json" "JWKS"

        echo ""
        echo "--- /system/ping 响应 ---"
        RESP=$(curl -s --max-time 3 "http://localhost:$PORT/system/ping" 2>/dev/null)
        if [ -n "$RESP" ]; then
            echo "$RESP" | python3 -m json.tool 2>/dev/null || echo "$RESP"
        else
            warn "无响应"
        fi
    else
        warn "服务启动超时，但进程仍在运行"
    fi

    # 清理
    kill $SERVER_PID 2>/dev/null || true
    pkill -f UEAdminAPI_drogon 2>/dev/null || true
    rm -f "$TMPLOG"
    echo ""
    echo "--- 已停止测试服务 ---"
fi

echo ""
echo "========================================="
if [ $FAILS -eq 0 ]; then
    echo -e "${GREEN} 全部通过!${NC}"
else
    echo -e "${RED} $FAILS 项检查失败${NC}"
fi
echo "========================================="
exit $FAILS
