#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
OUT_DIR="$BUILD_DIR/test_bins"

rm -rf "$BUILD_DIR"
mkdir -p "$OUT_DIR"

unset LDFLAGS
unset CPPFLAGS
unset CXXFLAGS
unset LIBRARY_PATH
unset CPATH
unset CPLUS_INCLUDE_PATH
unset DYLD_LIBRARY_PATH

# -pthread обязателен: без него std::thread падает в рантайме на Linux
# (std::system_error "Enable multithreading to use std::thread").
# -Werror здесь НЕ ставим: артефакт сдачи не должен ломаться из-за
# безобидного warning на другой версии компилятора (для разработки
# строгая сборка остаётся в verify.sh).
g++ -O2 -std=c++17 -pthread \
  "$ROOT_DIR/src/main.cpp" \
  -o "$OUT_DIR/regex_matcher"

chmod +x "$OUT_DIR/regex_matcher"

tar -czf "$ROOT_DIR/test_bins.tar.gz" -C "$OUT_DIR" regex_matcher
