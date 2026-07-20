#!/bin/bash
cd ~/drogon
echo "=== 子模块状态 ==="
git submodule status 2>&1
echo ""
echo "=== 系统依赖 ==="
for pkg in libssl-dev libjsoncpp-dev libuv1-dev zlib1g-dev; do
    dpkg -l "$pkg" 2>/dev/null | grep "^ii" | awk '{print $2, $3}' || echo "  $pkg: NOT INSTALLED"
done
echo ""
echo "=== cmake ==="
cmake --version | head -1
echo "=== ninja ==="
which ninja && ninja --version || echo "no ninja"
echo "=== g++ ==="
g++ --version | head -1
echo "=== 编译线程 ==="
nproc
