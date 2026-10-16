# 素材工廠：Tripo → Blender → Unreal

日期：2026-10-15　｜　參考：NoMagic（Codex + Tripo + 無介面 Blender）、Zhengxian 分享的 Tripo + Opus + Blender MCP 流程

## 分工

| 素材 | 來源 |
|---|---|
| 主要人物（玩家家族） | MetaHuman |
| 背景村民、動物 | Tripo（人形用統一骨架）——近景品質要實測 |
| 道具、家具、裝備、服裝件、建築小件 | Tripo |
| 地形、岩石、植被、建材 | Quixel Megascans（Fab） |

## 流程

1. **參考圖**：人形做 4 視角參考圖（正、背、左、右），A-pose。道具用 1–2 張參考圖或文字描述。
2. **生成**：Tripo（P2.0 或目前最新版）。人形把頭、身體、頭髮分開生成，每件面數上限先設 25k quads。
3. **綁骨**：Tripo 自動綁骨（humanoid，含手指），固定綁骨版本，所有人形共用同一套骨架。匯出 GLB（含貼圖與基礎動畫）。
4. **先檢查、不修**：在 Blender 裡（無介面腳本或 Blender MCP）檢查並出報告：骨架結構、權重、貼圖連結、腳底接地、比例和軸向、面數、動作幅度。
5. **再修**：依報告修正（缺骨、頭髮權重、手穿進身體等）。修正參數寫進資料表（CSV/JSON），不手動改模型。
6. **整理輸出**：統一單位（公尺）與軸向、原點放底部、減面到預算、做 LOD、產生碰撞體（UCX_）、拆件並加掛點（socket）、匯出 FBX 到 `Art/Source/`。
7. **匯入 Unreal**：用命令列匯入 `Content/`；人形用 IK Retargeter 對應到 UE Mannequin，才能套用 Fab 動畫。
8. **比對靜止姿勢**：碰動畫之前，先比對 Unreal 裡的 rest pose 和原始模型是否一致。
9. **自動截圖**：在 Unreal 裡自動截圖，給你選最好的版本並簽核。

## 規則

- **大部分 bug 出在工具之間，不在工具裡面**：每一次交接（Tripo→Blender、Blender→Unreal）都要有自動檢查。
- Claude 不從零建模，只負責檢查、修正、整理和匯出。
- 你負責挑選版本和最後簽核。

## 前置條件

- Tripo API key：你在 Tripo 後台產生，存成 Mac 上的環境變數，不進 git。
- 若要商用：Tripo 付費方案（免費版不能商用）。
- 第一個測試：木桶、斧頭各一個，看寫實風格的品質夠不夠。
