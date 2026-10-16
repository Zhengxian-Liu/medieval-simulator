# 中世紀模擬（Medieval Simulator）

14 世紀英格蘭一個家族的幾代人生：克魯賽德國王式的事件、個性和傳承，加上模擬市民式的家人、心情和往來。

- `simcore/`：C++20 的模擬核心和純文字試玩版。說明見 [simcore/README.md](simcore/README.md)。
- `design/`：遊戲設計（[GDD](design/GDD.md)、[架構](design/architecture.md)、[季回合](design/season-loop.md)……）。
- `research/`：工具和 Unreal 的調研。

## 在 Mac 上玩

```bash
git clone https://github.com/Zhengxian-Liu/medieval-simulator.git
cd medieval-simulator/simcore
bash scripts/play.sh
```

之後更新：在 `medieval-simulator` 資料夾裡 `git pull`，再跑一次 `bash scripts/play.sh`（會自動重新編譯）。

每個改動都要通過 `simcore/scripts/test.sh`，GitHub 上的 CI 會在 Linux 和 Mac（內建 bash 3.2）各跑一次。
