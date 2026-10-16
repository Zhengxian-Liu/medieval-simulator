#!/usr/bin/env bash
# The browser version: builds if needed, starts a game and opens it in the browser (http://127.0.0.1:8765/).
#   scripts/ui.sh                 新的一局（隨機種子）
#   scripts/ui.sh --seed 7        指定種子；其他參數照樣傳給遊戲（--start 1340、--origin artisan、--gentle……）
# Same model and saves as scripts/play.sh. Close with Ctrl+C in this window.
set -eo pipefail  # no -u: macOS bash 3.2 treats empty arrays as unset
cd "$(dirname "$0")/.."
mkdir -p build; "$BASH" scripts/build.sh > build/build.log 2>&1 || { cat build/build.log; echo "建置失敗"; exit 1; }
seed=""
args=("$@")
for ((i = 0; i < ${#args[@]}; i++)); do [ "${args[$i]}" = "--seed" ] && seed="${args[$((i + 1))]}"; done
if [ -z "$seed" ]; then seed=$((RANDOM * 32768 + RANDOM)); args+=(--seed "$seed"); fi
dir="saves/$(date +%Y%m%d-%H%M%S)-seed$seed"
mkdir -p "$dir"
model=()
if python3 tools/npc_decider.py --has-key; then
  model=(--npc-model "python3 $PWD/tools/npc_decider.py" --npc-log "$dir/npc.txt")
  echo "村民由決策模型決定（pplx-decider）。"
else
  echo "沒有 OPENROUTER_API_KEY，村民由規則決定。要用模型：在 simcore/.env 寫一行 OPENROUTER_API_KEY=你的金鑰（範本 .env.example）。"
fi
echo "種子 ${seed}，記錄存在 $dir/"
python3 tools/ui/server.py -- ${args[@]+"${args[@]}"} ${model[@]+"${model[@]}"} --out "$dir/log.md"
