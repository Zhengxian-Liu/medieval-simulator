#!/usr/bin/env bash
# Builds SimCore (play, chronicle, stats, simcore_tests) into ./build.
# Uses CMake when it is installed; otherwise compiles directly with the system C++ compiler
# (clang++ from Xcode on a Mac), so nothing extra has to be installed.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -z "${SIMCORE_NO_CMAKE:-}" ] && command -v cmake >/dev/null 2>&1; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  exit 0
fi
CXX="${CXX:-$(command -v clang++ || command -v g++)}"
[ -n "$CXX" ] || { echo "找不到 C++ 編譯器。Mac 上請先執行：xcode-select --install"; exit 1; }
mkdir -p build/obj
FLAGS="-std=c++20 -O2 -Iinclude -pthread"
objs=()
for f in src/*.cpp; do
  o="build/obj/$(basename "${f%.cpp}").o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find include -newer "$o" -print -quit)" ]; then
    echo "編譯 $f"
    "$CXX" $FLAGS -c "$f" -o "$o"
  fi
  objs+=("$o")
done
for t in play chronicle stats; do
  "$CXX" $FLAGS "tools/$t.cpp" "${objs[@]}" -o "build/$t"
done
"$CXX" $FLAGS -DSIMCORE_DATA_DIR="\"$PWD/data\"" tests/tests.cpp "${objs[@]}" -o build/simcore_tests
echo "建置完成：build/play"
