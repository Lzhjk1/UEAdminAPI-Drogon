# 重新生成 drogon ORM 模型（models/*.h|*.cc）
#
# 前提：本机可访问 PostgreSQL，且 PATH 上有 drogon_ctl（vcpkg 安装 drogon[ctl] 后位于
#       <vcpkg>/installed/x64-windows/tools/drogon/drogon_ctl.exe）。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools/regen_models.ps1 `
#       -DbName ueadmin -DbUser postgres -DbPassword *** [-DbHost 127.0.0.1] [-DbPort 5432]
#
# 说明：
#   - 会写 models/model.json（已在 .gitignore 中，不入库）；
#   - 生成后请用 git diff 检查 models/ 的变化再提交；
#   - 库结构必须与 db/migrations 一致（先跑 db/apply.sh）。
param(
    [Parameter(Mandatory = $true)][string]$DbName,
    [string]$DbHost = "127.0.0.1",
    [int]$DbPort = 5432,
    [string]$DbUser = "postgres",
    [string]$DbPassword = "",
    [string]$Schema = "public"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$modelDir = Join-Path $repoRoot "models"
$modelJson = Join-Path $modelDir "model.json"

$cmd = Get-Command drogon_ctl -ErrorAction SilentlyContinue
if (-not $cmd) {
    Write-Error "未找到 drogon_ctl。请把 vcpkg 的 drogon 工具目录加入 PATH（<vcpkg>\installed\x64-windows\tools\drogon）。"
}

$config = [ordered]@{
    rdbms    = "postgresql"
    host     = $DbHost
    port     = $DbPort
    dbname   = $DbName
    schema   = $Schema
    user     = $DbUser
    password = $DbPassword
    tables   = @()
}
$config | ConvertTo-Json -Depth 5 | Set-Content -Path $modelJson -Encoding UTF8
Write-Host "已写入 $modelJson"

Push-Location $repoRoot
try {
    & $cmd.Source create model $modelDir
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "生成完成。请执行： git diff --stat models/  确认变更后再提交。"
