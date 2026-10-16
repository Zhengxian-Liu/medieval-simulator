#!/usr/bin/env bash
# Everything that must pass before a change is called done:
#   1. the scripts run under whatever bash runs this file. On a Mac that is the built-in bash 3.2,
#      which differs from bash 5 (a $name right before a Chinese character takes the character into the
#      name; set -u calls an empty array unset), so CI runs this file with macOS's /bin/bash.
#      Elsewhere: BASH32=/path/to/bash-3.2 makes step 1 also run the scripts under that bash.
#   2. unit tests (tests/tests.cpp)
#   3. replay: a game played with a (fake) model, then replayed from its log, must match exactly
#   4. the decider adapter falls back to the rules (-1) when the model cannot be reached
#   5. the browser version (scripts/ui.sh) plays a year through its server the way the page does
# No network and no API key needed.
set -eo pipefail  # no -u: macOS bash 3.2 treats empty arrays as unset
cd "$(dirname "$0")/.."
mkdir -p build; "$BASH" scripts/build.sh > build/build.log 2>&1 || { cat build/build.log; echo "建置失敗"; exit 1; }
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
echo "1/5 腳本在 $(basename "$BASH") ${BASH_VERSION} 下能跑"
bad=$(LC_ALL=C grep -nE '\$[A-Za-z_][A-Za-z0-9_]*[^[:print:][:space:]]' scripts/*.sh || true)
[ -z "$bad" ] || { echo "失敗：變數後面緊接中文，macOS bash 3.2 會把字吃進變數名（改用 \${名字}）："; echo "$bad"; exit 1; }
# Run from a folder with a space in its name, like the user's "Medieval Simulator": a copy with its own build
# (a link would make CMake re-point this folder's build at the temporary path).
spaced="$tmp/有 空格 simcore"; mkdir -p "$spaced"
cp -R CMakeLists.txt data include scripts src tools "$spaced/"
smoke() {  # $1 = bash to use; plays one year without and with the model branch
  for key in "" dummy; do
    out=$(SIM_IGNORE_ENV_FILE=1 OPENROUTER_API_KEY="$key" DECIDER_URL=http://127.0.0.1:9/none "$1" "$spaced/scripts/play.sh" --seed 7 --years 1 </dev/null 2>&1) \
      || { echo "$out" | tail -5; echo "失敗：play.sh 在 $1 下出錯"; exit 1; }
    # With a key the decider must actually run (and fail to reach the model), not fail to start.
    if [ -n "$key" ]; then
      grep -q "連不上決策模型" <<<"$out" || { echo "$out" | tail -5; echo "失敗：資料夾有空格時決策模型沒有跑起來"; exit 1; }
    fi
    dir=$(echo "$out" | sed -n 's/.*記錄存在 \(saves\/[^ ]*\)\/.*/\1/p' | head -1)
    [ -n "$dir" ] && [ -s "$spaced/$dir/log.md" ] || { echo "$out" | tail -5; echo "失敗：play.sh 沒寫出記錄"; exit 1; }
  done
}
smoke "$BASH"
if [ -n "${BASH32:-}" ]; then smoke "$BASH32"; echo "   也在 $("$BASH32" -c 'echo $BASH_VERSION') 下跑過"; fi
echo "   通過"
echo "2/5 單元測試"
./build/simcore_tests | tail -1
echo "3/5 模型局的記錄能逐字重播"
# The launchers hand the model command over the same way, quoted, and the user's folder has a space in its name.
mkdir -p "$tmp/有 空格"; cp tests/fake_decider.py "$tmp/有 空格/"
fake="python3 '$tmp/有 空格/fake_decider.py'"
./build/play --data data --seed 21 --years 30 --npc-model "$fake" --npc-log "$tmp/npc.txt" --out "$tmp/a.md" </dev/null >/dev/null 2>&1
n1=$(wc -l < "$tmp/npc.txt")
./build/play --data data --seed 21 --years 30 --npc-model false --npc-log "$tmp/npc.txt" --out "$tmp/b.md" </dev/null >/dev/null 2>&1
n2=$(wc -l < "$tmp/npc.txt")
grep -qv '^-1' "$tmp/npc.txt" || { echo "失敗：假模型一次都沒回答（指令沒跑起來？）"; exit 1; }
cmp -s "$tmp/a.md" "$tmp/b.md" || { echo "失敗：重播和原局不同"; exit 1; }
[ "$n1" = "$n2" ] || { echo "失敗：重播時又叫了模型（$n1 → $n2 行）"; exit 1; }
echo "   通過（$n1 次決定）"
echo "4/5 模型連不上時退回規則"
out=$(echo '{"who":"x","title":"t","situation":"s","mind":"m","options":["a","b"]}' | DECIDER_URL=http://127.0.0.1:9/none python3 tools/npc_decider.py)
# -1, then after a tab why (for the one warning the game prints).
case "$out" in "-1"|"-1	"*) ;; *) echo "失敗：應該印 -1，實際是 $out"; exit 1 ;; esac
echo "   通過"
echo "5/5 瀏覽器版能玩完一年"
python3 tests/ui_smoke.py "$tmp/ui.md"
echo "全部通過"
