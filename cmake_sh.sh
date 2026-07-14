cmake . \
	-DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
	-G Ninja -B build \
	-DCMAKE_BUILD_TYPE=Release \
  	-DCMAKE_C_COMPILER=gcc-16 \
  	-DCMAKE_CXX_COMPILER=g++-16

echo "cmake done"
echo "then run 'cmake --build build' to build"
