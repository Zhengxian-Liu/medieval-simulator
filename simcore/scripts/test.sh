#!/usr/bin/env bash
# Everything that must pass before a change is called done:
#   1. the scripts run under whatever bash runs this file. On a Mac that is the built-in bash 3.2,
#      which differs from bash 5 (a $name right before a Chinese character takes the character into the
#      name; set -u calls an empty array unset), so CI runs this file with macOS's /bin/bash.
#      Elsewhere: BASH32=/path/to/bash-3.2 makes step 1 also run the scripts under that bash.
#   2. unit tests (tests/tests.cpp)
#   3. replay: a game played with a (fake) model, then replayed from its log, must match exactly
#   4. the decider adapter falls back to the rules (-1) when the model cannot be reached
# No network and no API key needed.
set -eo pipefail  # no -u: macOS bash 3.2 treats empty arrays as unset
cd "$(dirname "$0")/.."
mkdir -p build; "$BASH" scripts/build.sh > build/build.log 2>&1 || { cat build/build.log; echo "建置失敗"; exit 1; }
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
echo "1/4 腳本在 $(basename "$BASH") ${BASH_VERSION} 下能跑"
bad=$(LC_ALL=C grep -nE '\$[A-Za-z_][A-Za-z0-9_]*[^[:print:][:space:]]' scripts/*.sh || true)
[ -z "$bad" ] || { echo "失敗：變數後面緊接中文，macOS bash 3.2 會把字吃進變數名（改用 \${名字}）："; echo "$bad"; exit 1; }
smoke() {  # $1 = bash to use; plays one year without and with the model branch
  for key in "" dummy; do
    out=$(OPENROUTER_API_KEY="$key" DECIDER_URL=http://127.0.0.1:9/none "$1" scripts/play.sh --seed 7 --years 1 </dev/null 2>&1) \
      || { echo "$out" | tail -5; echo "失敗：play.sh 在 $1 下出錯"; exit 1; }
    dir=$(echo "$out" | sed -n 's/.*記錄存在 \(saves\/[^ ]*\)\/.*/\1/p' | head -1)
    [ -n "$dir" ] && [ -s "$dir/log.md" ] || { echo "$out" | tail -5; echo "失敗：play.sh 沒寫出記錄"; exit 1; }
    rm -rf "$dir"
  done
}
smoke "$BASH"
if [ -n "${BASH32:-}" ]; then smoke "$BASH32"; echo "   也在 $("$BASH32" -c 'echo $BASH_VERSION') 下跑過"; fi
echo "   通過"
echo "2/4 單元測試"
./build/simcore_tests | tail -1
echo "3/4 模型局的記錄能逐字重播"
fake="python3 $PWD/tests/fake_decider.py"
./build/play --data data --seed 21 --years 30 --npc-model "$fake" --npc-log "$tmp/npc.txt" --out "$tmp/a.md" </dev/null >/dev/null 2>&1
n1=$(wc -l < "$tmp/npc.txt")
./build/play --data data --seed 21 --years 30 --npc-model false --npc-log "$tmp/npc.txt" --out "$tmp/b.md" </dev/null >/dev/null 2>&1
n2=$(wc -l < "$tmp/npc.txt")
[ "$n1" -gt 0 ] || { echo "失敗：假模型一次都沒被呼叫"; exit 1; }
cmp -s "$tmp/a.md" "$tmp/b.md" || { echo "失敗：重播和原局不同"; exit 1; }
[ "$n1" = "$n2" ] || { echo "失敗：重播時又叫了模型（$n1 → $n2 行）"; exit 1; }
echo "   通過（$n1 次決定）"
echo "4/4 模型連不上時退回規則"
out=$(echo '{"who":"x","title":"t","situation":"s","mind":"m","options":["a","b"]}' | DECIDER_URL=http://127.0.0.1:9/none python3 tools/npc_decider.py)
[ "$out" = "-1" ] || { echo "失敗：應該印 -1，實際是 $out"; exit 1; }
echo "   通過"
echo "全部通過"
