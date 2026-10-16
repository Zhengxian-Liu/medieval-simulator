# Claude × 遊戲開發：工具、Skills 與 Tech Stack 調研

日期：2026-10-15　｜　專案：Medieval Simulator

## 資料來源說明

- **X (Twitter)**：直接抓取 x.com 被擋（robots / 需登入），所以 X 的內容是透過搜尋引擎索引到的推文標題與摘要取得，無法看到完整討論串與回覆。
- **補充來源**：工具官方 repo / 文件、Unity 官方部落格與文件、幾篇評比文章。注意：StraySpark、Summer Engine、Ziva 的文章都是**賣自家工具的廠商**寫的，結論已扣掉自我推銷的部分。

## 一、X 上大家在分享什麼

| 推文 | 重點 |
|---|---|
| [@majidmanzarpour](https://x.com/majidmanzarpour/status/2023211517370343882) | Claude Code + Godot 一小時做出跨平台手遊。重點是 **Godot 的 CLI 讓 Claude 能直接操作引擎**，再搭配 godot-mcp 做除錯。 |
| [@asynkimo](https://x.com/asynkimo/status/2072670419400728929) | 用 Claude 從零做出完整 Godot 遊戲，組合是：**遊戲設計文件 (GDD) + Godot MCP + SpriteCook MCP（生成 sprite）**。 |
| [@ClaudeCode_UT](https://x.com/ClaudeCode_UT/status/2067487476491669790) | Claude Code + Unity 6 + Unity MCP，4 週把 VR 遊戲上架 Meta Horizon Store。 |
| [@unity_japan](https://x.com/unity_japan/status/2059517889041105175) | Unity 官方教學：Unity MCP 接 Claude Code / Codex / Copilot。 |
| [@minchoi](https://x.com/minchoi/status/2012678452067835943) | 推薦 3 個 MCP：Blender MCP、Unity MCP、Unreal MCP（3D 場景用 prompt 生成）。 |
| [@JustinPBarnett](https://x.com/JustinPBarnett/status/1901957423851557035) | Unity MCP 作者，一個 prompt 做 Mario clone（早期 demo，現在專案為 CoplayDev/unity-mcp）。 |
| [@DanKornas](https://x.com/DanKornas/status/2103042746135040245) | 推薦 **Claude Code Game Studios** 模板：用多個專門 agent 組成「工作室」流程。 |

**共識趨勢**：
1. 「**CLI 能跑的東西，agent 才能自主跑**」，這是 Godot 被大量推薦的主因。
2. 成功案例幾乎都是「**設計文件 + 引擎 MCP + 素材生成工具**」三件套。
3. Unity 也有大量成功案例，但多半是已經熟 Unity 的團隊。

## 二、引擎 MCP 伺服器（讓 Claude 直接操作編輯器）

### Godot（社群最活躍）
| 工具 | 說明 | 成本 |
|---|---|---|
| [hi-godot/godot-ai](https://github.com/hi-godot/godot-ai) | 約 3k stars、MIT。46 個工具：建場景、改節點/腳本、接 signal、UI、材質、動畫。GDScript 完整支援（語法檢查 + hot reload），**C# 只能寫檔不能編譯回報**。需 Godot 4.7+。 | 免費 |
| Coding-Solo/godot-mcp | 輕量：開編輯器、跑專案、抓 debug 輸出、建場景。程式碼小、好讀。 | 免費 |
| GDAI MCP | 編輯器外掛，多了截圖與模擬鍵盤輸入（可讓 Claude「看」遊戲畫面）。 | 一次性 $19 |

### Unity
- **官方**：Unity 已把 Editor 內的 MCP server 標為 deprecated，改用 **Unity CLI**（`unity mcp configure <client>`），免費、需 Unity 6.0 LTS+。（[Unity 文件](https://docs.unity3d.com/Packages/com.unity.ai.assistant@2.0/manual/unity-mcp-get-started.html)、[Unity 部落格](https://unity.com/blog/unity-ai-mcp-how-to-get-started)）
- **社群**：CoplayDev/unity-mcp（MIT，支援 2021.3–6.x）。
- 缺點：headless build 設定較麻煩、授權政策歷史上有爭議。

### Unreal
- 開源：chongdashu/unreal-mcp（約 70 個工具）。另有商業版本。
- 生態最成熟但最重；C++ 編譯循環長，對 agent 迭代速度最不友善。對小團隊模擬遊戲來說殺雞用牛刀。

### Web（Phaser / Three.js）
- 不太需要專門 MCP，Claude 直接寫 JS/TS 就行，搭配 Playwright 做截圖驗證。
- 有現成 skills：Phaser、three.js 遊戲相關 skill（[claudemarketplaces.com](https://claudemarketplaces.com/skills/opusgamelabs/game-creator/phaser)）。
- 適合快速原型，但大型模擬（大量單位、存檔、模組）長期來說工具鏈要自己補。

### 素材 / 美術
- **Blender MCP**（ahujasid/blender-mcp）：用 prompt 操作 Blender 建模。
- **SpriteCook MCP** 等 sprite 生成工具：2D 美術佔位素材。

## 三、Skills / Plugins / 工作流模板

| 名稱 | 類型 | 說明 |
|---|---|---|
| [Claude Code Game Studios](https://themenonlab.blog/blog/claude-code-game-studios-48-agents-model-tiers-org-chart)（Donchitos，約 2k stars） | `.claude/` 模板 | 48 個 agent（創意總監、技術總監、關卡/經濟設計師、QA…）、37 個 slash command、8 個 hooks、29 份文件模板。**適合借鑑結構，不建議整包照搬**（48 個 agent 對小團隊太重）。 |
| [zate/cc-godot](https://cdn.jsdelivr.net/gh/zate/cc-godot@main/README.md) | Claude Code plugin | Godot 開發 plugin。 |
| [fenixnix/Godot-Skills](https://github.com/fenixnix/Godot-Skills)、godot-gdscript-patterns、godot-testing-patterns | Skills | GDScript 慣例、測試模式（GUT）等。 |
| GodotPrompter | Skills/plugin | Godot 專用提示集合。 |

**一個值得學的實戰案例**：[Headless Game Development with Claude Code and Godot](https://thebrandonwu.substack.com/p/building-a-visual-novel-with-claude)
- Docker devcontainer 裡裝 headless Godot + git + gh，push 到 main 自動 build 並部署到 itch.io。
- 用「PM 角色」決定優先順序、「目標玩家角色」做試玩評論。
- 最大收穫：**讓 Claude 自己做開發工具**（劇情節點編輯器、角色卡生成器）。

## 四、引擎比較（針對 Claude 協作）

| 面向 | Godot 4 | Unity 6 | Unreal 5 | Web (TS + Phaser/Three) |
|---|---|---|---|---|
| Claude 能否純 CLI 自主跑（build/test/run） | ★★★ 最佳 | ★★ 需設定 | ★ 慢 | ★★★ |
| 場景/資源是否為可讀文字 | ★★★ (.tscn/.tres) | ★★ (YAML，較冗長) | ★ 二進位為主 | ★★★ |
| MCP 生態 | ★★★ 多且活躍 | ★★★ 官方 CLI + 社群 | ★★ | 不需要 |
| 2D/2.5D 模擬類適配 | ★★★ | ★★★ | ★ | ★★ |
| 大量單位效能 | ★★（可用 C#/GDExtension 補） | ★★★（DOTS） | ★★★ | ★ |
| 授權 / 成本 | MIT 免費 | 有收費門檻 | 營收分成 | 免費 |
| 引擎體積 / 雲端容器可跑 | 小、易放進容器 | 大 | 很大 | 最小 |

## 五、建議

### Tech stack：**Godot 4.x + GDScript（核心模擬可視需要改 C#）**

理由：
1. **最適合 agent 協作**：場景與資源是文字檔，Claude 能直接讀寫、git diff 可審；headless CLI 能跑 build 與測試，Claude 可以在雲端容器裡自己驗證，不需要人一直點編輯器。
2. **中世紀模擬的需求**（村莊/經濟/人口/資源系統、大量資料表、存檔）大多是資料與邏輯，正是 Claude 最強的部分；Godot 2D/2.5D 足夠且輕量。
3. X 上 Claude + 遊戲開發的分享以 Godot 最多，MCP 與 skill 生態成熟、免費。
4. 注意：godot-ai 對 GDScript 支援最完整，C# 只有寫檔；若之後模擬規模大到需要效能，再把核心模擬層抽到 C# 或 GDExtension。另外 Godot 4 的 C# 專案目前無法匯出 Web。

**何時改選其他**：團隊已經精通 Unity → 選 Unity 6 + 官方 Unity CLI MCP；只想做瀏覽器小原型 → TS + Phaser。

### 建議工具組合
- **Claude Code**（主力）+ **godot-ai MCP**（編輯器操作）
- **GUT** 測試框架 + headless Godot：讓 Claude 寫完自己跑測試
- 一個 Godot skill（例如 cc-godot 或 Godot-Skills）放 GDScript 慣例
- 需要時：Blender MCP（3D）或 sprite 生成工具做佔位美術
- GitHub Actions：push 自動 headless build（之後可加 itch.io 部署）

### 建議協作方式
1. **先寫文件再寫碼**：`docs/GDD.md`（遊戲設計）、`docs/systems/*.md`（每個模擬系統一份：經濟、人口、農業、戰爭…）、`CLAUDE.md`（程式慣例、資料夾結構、怎麼跑測試）。
2. **借鑑 Claude Code Game Studios 的結構，但精簡**：只留 3–4 個角色（設計、工程、QA、平衡/經濟），不需要 48 個。
3. **模擬邏輯與畫面分離**：模擬核心寫成純邏輯（可在 headless 測試、可用確定性 seed 重播），Godot 場景只負責顯示。這讓 Claude 最能自主迭代。
4. **專案裡分工**：你決定設計方向與手感；Claude 負責系統實作、測試、資料表、工具（例如事件編輯器、平衡試算表）。每個功能一個 thread / PR。
5. 第一步建議：建 repo、跑一個 headless 可測的最小原型（一個村莊、時間推進、一種資源），驗證整條工具鏈。

## 六、方向更新：等角視角（isometric）+ sprite 美術（2026-10-15）

結論不變，仍建議 Godot 4，而且這個方向讓 Godot 更合適：

- **引擎原生支援**：Godot 4 的 `TileMapLayer` 內建 isometric 格子形狀，加上 Y-sort 處理前後遮擋，2D 渲染管線很輕，不需要 3D。
- **常見坑**：多層 TileMapLayer 疊加時的 Y-sort 排序、建築佔多格時的遮擋、y_sort_origin 設定（Godot 論壇大量相關問題，例如[多層 TileMapLayer + YSort](https://forum.godotengine.org/t/best-practice-for-stacking-tilemaplayer-nodes-with-ysort-in-godot-4-4/120306)）。建議一開始就定好：地形一層、建築/單位用 Y-sort 的 Node2D，並寫進 CLAUDE.md。
- **Skill**：[godot-tilemap-mastery](https://skillselion.com/skills/thedivergentai/gd-agentic-skills/godot-tilemap-mastery)（gd-agentic-skills 系列）可給 Claude 參考 TileMap 寫法。
- **Sprite 生成**：**[PixelLab MCP](https://www.pixellab.ai/mcp)** 支援 Claude Code，可直接生成 isometric tiles、4/8 方向角色與動畫（走、跑、待機、攻擊）、tileset（Wang / dual-grid / 3x3）。需付費訂閱，有免費試用額度。適合先做佔位美術，正式美術可之後替換或由美術師接手。
- **美術規格要先定**（寫進 `docs/art-style.md`）：tile 尺寸（例如 64×32 菱形）、角色方向數（4 或 8）、像素密度、調色盤。規格固定後，AI 生成與人工美術才能混用。
- **建議起手**：先用簡單色塊佔位 tile 把等角地圖、相機、點選格子、建築放置跑通，再接 PixelLab 換素材。

## 七、再更新：要高精度光影、水面效果、物理（2026-10-15）

需求變成「sprite 美術 + 高品質光影/水面/物理」，這就是 **HD-2D** 風格（如 Octopath Traveler、Triangle Strategy）。做法要從「純 2D TileMap」改成「**3D 世界 + 正交等角相機 + sprite**」，第六節的 TileMapLayer 做法不再適用。

**Godot 4 做得到，作法：**
- 地形、水面、建築底座用 3D 低面數 mesh（可貼像素材質）；角色、樹木、道具用 `Sprite3D`（billboard 或固定朝向）。
- 相機用 `Camera3D` 正交投影，固定等角角度（約俯角 30°、水平 45°）。
- 光影：Forward+ 渲染器的即時陰影、SDFGI / VoxelGI 全域光照、體積霧、SSAO、bloom、glow。
- 水面：自訂 shader（波動、折射、反射、岸邊泡沫）；Godot 的 shader 是文字檔，Claude 很容易寫和調。
- 物理：3D 物理用 Jolt（較新版本內建），可做落石、破壞、布料、漂浮物等。
- sprite 要吃到光：給 sprite 做 normal map（PixelLab 等工具或手繪），否則會像紙板貼在場景裡。

**誠實評估：**
- 光影天花板 Unreal（Lumen）較高，Octopath 本身就是 Unreal 做的。但 Unreal 對 Claude 協作最不友善（C++ 編譯慢、資源多為二進位、引擎龐大），小團隊不划算。
- Godot 的 GI 屬於「風格化遊戲夠用」等級，不是寫實 AAA 等級；對 sprite 風格來說足夠。
- 主要風險是效能：大量單位 + 即時陰影 + GI。需要早期做效能測試（例如 500 個村民 sprite 在場上）。
- Claude 的限制：光影「好不好看」需要人眼判斷。Claude 可以寫 shader 並用截圖驗證，但美感調整要你給回饋。

**建議**：仍選 Godot 4（Forward+ 渲染器），第一個原型改成「3D 等角場景 + 一片水面 + 幾個 sprite 角色 + 動態光源（日夜循環）」，先驗證畫面效果與效能，再往模擬系統推進。
