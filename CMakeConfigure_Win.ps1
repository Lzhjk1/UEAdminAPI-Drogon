# 1. 加载 MSVC x64 编译环境
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && set' |
  ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] } }

# 2. 配置（生成 Ninja 工程）
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=D:/Software/vcpkg/scripts/buildsystems/vcpkg.cmake

# # 3. 编译
# cmake --build build