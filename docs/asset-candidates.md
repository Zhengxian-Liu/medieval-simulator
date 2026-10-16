# 资产候选清单（写实中世纪村庄，Godot 4.7）

调研日期：2026-10-16。只读调研，未购买、未登录。价格取自各 listing 页面/接口里的美元标价（Fab 页面显示的是新台币，接口里的 `priceTierId` 带美元原价，例如 `USD_1799` = $17.99）。**Fab 十月闪购到 2026-10-17 17:00 ET 结束**，页面上的价格可能是折扣价，以结账时为准。

预览图在 `docs/asset-candidates/`，都是 listing 页面上的公开缩略图，已缩到 800px 宽。

## 许可结论（已在官方页面核实）

- **Fab Standard License**（[fab.com/eula](https://www.fab.com/eula)）：可商用；"Use the asset through any compatible tool (use isn't limited to Unreal Engine)"，**可以用在 Godot**；**不需要署名**；不得单独转售或再分发。Personal 和 Professional 两档权利完全相同，区别只是资格：购买时过去 12 个月数字内容行业总收入不超过 10 万美元才能买 Personal，之后收入超了也不用升级。
  - **例外**：Quixel 的 **Megascans 树木资产包**（European Beech、European Hornbeam 等）描述里写着"UE限定内容 - 只允许在基于虚幻引擎的产品中使用"，所以不能用。Megascans **单件**资产（道具、草、灌木，FBX/glTF 格式）用的是普通 Standard License，没有这一条。
  - 只提供 "Unreal Engine" 格式的包，许可本身允许用在其他引擎，但拿不到 FBX，得先装 UE、导入后再导出 FBX。工作量大，材质也要重做，所以都没进候选。
- **CGTrader Royalty Free License**（[terms](https://www.cgtrader.com/pages/terms-and-conditions)）：可以放进游戏商用，没有署名要求，没有份数限制。21A.3 条要求采取"商业上合理的措施"防止终端用户拿到原始资产，所以 Godot 导出时要开 **PCK 加密**。"no AI" 版本只是禁止拿资产训练 AI。
- **Unity Asset Store**（[官方说明](https://support.unity.com/hc/en-us/articles/34387186019988)）：明确写了"You can use Unity Asset Store assets with other engines"，前提是遵守 EULA，不能让别人从成品里提取资产（同样要做 PCK 加密）。注意：Unity 自家发布的资产走 Unity Companion License，只能用于 Unity；下载要经过 Unity Editor/Package Manager。本次没找到 FBX、写实、茅草屋都确认过的 Unity 包，所以候选里没有 Unity 的。
- **Poly Haven**：CC0。但树和草的面数是百万级（例如 `fir_tree_01` 785 万、`grass_medium_01` 160 万），种类也偏南非、海岸、松林，不适合当主力。只适合少量补充：`barrel_03` 1.5k 面、`shrub_03` 1.7 万面、岩石等。

## 排名（最佳组合在前）

| # | 名称 | 价格（Personal / Pro） | 许可结论 | 格式 | 面数 / 贴图 | 覆盖 | 写实程度 | 主要注意事项 |
|---|---|---|---|---|---|---|---|---|
| 1 | **Medieval Village Mega Pack**，作者 Darek Dubiniec，Fab<br>https://www.fab.com/listings/ee749ae2-3707-476c-bf23-0a6072c200d0 | $17.99 / $64.99 | Fab Standard：可商用、可用于 Godot、免署名 ✅ | FBX、.blend、附加文件 | 每个模型 5k–15k 三角面（只有外观）；单张图集，提供 4K/2K/1K 三档，4K 时约 8.8 px/cm | 房屋：民居、谷仓、棚、鸡舍、教堂、风车、酒馆、铁匠铺；墙体有木、篱笆抹泥（wattle and daub）、茅草；屋顶有木瓦和茅草两种。道具：工具、推车、围栏、水井等 | 中高。宣传里写 "low poly"，但预览是 Cycles 渲染的写实 PBR。卖点就是给等距、俯视、城建游戏用，和本项目镜头一致 | 2026-05 才上架，**0 条评价**；描述说的 "1:10 scale" 意思不清，导入后要核对尺寸（unverified）；没有室内；植被有多少不清楚（预览里有灌木）（unverified） |
| 2 | **Medieval Buildings game-ready**（12 件套），作者 Eve13architect，CGTrader<br>https://www.cgtrader.com/3d-model-packs/large-medieval-pack<br>其中免费的 3 栋茅草屋：https://www.cgtrader.com/free-3d-models/architectural/other/medieval-houses-with-thatched-roofs-game-ready | $5.00（标注原价 $10，促销中）；其中 3 栋茅草屋免费 | CGTrader Royalty Free（no AI）：可商用、免署名 ✅；要做 PCK 加密 | 12 件都有 FBX，3 件有 glTF/OBJ，.blend 原生，总共 4.45 GB | 1:1 比例；贴图 2–4K，图集、trim sheet 和平铺材质混用。免费的 3 栋屋共 39,742 三角面，2 张 2K 图集（BaseColor/Normal/Roughness/Metalness/Opacity） | 房屋：木骨架屋（timber-frame）、茅草屋、石教堂、城门塔、城墙、斯拉夫农舍 | 中高，从预览看是写实 PBR | 只有 1 条评价；除免费那件外，其余 11 件的面数没写（unverified）；部分楼有室内。可以先下免费件验证 Godot 导入和观感 |
| 3 | **Quixel Megascans 单件资产**，Fab："Historic Medieval Farm" 系列 + 草和灌木<br>例如：手推车 https://www.fab.com/listings/2d8a2fde-c4c5-449b-8458-cc556c11baba ・木柴堆 https://www.fab.com/listings/f601c8e2-e592-4ce5-bb95-ff7310a4db1e ・桶 https://www.fab.com/listings/718fcede-96fb-4e3f-9d2c-6677d8663327 ・Wild Grass https://www.fab.com/listings/971ee9b9-7680-45f1-84bd-013b5e518b23 ・Boxwood https://www.fab.com/listings/ae90090d-cafb-4431-a74f-8cded15f93e1 | 每件 $0–4.99（Personal）。Farm 系列 $1.99–2.99；Ribbon Grass、Wooden Barrel、Wooden Wheelbarrow 等免费；Wild Grass $3.99；Boxwood $4.99。Pro 价约为 10 倍 | Fab Standard ✅（只用单件资产，**不要用 Megascans 树木资产包**） | FBX、glTF（还有 ZBrush 源等附加文件） | 照片扫描。例如手推车贴图密度 2766 px/m，尺寸 2.2×1.23×0.92 m。每件的面数和 LOD 层级 listing 没写（unverified），贴图分辨率下载时可选（unverified） | 道具：手推车、货车、桶、木柴堆、麻袋、干草叉、犁、粘土炉、凳子、火把、火盆、模块化木围栏、干草捆。植被：草丛、灌木、蕨类、岩石 | 最高（扫描件） | 要一件一件领取；扫描件和第 1、2 项的手工建模放在一起，色调和质感可能不统一，要做一个测试场景来看 |
| 4 | **Game ready forest pack of 13 Oak trees assets**，作者 oleksiy_foliage，Fab<br>https://www.fab.com/listings/7ff289f1-7ff1-4bb8-8c16-ef8c2d6ef138 | $35.99 / $53.99 | Fab Standard ✅ | 只有 FBX | 自称低面数、game ready；叶子图集 2K，树皮 2048×4096；有 Albedo/Normal/Roughness/AO/Translucent/Opacity 贴图。面数没写（unverified） | 植被：13 棵橡树，从幼苗到大树 | 中高（见预览） | 0 条评价；没有风动画和 LOD 的说明（unverified）；只有橡树一个树种。同作者还有 Ash 白蜡树包（$49.99，17 件，带 LOD 和 billboard）和草丛包（$9.99 起），可以补种类 |
| 5 | **Medieval Village – A Middle Ages Environment Pack, Buildings Kit**，作者 PolyGo Studios，Fab<br>https://www.fab.com/listings/4f8a3166-4c15-4109-8188-83b9d498ba08 | $69.99 / $299.99 | Fab Standard ✅ | UE、Unity、Blender（**没有 FBX**，要从 .blend 导出 glTF） | 88 个独立网格；贴图 512–4K | 11 栋成品建筑和模块化墙体（原木、石、灰泥、木板），带道具和植被 | 高（预览接近 UE5 写实场景） | 去重复纹理、油漆层、磨损这些材质功能只有 UE/Unity 版有，Blender 版会丢一部分；Blender 版具体包含多少植被不清楚（unverified）；0 条评价。适合作为备选或升级档 |

预览图：
- 1 → `asset-candidates/darek-village-megapack.jpg`
- 2 → `asset-candidates/cgtrader-medieval-buildings.jpg`
- 3 → `asset-candidates/megascans-medieval-farm.jpg`（Historic Medieval Farm Cart）
- 4 → `asset-candidates/oleksiy-oaks.jpg`
- 5 → `asset-candidates/polygo-medieval-village.jpg`

五张都下载成功了。

## 推荐：先买或先下哪些

**第一批（总价约 $60–75，全部可用于 Godot、免署名）：**
1. **CGTrader 免费的 3 栋茅草屋**（$0）：今天就下 glTF 版，导入 Godot，用 45°、22 m 镜头看看效果。零成本，正好验证整条流程。
2. **Darek 的 Medieval Village Mega Pack**（$17.99）：唯一一个同时覆盖房屋和院落道具、明确面向等距视角、自带 1K/2K 档贴图、提供 FBX 和 .blend 的包，最符合 M2 Pro 16 GB 的预算。
3. **Megascans Historic Medieval Farm 道具和草丛**（约 $15–25，挑 8–10 件）：手推车、桶、柴堆、围栏、干草叉、草丛、灌木。扫描级质感，正好解决 visual-target.md 里"密度"和"边缘杂物"的问题。
4. **oleksiy 13 Oaks**（$35.99）：给温带欧洲场景提供主力树种。

**理由**：这四样一起覆盖了房屋（木骨架、茅草、石）、院落道具和植被三类，格式都是 FBX/glTF，许可已核实可用于 Godot。如果 CGTrader 的 12 件套（$5）在测试里看着不错，可以直接替代或补充 Darek 包的房屋。只有在 10 m 近景下房屋细节明显不够时，才考虑第 5 项 PolyGo（$69.99），或者 Enterables 的 [Enterable Medieval Houses and Cottages v2](https://www.fab.com/listings/7b6d790b-23da-44d0-a089-541f457f3629)（$199.99，UE + FBX，10 栋可进入的房屋，4 级 LOD，只用平铺材质，面数没写）。

**风险**：第 1、2 项是手工建模加图集，第 3 项是扫描件。在 22 m 的等距视角下，统一的光照、AO 和调色通常能压住差异，但最好先搭一个测试场景确认。

**导入 Godot**：FBX 用 Godot 4.7 内置的 ufbx 导入器直接导；也可以在 Blender 里把 .blend 导出成 glTF（更推荐）。Megascans 的 glTF 可以直接导入。

## 看过但没入选

- **Medieval Village – Slavic Huts environment**（Scans Factory，Fab，https://www.fab.com/listings/02fe849e-f57c-4c98-9cbb-b05d65614278）：写实度很高，有 990+ 资产、茅草屋和照片扫描。但**只有 Unreal Engine 格式**，要绕道 UE 导出。
- **Medieval Village (Mega Pack)**（Dexsoft，Fab，https://www.fab.com/listings/a01c7954-32bf-4d02-bf36-b183fcff8a16）：522 个网格，只有 UE 格式。
- **Medieval Village Megapack**（Meshingun，Fab，https://www.fab.com/listings/e49a4e1a-63a4-4472-9026-8c2339164c29）：4.8 分（20 条评价），$179.99，只有 UE 格式。描述说 Unity 商店也有，但本次没找到它的 Unity 页面（unverified）。
- **Medieval Farmstead Builder**（CropCraft）和 **Medieval Village Pack**（FreshCan）：只有 UE/UEFN/Unity 格式，没有 FBX。
- **Megascans 树木资产包**（European Beech / Hornbeam）：标明 UE 限定，**不能用于 Godot**。Megaplants 是 UE 的程序化植被（PVE），格式只有 UE/USD。
- **Medieval Village Houses Pack**（Fab a1b2cb1f…）：标题里写着 "Stylized"，排除。
- **KitBash3D Medieval Siege**（https://kitbash3d.com/products/medieval-siege）：$245，格式很全，但只有搜索摘要，**没打开页面核实许可**（unverified）。官方推荐配置是 32 GB 内存加 8 GB 显存，偏影视级面数，不适合 16 GB 的 M2 Pro。
- **Sketchfab**：只做了一次网络搜索，没找到写实、面数合理的 CC0 或 CC-BY 茅草屋（搜到的一件 18.8 万面，另一件是风格化的）。**没有逐个筛选 Sketchfab 的可下载结果**（unverified）。
- **Synty、Quaternius**：按要求排除，都是风格化或低多边形。

## 未核实项汇总

- 第 1 项：1:10 比例的含义、植被数量、实际导入后的尺寸。
- 第 2 项：除 3 栋免费屋外，其余 11 件的面数。
- 第 3 项：Megascans 单件的 LOD 层级、LOD0 面数、下载时可选的贴图分辨率。
- 第 4 项：面数、有没有 LOD 和风动画。
- 第 5 项：Blender 版包含哪些内容（植被、材质功能）。
- KitBash3D 的许可全文；Sketchfab 没有系统筛选；Meshingun 的 Unity 版本。
