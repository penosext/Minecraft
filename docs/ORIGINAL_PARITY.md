# 原代码核对与复刻范围（含1.0.10物品状态修正）

固定参考：`gstaaij/minecraft-console@09df8928eeb15328bca499d225e48ea95af6d119`。

本文区分三件事：原始函数/参数、词典笔平台适配、尚未复刻的完整系统。**测试自洽和源字节校验不等于取得了原版运行帧或世界输出的外部等价认证。**

## 1. 原世界生成链

原 `RandomLevelSource.cpp` 的主路径不是一条高度公式：

1. 构造 Random 与各组 noise，保留随机消耗次序。
2. `prepareHeights/getHeights` 读取粗尺度biome，生成基础密度、石头和水。
3. 最终biome查询及 `buildSurfaces` 生成基岩、表层/填充层、冰/砂岩等。
4. 洞穴/峡谷和结构阶段。
5. `postProcess` 中的结构后处理、湖泊、地牢、Biome装饰、生物及冰雪。

当前已接入1–3，原村庄/Scattered建筑选址与模板提取，及树木/植物Feature子集。完整洞穴/峡谷、完整装饰前置随机消耗和所有生成阶段未移植。

### 世界大小

`Minecraft.World/ChunkSource.h:8–21`：Classic54、Small64、Medium192、Large320 chunks，对应864/1024/3072/5120方块。大世界创建菜单默认Large；不是所有版本统一默认864。

`Level.h:74–100`：基础生成深度128，建造高度256，海平面63，一天24000ticks。原坐标以世界中心为零，应用逻辑坐标通过 `source = logical - size/2` 转换。

### 群系、树和建筑

- `biomes/` 使用原Layer/Voronoi流程，真实raw/zoomed查询，非按坐标手选颜色。
- `terrain/` 保留原密度/表层函数字节。
- `structures/` 保留原村庄拼接、选址和建筑模板，以及原沙漠神庙、丛林神庙、沼泽小屋模板。
- `decoration/` 提取原Tree、BasicTree、Birch、Pine、Spruce、MegaTree、SwampTree、GroundBush及部分植物，原树种/数量规则和metadata保留。

**差异：**结构采用canonical whole-piece生成后裁剪，不是原逐chunk后处理调度；树木采用装饰子集调度，省略其他前置阶段的RNG消耗。因此不能承诺同种子每棵树位置、整村落地细节与完整原控制台世界逐块相同。

新建v3世界启用装饰，v1/v2和旧自写世界保留原基准；不静默向旧图加树。

## 2. 第一人称右手的位置、大小与动画

原来源：

- `Minecraft.Client/ItemInHandRenderer.cpp:683–739` 空手路径。
- `PlayerRenderer.cpp:523` 起的 `renderHand`。
- `HumanoidModel.cpp` 的arm0构造、setupAnim。
- `Polygon.cpp` 的UV赋值和0.1 texel内缩。
- `GameRenderer.cpp` 手部投影与分屏参数。

实际实现的原始段与数值在 `jsapi/build/generated/OriginalFirstPerson.manifest.json` 自动记录，附Git blob、SHA-256、源字节范围与生成代码校验。

右臂盒体为4×12×4，局部原点(-3,-2,-2)、pivot(-5,2,0)，原静态z旋转0.1rad。空手按原OpenGL后乘顺序计算4×4矩阵，手部70°垂直FOV，aspect使用实际视口；不再用 `w*0.79/h*0.13` 等目的摆放值。

960×266站立空手原路径的本地参考包围框为 `(577,187)–(670,265)`，5536可见像素。袖口位于该视口下方是原变换输出，不能为了露袖而擅自调整。参考图在 `dist/reference-firstperson-1.0.8/`。

摆臂进度按 `LivingEntity.h` 默认SWING_DURATION=6、20TPS推进；原动画使用Mth查表。当前未实现完整角色步态插值、受伤镜头、立体/缩放、所有皮肤及持物使用行为。

持物仅支持完整cube/grass方块，按原Windows64 item分支矩阵（swing exponent1）及原中心化；特殊形状/工具/地图等不伪装成完整支持。人物面拓扑与CPU填充/光照仍是平台实现，没有原GPU帧对照，不承诺pixel-perfect。

## 3. 物品栏

原规则：

- `Minecraft.World/Inventory.cpp:11–29`：36主槽、4armor、9快捷槽，selected默认0。
- `InventoryMenu.cpp:29–62`：主包/快捷槽/armor与合成槽布局。
- `Gui.cpp:93–94,334–340,643–655`：182×22快捷栏、24×22选中框、槽距20、16×16物品图标。
- `ItemInstance.cpp:172–179`：实际物品最大堆叠委托给Item，不能所有Item强制64。

1.0.10库存按原`Item ID + 合法aux`保存；每物品使用实际堆叠上限，门为1件，已支持的普通方块多为64。默认空包。保留原PNG裁取和布局比例，36+4状态、9快捷栏、分堆/合并/交换、创造填槽、选槽、原子存档均已实现。

`tools/generate-items.py`核对原`Item.cpp/Tile.cpp`注册、各Item子类的aux/堆叠行为和`Common/UI/IUIScene_CreativeMenu.cpp`创造清单，生成126个当前已覆盖物品变体、125个创造项和全部312个渲染状态分类。未覆盖原清单物品另列，不把当前目录称为完整原版物品表。物品索引与渲染索引独立，过滤创造项不重新编号。

楼梯方向/倒置、台阶上下、门上下/开关/铰链、作物年龄、原木轴向等不作为背包物品；羊毛颜色、木种、砂岩/石砖和台阶材料aux保留。库存图标与持物只取canonical物品代表，不保存任意旧朝向。v1只在内存迁移，明确保存才写v2；无法映射的旧条目和超上限余量保留原槽/key/count的受保护记录，不可选择放置，不丢数量。

`ItemPlacement.cpp`按原`TileItem::useOn`的item aux到世界data链路核对已审规则，以命中面/点击高度/玩家朝向生成状态，采用source yaw等于负World yaw。两格门先验证全部单元并批量发布，不能半扇成功。`tools/extract-placement.py --check`核对源函数、常量和注册依据；这仍是审计过的规则子集，不是完整原Tile engine、tick/neighbor/drop调度。旧World::place渲染索引接口仅为兼容，实际应用改用World::placeItem。

门、种子、线、红石粉、甘蔗、可可豆、中继器、地狱疣、炼药锅、花盆、胡萝卜与马铃薯这13项使用原`items.png`的16×16 sprite。`tools/generate-item-icons.py`核对原Item图标名、PreStitchedTextureMap格位及PNG/blob/RGBA哈希，按canonical key查图，不显示门下半/作物阶段等世界模型。固定提交的GitHub contents API已核对该PNG路径和blob，未做提交签名认证。其余普通方块图标仍是CPU代表模型；这不表示完整原ItemRenderer或特殊物品手持分支已移植。

**适配与未完成：**960×266使用1×布局是平台选择，不是已复刻原控制台GUI缩放、safe-zone或Iggy UI。传统 `InventoryScreen::renderBg` 本身是 `#if 0 / 4J Unused`，这里是其原纹理与槽位布局复刻，不是完整console UI运行时。方块缩略图/数字是平台绘制；未提供全部工具、合成、装备效果、耐久、掉落与完整Item继承体系。

## 4. 太阳、月亮与相机

原 `LevelRenderer::renderSky`：`Ry(-90°) * Rx(timePhase*360°)`，太阳中心Y=100、半尺寸30，月亮Y=-100、半尺寸20；使用原太阳/月相纹理quad和 `SRC_ALPHA, ONE` 加法混合。太阳不通过解析圆判定。

1.0.8按该quad采样原纹理，保留8月相。原太阳PNG黑背景不能擅自改透明；使用原混合消去黑色贡献。

原 `GameRenderer::setupCamera/getFov` 使用垂直FOV，站立基础值70°。世界和生物已同步同焦距；手部另有原投影。跑动FOV、伤害/水中镜头及所有Options未完整接入。

## 5. 遮挡光照

原游戏的方块变暗来自天光/方块光传播、面亮度和AO，并非“给太阳加一个现代shadow-map”即可复刻。

当前依据原Tile构造表取opacity/emission，缓存玩家窗口的15级天光和方块光，配合邻面AO与半砖/楼梯/耕地的上+四侧邻居最大亮度规则。缓存以世界路径、窗口、数据指针、revision失效，挖开顶棚会更新。

通过：遮顶变暗、挖洞变亮、火把/岩浆/萤石发光、水/叶/冰衰减、昼夜、AO、内缩半砖、39.5/40.5距离、缓存与输出护栏。

**限制：**这是96×256×96窗口光照子集，不是完整原Level异步光照、天气、所有材质/连接状态的逐像素等价实现。窗口边界和CPU光照填充仍可能与原运行时不同。

## 6. 攻击、挖掘、放置距离

源码直接核对：

- `CreativeMode.cpp:110–127`：方块5，hasFarPickRange=true。
- `SurvivalMode.cpp:164–166`：本地生存方块4。
- `MultiPlayerGameMode.cpp:244–250`：联机创造5，非创造4.5。
- `GameRenderer.cpp:250–359`：实体选取far=6，普通限制3；另扩张实体pick radius。
- `Entity.cpp:1688–1690`：默认pickRadius0.1。

当前应用是创造式方块交互，使用方块5、far实体选取6及0.1扩张。旧Scene API默认3仍供普通模式/测试，调用者显式选择far。

这不等于已实现原服务端许可范围、网络验证、装备伤害、暴击与全部生存交互。World方块拾取仍是cube-cell DDA，不是所有复杂方块的精确轮廓选择；该差异在存档状态中明确列出。

## 7. 玩家碰撞与后台区块所有权（1.0.11）

`BlockCollision`独立于渲染geometry，按原Tile的`getAABB/addAABBs`核对局部碰撞箱；草植物/Bush、火把及液体没有阻挡AABB，而草方块是实心。梯子原版并非完全无箱：按朝向保留2/16贴墙薄箱，格内其余空间可通行。半砖上下、楼梯邻接组合、门上下/铰链、栅栏1.5高等采用已审子集，候选查询包含人物脚下方一格，避免漏碰延伸箱。

碰撞不再借用`solid()`的选取/占格判定，所以可穿过的植物与火把仍可挖掘。梯子/藤蔓的脚所在格判断及限落/实际横碰攀爬按原LivingEntity规则作秒制适配；没有宣称运行完整Entity travel。玩家当前宽0.5、高1.8保留；原Player正常宽0.6未在此轮无提示改变。水流/完整液面、游泳/氧气/岩浆伤害等仍未移植；Mob仍使用其独立轻量物理。

后台补载不再借用活动World。复制请求包含实例epoch、种子、生成基准配置及区块key，worker独占另一个生成context，只返回baseline和biomes。主线程短暂接收，校验epoch/目标/当前缓存及最新overlay，分配与校验完成后发布。旧目标/旧世界结果不会覆盖当前世界；保存与画面可在worker运行期间继续。切换世界/销毁的显式join/discard不同于正常游戏帧，不以全局大锁换取安全。

## 8. 区块范围、阴影与设备GPU核查（1.0.13）

本地固定源码中能核实：`Level.cpp:3241–3285` 是中心向外的区块tick轮询，而非地形加载队列；`LevelRenderer.cpp:1942–2044` 是近距离优先的脏渲染块处理，`2543–2593` 为绘制视锥剔除。给定来源中没有完整的ServerChunkCache/MultiPlayerChunkCache实现，因此不能将平台预载队列、线程数或两个半径的默认值称为原版逐行移植。

设置中的显示/预加载两个半径各为1–3，预加载不得小于显示。显示决定窗口48/80/112宽，预加载决定9/25/49目标区块。先准备中心和显示范围，再后台准备外圈；隐藏预载只更新缓存，不触发显示窗口光照重建。这是维持原生成结果与安全线程所有权的平台调度，未扩大存档或改变生成基准。

阴影继续采用原Tile不透明度/发光表、15级天光与方块光和AO子集，不添加shadowmap。`Level::checkLight`在`3401–3451,3466–3660`提供分层、先消光后传播及局部距离限制；`LevelChunk.cpp:546–651,695–793`提供顶到底天光与gap实现。但`Level.cpp:3304–3307`中的`lc->tick()`已注释，不声称这一版本存在已验证运行的异步gap tick队列。当前挖掘/放置邻域立即刷新，视线附近先更新，远处通过有预算重建最终收敛；局部刷新不会冒充全窗已完成。未知区块通过loadedColumns屏蔽天光/块光及顶部采样，避免将未加载的空气占位当作光源。首建和窗口换原点仍可能同步花费时间，并不承诺完全无卡帧。

设备实查为RK3562/Mali-Bifrost，`/dev/mali0`及内核驱动存在；但搜索和运行时`dlopen`均未找到EGL/GLES/libmali用户态库。DRM/RGA显示/二维合成及RKNPU节点不等于可用3D渲染。未建立EGL context，未取得GL_RENDERER，未启用GPU世界渲染，未改系统库或接管显示。只读探针结果保存在`dist/device-gpu-runtime-1.0.13.log`。

## 9. 验证与仍缺的完整系统

1.0.10最终19/19联合回归通过，原函数字节及纹理哈希可检查；真机安装/启动返回0。独立同代码设备进程验证125/126/312目录计数、54个楼梯状态归一、真实材质aux、门堆叠1、canonical库存、四朝向/上下台阶/两格门规则，以及自然地形实际放置1格和保存重开。实际AMR保存v2 `item:…`库存；完整GUI逐项隔离验收未完成，不把本地mock视为设备验证。1.0.8库存、放置/挖掘文件及v3世界副本重载旧验证仍保留。

没有原控制台同版本的运行帧、完整区块输出或可自动回放输入参考。因此本项目不能诚实宣称“所有东西完全一致”。剩余大项包括完整生成阶段、原Level/实体/存档、完整Item/GUI、音频/网络、原GPU后端和Iggy ARM64运行时。
