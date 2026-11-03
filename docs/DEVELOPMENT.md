# 开发环境部署与打包

本文档说明如何从零搭好这个项目的开发环境、一键打出安装包，以及装到词典笔上。
所有命令都在仓库根目录 `/root/1234/Minecraft` 下执行。

> 写这份文档时，第 1、2、4 节的命令都在本机实跑验证过（含完整走一遍打包、25 项原生测试、
> 40 项界面测试、6 个生成器校验）。第 3 节的安装流程来自此前多次成功安装的实际记录，
> 但写文档当时设备离线，**没能再当场复跑一遍**。

---

## 1. 需要装什么

打包只依赖下面这些，版本是当前这台机器上实际验证过的：

| 工具 | 验证版本 | 用途 |
|---|---|---|
| gcc / g++ | aarch64-linux-gnu | 编译原生库 |
| CMake | 3.28.3 | 原生库与测试的构建系统 |
| Node.js | 22.23.4 | 跑界面打包与界面测试 |
| pnpm | 10.34.5 | 装界面依赖、调用打包命令 |
| Python | 3.12.3 | 跑离线代码生成器 |
| Pillow | 10.2.0 | 生成器读写 PNG 贴图 |

Debian / Ubuntu 上一次装齐：

```bash
sudo apt update
sudo apt install -y build-essential cmake python3 python3-pip git
sudo pip3 install --break-system-packages pillow
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
sudo apt install -y nodejs
sudo npm install -g pnpm
```

装完自检：

```bash
gcc -dumpmachine && cmake --version | head -n1 && node --version \
  && pnpm --version && python3 --version && python3 -c "import PIL; print(PIL.__version__)"
```

如果要装到真机，再加两个：

```bash
sudo apt install -y sshpass openssh-client
```

### 关于编译目标

词典笔是 **aarch64**。如果你的开发机本身就是 aarch64（当前这台就是），直接用
`--native` 本机编译即可，不需要交叉编译工具链。

如果开发机是 x86_64，就需要一套 aarch64 交叉工具链，两种给法：

```bash
# 方式一：放进 jsapi/toolchains/，脚本会自动找
jsapi/toolchains/<你的工具链目录>/bin/aarch64-...-gcc

# 方式二：直接指定前缀
export CROSS_TOOLCHAIN_PREFIX=/opt/aarch64-toolchain/bin/aarch64-linux-gnu-
```

`jsapi/toolchains/` 目录当前是空的，交叉工具链没有随仓库分发。

### 关于原始素材

原生库在构建时会跑一批离线生成器，它们从 `port/upstream/` 读原始源码、从
`port/assets/` 读贴图，再生成 C++ 头文件。这两个目录已经在仓库里，**不需要联网**。
生成器会校验源码哈希，素材被动过就会直接报错而不是悄悄产出错误结果。

---

## 2. 一键打包

```bash
bash tools/build.sh --native
```

这一条命令会依次完成：跑离线生成器 → 编译原生库 → 剥调试符号 → 打包界面 →
产出安装包。成功后看到：

```
[SUCCESS] Build process completed successfully!
```

产物在：

```
dist/minecraft-native-aarch64.amr
```

x86_64 开发机改用交叉编译（去掉 `--native`），产物名会变成
`dist/miniapp-<工具链名>.amr`：

```bash
bash tools/build.sh
```

### 改版本号

版本号取自 `ui/package.json` 的 `version`，打包时会用它拼中间文件名，所以要改就改这里：

```bash
python3 - <<'PY'
import json
from pathlib import Path
p = Path('ui/package.json')
d = json.loads(p.read_text())
d['version'] = '1.0.17'
p.write_text(json.dumps(d, ensure_ascii=False, indent=2) + '\n')
PY
```

---

## 3. 装到词典笔

设备默认地址 `192.168.161.167`，root 密码 `X7`。先确认通得上：

```bash
sshpass -p X7 ssh -o ConnectTimeout=10 root@192.168.161.167 'echo online'
```

然后上传、校验、安装、启动：

```bash
python3 - <<'PY'
import hashlib, subprocess
from pathlib import Path

amr = Path('dist/minecraft-native-aarch64.amr')
digest = hashlib.sha256(amr.read_bytes()).hexdigest()
ssh = ['sshpass', '-p', 'X7', 'ssh', '-o', 'ConnectTimeout=12', 'root@192.168.161.167']

remote = subprocess.check_output(ssh + ['mktemp -d /tmp/mc.XXXXXX'], text=True).strip()
subprocess.run(['sshpass', '-p', 'X7', 'scp', '-O', '-o', 'ConnectTimeout=12',
                str(amr), f'root@192.168.161.167:{remote}/app.amr'], check=True)

# 两端校验一致再装，避免装上一个传坏的包
remote_digest = subprocess.check_output(ssh + [f'sha256sum {remote}/app.amr'], text=True).split()[0]
assert remote_digest == digest, f'上传损坏: {remote_digest} != {digest}'

print(subprocess.check_output(ssh + [
    f'miniapp_cli install {remote}/app.amr; miniapp_cli start 6d696e6563726166'], text=True))
PY
```

`install` 和 `start` 都返回 `{"ret": 0}` 就算装好了。

### 装完怎么确认真的在跑

看设备日志，确认版本号对、并且在持续出渲染统计：

```bash
sshpass -p X7 ssh root@192.168.161.167 \
  'LOG=$(ls -t /userdata/applog/DictPen_*.log | busybox head -n 1); busybox tail -c 20000 "$LOG"' \
  | grep -E 'getVersion|MINECRAFT_RENDER_STATS|TypeError|GLIBC_'
```

正常情况下能看到 `app.getVersion()=<你的版本>` 和若干条 `MINECRAFT_RENDER_STATS`，
并且没有 `TypeError` 或 `GLIBC_` 报错。

截图确认画面：

```bash
sshpass -p X7 ssh root@192.168.161.167 'miniapp_cli capture /tmp/shot.png'
sshpass -p X7 scp -O root@192.168.161.167:/tmp/shot.png /tmp/shot.png
```

---

## 4. 提交前跑测试

### 原生测试（25 项）

首次需要先配置测试构建目录：

```bash
cmake -S tests/native -B jsapi/build/native-tests -DCMAKE_BUILD_TYPE=Release
```

之后每次：

```bash
cmake --build jsapi/build/native-tests --parallel 4
ctest --test-dir jsapi/build/native-tests --output-on-failure --parallel 3
```

期望结尾是 `100% tests passed, 0 tests failed out of 25`。

### 界面测试（40 项）与类型检查

```bash
for t in ui-controls ui-streaming-live ui-inventory-layout ui-chunk-range; do
  node tests/$t-test.cjs
done
pnpm -C ui exec tsc --noEmit
```

四套分别输出 15、10、5、10 项通过。

### 生成器可复现校验

生成器都支持 `--check`，用来确认产物能从源码精确复现：

```bash
for g in generate-items.py generate-item-icons.py generate-recipes.py \
         extract-placement.py extract-collision.py extract-interaction.py; do
  python3 tools/$g --check
done
```

---

## 5. 常见问题

**打包报 `No toolchain found in jsapi/toolchains/`**
开发机不是 aarch64 且没给交叉工具链。加 `--native`（本机是 aarch64），
或按第 1 节设置 `CROSS_TOOLCHAIN_PREFIX`。

**装上后起不来，日志有 `version 'GLIBC_2.38' not found`**
设备的 glibc 是 2.36，用了需要更高版本的函数。常见来源是 `std::stoi`、
`strtol`、`sscanf`，它们会引入 `__isoc23_*` 符号。先定位：

```bash
readelf --dyn-syms --wide ui/libs/libjsapi_Minecraft.so | grep '2\.38'
```

然后把对应处改成手工解析。提交前自检，输出应为空：

```bash
readelf --version-info ui/libs/libjsapi_Minecraft.so | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -3
```

最高应该停在 `GLIBC_2.35`。

**独立编译的小程序莫名段错误**
不要同时链接静态库和单独编译同一个 `.cpp`，那会让同一个符号出现新旧两份实现。
要么只链接 `jsapi/build/native-tests/` 下的 `.a`，要么全部从源码编。

**设备连不上**
设备会掉线。重试即可，不要中途改用别的方式去装：

```bash
for i in 1 2 3 4 5; do
  sshpass -p X7 ssh -o ConnectTimeout=10 root@192.168.161.167 'echo online' && break
  sleep 20
done
```

**改了生成器但产物没变**
生成器默认跳过内容相同的写入。确认是否真的该变，再用 `--check` 验证复现。

---

## 6. 目录速查

| 路径 | 内容 |
|---|---|
| `jsapi/src/MinecraftRenderer/` | 原生实现：世界、渲染、背包、工具规则 |
| `jsapi/build/generated/` | 生成的头文件，**不要手改** |
| `ui/src/pages/index/` | 界面逻辑、模板、样式 |
| `tools/` | 构建脚本与离线生成器 |
| `tests/` | 原生测试（`.cpp`）与界面测试（`.cjs`） |
| `port/upstream/` | 固定参考源码 |
| `port/assets/` | 原始贴图素材 |
| `dist/` | 安装包与验证记录 |

改动涉及原版行为时，请同时更新 [源码对照](ORIGINAL_PARITY.md)，把依据的源文件和行号写清楚。
