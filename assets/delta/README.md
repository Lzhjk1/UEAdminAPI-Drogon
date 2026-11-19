# 差量自动生成资产

服务端在收到新的全量安装包后，用这两个文件在本机解包比对并编译差量安装器：

- `AutoPDMS-project-setup.nsi`：主 NSIS 脚本（`DELTA_MODE` 分支）。
- `UESoftMYRMAgreement.txt`：安装向导许可协议，编译期需要存在。

来源：与 ObjectPRX 打包侧的 `AutoBuildPRX/` 下同名文件保持同步。修改打包侧 NSI 后需要同步到这里，
否则线上生成的差量安装器与全量包的安装段（注册表、快捷方式、-Post 重启、WorkPrj 初始化等）会不一致。

同步命令（在仓库同级目录执行）：

```powershell
Copy-Item ..\ObjectPRX\AutoBuildPRX\AutoPDMS-project-setup.nsi . -Force
Copy-Item ..\ObjectPRX\AutoBuildPRX\UESoftMYRMAgreement.txt . -Force
```

运行时依赖（见 `build_docker/Dockerfile.run`）：`p7zip-full`（`7z` 解包 NSIS 安装器）、
`nsis`（`makensis` 编译 Windows 安装器）。工具路径与资产目录可在 `config.yaml` 的
`Installer.DeltaAssetsDir / SevenZipPath / MakensisPath` 覆盖。
