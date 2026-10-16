# Medieval Simulator：Unreal 5 專案計畫

日期：2026-10-15　｜　決定：改用 Unreal Engine 5（Zhengxian 在決策卡選擇）

## 一、Unreal × Claude 現況（2026-10）

- **UE 5.8 已發佈**，內建官方 **MCP plugin**，Claude 可以直接操作 Blueprint、資產、關卡、材質、模型。Epic 在 State of Unreal 2026 示範過用 Claude 建場景、打光、改 Blueprint。（來源：[State of Unreal 2026 整理](https://knowledge.edge-online.com/p/ai-goes-native-in-unreal-engine-the-five-main-takeaways-from-state-of-unreal-2026)）
- **MetaHuman 改為 MIT 授權**，可免費商用；5.8 新增 MetaHuman Collections 做群眾。（同上來源，未另外核實）
- **UE 5.8 預計是 UE6 前最後一個版本**。UE6 會以 Verse 取代大部分 C++、逐步淘汰 Blueprint，early access 預計明年底，時程不確定。
- 社群 MCP：chongdashu/unreal-mcp、uemcp（不用編譯 plugin）、[UnrealClaude](https://github.com/Natfii/UnrealClaude)（編輯器內整合 Claude Code）、StraySpark（商業）。官方 plugin 出來後，優先用官方的。
- Claude 的強項是 **C++**；Blueprint 是二進位 `.uasset`，Claude 只能透過 MCP 間接讀寫，容易漏掉引用。（[Ludus 文章](https://ludusengine.com/blog/claude-code-for-unreal-engine)，廠商撰寫）

## 二、協作方式

- **遊戲邏輯盡量寫在 C++**，Blueprint 只用在美術/設計師需要調的地方（動畫藍圖、UI 排版、數值資料）。Claude 能完整讀、寫、review C++，也能進 git diff。
- **數值與內容用 DataTable / DataAsset，來源放 CSV/JSON**（文字檔，Claude 可以改、可 diff），在編輯器匯入。
- **模擬核心與畫面分離**（延續 Godot 原型的做法）：村民需求、作息、關係、經濟寫成不依賴 Actor 的 C++ 類別，用 Unreal Automation Tests 測試，可以重播。
- **Claude 在哪裡工作**：
  - 雲端：寫 C++、設計文件、資料表、review。不能編譯或跑 Unreal（引擎太大、需要 GPU）。
  - 你的電腦（Remote Control session）：編譯、跑測試、開編輯器透過官方 MCP 擺場景、截圖檢查畫面。
- **版本控制**：GitHub + Git LFS（`.uasset`、`.umap`、貼圖、模型都走 LFS）。Unreal 專案很快會到數 GB，要注意 GitHub LFS 的容量和流量額度。
- **美術來源**：Fab / Quixel Megascans（自然環境、岩石、植被）、MetaHuman（村民）、Fab 上的中世紀建築包；動畫用 Motion Matching + 動作素材。原創美術之後再決定。

## 三、技術選型

| 項目 | 選擇 |
|---|---|
| 引擎 | Unreal Engine 5.8 |
| 語言 | C++ 為主，Blueprint 為輔 |
| 光影 | Lumen（全域光照）、Virtual Shadow Maps、MegaLights（大量光源，例如火把） |
| 模型 | Nanite（建築、岩石、植被） |
| 角色 | MetaHuman + Motion Matching；大量村民用 Mass（Mass Entity / Mass AI）處理遠景群眾，近景切換成完整角色 |
| 地形 | Landscape 或 5.8 的 Mesh Terrain；植被用 Procedural Vegetation / PCG |
| 水面 | Water plugin（河流、湖、海岸） |
| 物理 | Chaos |
| 鏡頭 | 自寫 C++ 鏡頭系統：縮放值 0–1 連續控制俯角、距離、FOV、景深 |

## 四、第一個原型（目標：驗證鏡頭 + 畫面 + 效能）

1. 建 UE 5.8 C++ 專案、Git LFS、`.gitignore`、CLAUDE.md、官方 MCP plugin。
2. **縮放鏡頭**：滾輪從高空等角俯瞰，連續拉到村民肩後的第三人稱；點村民可以跟隨。
3. 一小塊場景：河流（Water plugin）、地形、Megascans 植被、3–5 棟中世紀房屋（Fab 素材）、日夜循環 + 火把。
4. 5–10 個 MetaHuman 村民，有簡單作息（白天工作、晚上回家）。
5. 效能測試：同時 200 個村民（遠景用 Mass），記錄 FPS。

完成後你來看畫面和手感，決定美術方向再往生活模擬系統推進。

## 五、需要你提供

1. **電腦規格**：你目前連線的是 Mac（`liuzhengxian-work-9`）。Unreal 5 在 Apple Silicon Mac 上能跑 Lumen 和 Nanite，但效能較弱、沒有硬體光追，部分 Fab plugin 只支援 Windows。寫實畫面的主流開發環境是 **Windows + NVIDIA RTX 顯卡**。你有 Windows 電腦嗎？Mac 的型號（M 幾、記憶體多大）？
2. **GitHub repo**：建一個空的 private repo（例如 `medieval-simulator`），之後啟用 Git LFS。
3. **Epic Games 帳號**：安裝 Unreal 和使用 Fab 素材需要。
