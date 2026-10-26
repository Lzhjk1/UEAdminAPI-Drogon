#!/usr/bin/env bash
# 按顺序应用 db/migrations/*.sql，并记录到 schema_migrations。
#
# 用法：
#   DATABASE_URL='postgresql://user:pass@host:5432/dbname' ./db/apply.sh
#   # 或使用 psql 标准环境变量（PGHOST / PGPORT / PGDATABASE / PGUSER / PGPASSWORD）：
#   ./db/apply.sh
#   ./db/apply.sh --dry-run     # 只打印将要执行的迁移
#
# 约定：
#   - 每个迁移文件在**单个事务**内执行，失败即整体回滚；
#   - 已执行的迁移按 checksum 校验，文件被改动会直接报错（不要改已应用的文件，新增一个）；
#   - 不在应用启动时自动迁移，由发布流程/CI 执行。
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
MIG_DIR="$DIR/migrations"
PSQL="${PSQL:-psql}"
URL="${DATABASE_URL:-}"
DRY_RUN=0

for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        *) echo "未知参数: $arg" >&2; exit 2 ;;
    esac
done

select_cmd() {
    if [ -n "$URL" ]; then
        "$PSQL" "$URL" -X -q -v ON_ERROR_STOP=1 "$@"
    else
        "$PSQL" -X -q -v ON_ERROR_STOP=1 "$@"
    fi
}

checksum() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

run_sql_file() {
    if [ -n "$URL" ]; then
        "$PSQL" "$URL" -X -q -v ON_ERROR_STOP=1 --single-transaction -f "$1"
    else
        "$PSQL" -X -q -v ON_ERROR_STOP=1 --single-transaction -f "$1"
    fi
}

check_db() {
    select_cmd -tAc 'select 1' >/dev/null
}

create_table_sql="
CREATE TABLE IF NOT EXISTS schema_migrations (
    version    text PRIMARY KEY,
    name       text NOT NULL,
    checksum   text NOT NULL,
    applied_at timestamptz NOT NULL DEFAULT now()
);"

if [ "$DRY_RUN" -eq 1 ]; then
    echo "[dry-run] 迁移目录: $MIG_DIR"
    for f in "$MIG_DIR"/*.sql; do
        [ -e "$f" ] || continue
        echo "  - $(basename "$f")  $(checksum "$f")"
    done
    exit 0
fi

check_db || {
    echo "无法连接数据库。请设置 DATABASE_URL 或 PG* 环境变量。" >&2
    exit 1
}

select_cmd -c "$create_table_sql" >/dev/null

mapfile -t files < <(find "$MIG_DIR" -maxdepth 1 -name '*.sql' -print | sort)

if [ ${#files[@]} -eq 0 ]; then
    echo "没有找到迁移文件: $MIG_DIR" >&2
    exit 1
fi

applied=0
skipped=0

for f in "${files[@]}"; do
    base="$(basename "$f")"
    version="${base%%_*}"
    sum="$(checksum "$f")"
    recorded="$(select_cmd -tAc "select checksum from schema_migrations where version = '$version'")"

    if [ -n "$recorded" ]; then
        if [ "$recorded" != "$sum" ]; then
            echo "迁移 $base 已应用但文件内容已变化（记录 $recorded，当前 $sum）。" >&2
            echo "请恢复原文件并新增一个迁移，而不是修改已应用的文件。" >&2
            exit 1
        fi
        skipped=$((skipped + 1))
        continue
    fi

    echo "==> 应用 $base"
    run_sql_file "$f"
    select_cmd -c "insert into schema_migrations(version, name, checksum) values ('$version', '$base', '$sum')" >/dev/null
    applied=$((applied + 1))
done

echo "完成：应用 $applied 个，跳过 $skipped 个。"
