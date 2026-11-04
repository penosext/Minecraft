# 原始游戏核心 ARM64 移植基线

**状态：应用1.0.8已接入原始基础地层、结构及树木/植物Feature子集并部署；完整游戏尚未移植。** 新建世界使用带版本的生成基准，旧版世界不自动升级。详细边界见 [源码对照](../../docs/ORIGINAL_PARITY.md)。

来源固定为 `gstaaij/minecraft-console` 提交 `09df8928eeb15328bca499d225e48ea95af6d119`。

## 已完成

1. `port/upstream/Minecraft.World` 根目录的1830个源码/头文件已按Git tree逐文件核验，记录在 `../original-world-fetch.json`。这不是所有客户端、子模块及资源已完整取得的声明。
2. 原始 `Random / ImprovedNoise / PerlinNoise` 的31个函数定义保持原字节编译，仅隔离PCH及计时/内存诊断平台接口；另精确提取 `Mth::lfloor` 和原数组模板。
3. `biomes/` 编译26个原始CPP单元，保留原Layer pipeline、河流/海岸/蘑菇岛/Voronoi过程，元数据来自23个原始biome注册。仅移除include并包隔离namespace，函数体有字节审计。
4. `terrain/` 原字节编译 `getMinDistanceToEdge`、`getHeightFalloff`、`getHeights`、`prepareHeights`、`buildSurfaces`。原始噪声初始化段与区块重播种语句保留原文。原构造整体未移植：没有构造或运行结构/洞穴对象。
5. `integration/` 将真实原始biome查询接入真实原始地层函数，生成16×128×16的Tile ID数组。固定默认_LARGE_WORLDS、864方块世界尺寸、海平面63、无自定义生成选项覆盖。

平台适配（类型、TLS、计时、对象生命周期和查询桥接）是新代码，不能误称全部文件逐字不变。随机流、噪声、分层、密度、表层计算不使用演示版value-noise或高度公式。

## 构建与验证

```bash
cmake -S ~/1234/Minecraft/port/original-core/integration \
  -B ~/1234/Minecraft/port/original-core/integration/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DORIGINAL_BIOMES_BUILD_TESTS=ON
cmake --build ~/1234/Minecraft/port/original-core/integration/build --parallel 4
ctest --test-dir ~/1234/Minecraft/port/original-core/integration/build --output-on-failure
```

本机ARM64 GCC13.3原生编译，`-fwrapv -ffp-contract=off -fno-fast-math`；集成Release **19/19通过**。涵盖来源字节审计、JDK RNG固定向量、正负64位种子、真实biome查询、地层随机流、基岩、数组索引及相邻密度节点接缝。

种子12345、区块(0,0)基础Tile数组FNV-1a诊断值为 `4154928b200470de`；文件为 `integration/build/chunk-seed12345-0-0.bin`。FNV仅作重复输出诊断，不是安全哈希。

**没有取得原控制台同版本输出做逐块外部对照，因此测试通过不构成完整原版等价性认证。** 模块UBSan测试通过，但原代码的signed shift等语言层限制仍记录在manifest，不能把-fwrapv当成修复全部UB。当前宿主ASan在main前地址空间初始化失败，未验证ASan。

审计清单：

- RNG/Noise：`integration/build/generated/source-manifest.json`
- Biome：`integration/build/biomes/generated/source-manifest.json`
- Terrain：`terrain/generated/source-manifest.json`

## 1.0.7–1.0.8 后续接入

- `structures/`：原村庄选址、拼接与模板，原沙漠/丛林神庙和沼泽小屋；采用记录过的canonical whole-piece处理，并非完整原后处理等价。
- `decoration/`：原树木和部分植物Feature、选种和数量规则；是装饰子集调度，缺少完整postProcess前置RNG消耗。
- `carvers/`（1.0.x v4）：原 `LargeFeature/LargeCaveFeature/CanyonFeature` 洞穴峡谷按原 `getChunk` 顺序作用于BASE，原 `OreFeature` + `BiomeDecorator::decorateOres`；`extract.py --check` 审计字节，清单在 `carvers/generated/source-manifest.json`。矿物使用省略前置阶段的重播种流，非完整 `postProcess` 等价。
- 应用已按v1/v2/v3/v4区分生成基准，旧存档保持原样。第一人称、原天体与GUI纹理布局的审计清单在应用generated目录及port/assets。

## 尚未完成

- 原废弃矿井/要塞，以及完整 `postProcess` 装饰、湖泊、地牢、实体生成、冰雪等；洞穴/峡谷、矿物、结构和树木已提取接入，但不等价于完整原流程。
- 完整Level、LevelChunk、服务器区块缓存、光照、更新/tick。
- 原Entity/Mob/Player运动、碰撞、跳跃、流体行为。
- 原始存储、metadata、实体/区块存档，不是现有mcworld-finite。
- 原世界渲染、原UI、音频、输入和4J平台后端。
- 完整原始游戏仍未完成。1.0.8已部署原始生成子集、原手部参数、天体纹理、窗口光照和方块库存；不能称完整原版客户端。

## 完整客户端的明确依赖缺口

原客户端CMake链接D3D11、XInput、Windows4J子模块与Iggy。

固定版本 `Minecraft.Client/Windows64/Iggy` 的完整Git tree已核实：只有Windows `.lib`、`iggy_w64.dll`、API头及GDraw桥接代码，没有Iggy核心实现源码或Linux ARM64运行库。目录证据保存在 `../original-iggy-tree.json`，原项目要求的4J子模块提交为 `8fb036f6d6ca5aa5aa2e20633638d6232a58d508`。

如果“完全按照”包含原始UI/中间件也不能更换，那么当前材料无法产出Linux ARM64完整客户端：需要兼容Iggy核心源码/运行库及对应平台依赖。若只要求原始游戏逻辑不改、允许更换UI/显示/输入后端，则仍是大型平台移植，不能把缺失逻辑写成空stub或用现有自写世界替代。
