# 数据库迁移

`db/migrations/*.sql` 按文件名顺序执行，执行记录写在 `schema_migrations`。

## 使用

```bash
export DATABASE_URL='postgresql://user:pass@host:5432/dbname'
./db/apply.sh --dry-run   # 先看要执行什么
./db/apply.sh             # 执行（每个文件一个事务）
```

也可以不用 `DATABASE_URL`，改用 psql 的 `PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD`。

## 规则

- **不要修改已应用的迁移**：`apply.sh` 会校验 checksum，改动会直接报错。要改就新增一个文件。
- 迁移**不在服务启动时自动执行**，由发布流程或 CI 显式运行。
- `0001_baseline.sql` 是仓库此前没有 DDL 时按 ORM 模型反推的基线，全部 `IF NOT EXISTS`，
  在生产库上执行等于 no-op。拿到生产库权限后建议用 `db/dump_baseline.sh` 生成权威版本替换。

## 新增表后

1. 写 `db/migrations/000N_xxx.sql`（增量、可重复执行，用 `IF NOT EXISTS` / `ON CONFLICT`）；
2. 在开发库执行 `./db/apply.sh`；
3. 用 `tools/regen_models.ps1` 重新生成 `models/`，把生成物一并提交。
