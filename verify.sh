#!/usr/bin/env bash
# Проверка исправленного regex_matcher против эталона (Python re.findall).
# Запуск:  bash verify.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../regex_project /src/main.cpp"
DATA="$HERE/example/data"
REF="$HERE/example/regex_matcher"
BIN="$HERE/regex_matcher_fixed"

echo "==> 1/4  Сборка $SRC"
g++ -O2 -std=c++17 -Wall -Wextra -Werror "$SRC" -o "$BIN"
echo "    OK (без предупреждений)"

echo "==> 2/4  Запуск на данных примера (987 регулярок x 200 строк)"
time "$BIN" "$DATA/re" "$DATA/text" "$HERE/out_fixed"

echo "==> 3/4  Сравнение с эталонным ответом example/data/out"
if diff -q "$DATA/out" "$HERE/out_fixed" >/dev/null; then
  echo "    ✅ PASS: 0 расхождений с эталоном"
else
  echo "    ❌ FAIL: есть расхождения (первые строки):"
  diff "$DATA/out" "$HERE/out_fixed" | head
  exit 1
fi

echo "==> 4/4  (опционально) Перегенерация эталона через python3 и сверка"
if command -v python3 >/dev/null 2>&1; then
  python3 "$REF" "$DATA/re" "$DATA/text" "$HERE/out_pyref"
  if diff -q "$HERE/out_pyref" "$HERE/out_fixed" >/dev/null; then
    echo "    ✅ совпадает с python re.findall"
  else
    echo "    ⚠️ отличается от python re.findall"
  fi
else
  echo "    (python3 не найден — шаг пропущен; шаг 3 уже подтвердил корректность)"
fi

echo "ГОТОВО."
