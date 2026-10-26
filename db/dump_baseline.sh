#!/usr/bin/env bash
# 从现有数据库导出权威基线，替换手写的 0001_baseline.sql。
#
# 必须满足：目标库尚未应用 0002 之后的迁移（即不含 team / payment_order 等新表），
# 否则导出的基线会与新迁移冲突。通常从生产库（或从生产恢复的克隆）导出。
#
# 用法：
#   DATABASE_URL='postgresql://user:pass@host:5432/dbname' ./db/dump_baseline.sh
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="$DIR/migrations/0001_baseline.sql"
URL="${DATABASE_URL:-}"

if [ -z "$URL" ]; then
    echo "请设置 DATABASE_URL。" >&2
    exit 1
fi

if [ -f "$OUT" ]; then
    backup="$OUT.bak.$(date +%Y%m%d%H%M%S)"
    cp "$OUT" "$backup"
    echo "已备份原基线到 $backup"
fi

pg_dump "$URL" \
    --schema-only \
    --no-owner \
    --no-privileges \
    --exclude-table=schema_migrations \
    > "$OUT"

echo "已生成 $OUT"
echo "请检查内容（尤其 varchar 长度、默认值、约束），确认后提交；"
echo "如果该库已经应用过迁移，请先删除 schema_migrations 的记录或改用全新克隆库。"
