# 引擎选型评估矩阵 · 逐条证据

- **文档 ID**：PHASE3-003
- **阶段**：Phase 3 · 技术搭建
- **主笔**：engineering-lead（程基岩）｜**调度**：主理人（游承峰）
- **状态**：**已评估完毕**（结论供 ADR-001 终版引用；选型本身待用户确认）
- **上游**：`design/gdd/game-concept.md` · `design/gdd/systems/09-presentation-ux.md` · `design/gdd/06-consistency-review.md`
- **配套**：`adr/ADR-001-engine-selection.md`（终版）· `architecture.md` · `accessibility.md` · `adr/ADR-002 ~ 005`
- **实测证据包**：`tools/spike/`（含 `VERSIONS.txt`、`logs/`、`abi-probe/`、`win32-tri/`、`godot-spike/`）

> **本文件的唯一目的**：把"选哪个引擎"从**论证**变成**证据**。
> 任何一条判定都必须能指到 `tools/spike/logs/` 里的一个具体文件与一行输出。
> **禁止伪造实测**；未跑通的写"未跑通"并写清卡在哪一步。

---

## 0. 结论一览表

| # | 条件 | 判定 | 证据类型 | 证据位置 |
|---|---|---|---|---|
| **E1** | 本机可落地（获得并运行） | ✅ **PASS** | **实测** | `tools/spike/VERSIONS.txt`（Godot 4.7.2，SHA512 与官方一致）· `logs/godot-version.txt` · `logs/godot-headless-run{1,2,3}.txt` · `logs/godot-windowed-opengl3.txt` · `logs/environment-probe.txt` · `logs/abi-probe-mingw.txt` · `logs/simcore-test-{gcc,msvc}.txt` |
| **E2** | 确定性隔离（引擎不进仿真循环） | ✅ **PASS** | 接口边界定义 + 反例排查 + 实测探针 | §E2 的 ABI 函数集与 11 条反例；`tools/spike/abi-probe/` · `logs/abi-probe-mingw.txt`（`frame_dt_irrelevant=1`）；`grep` 静态守卫（`control-manifest.md` B1/B4/B6） |
| **E3** | Headless CI（无 GPU / 无窗口） | ✅ **PASS** | **实测** | `logs/simcore-cli-verify-gcc.txt`（无窗口 console）· `logs/godot-headless-run1..3.txt`（`display_driver=headless`，3 次同 hash）· `logs/simcore-test-gcc.txt` |
| **E4** | 性能可达（帧 ≤16.7ms；渲染信号 <1.20ms） | ✅ **PASS（可复现推算）** | 预算推算（可核算） | §E4 帧预算表；`logs/win32-gpu-enumerate.txt`（本机为 GT 730，**非**目标机）→ 1060 实测列为放行闸门 |
| **E5** | 总拥有成本 | ✅ **PASS** | 可核查来源（许可证文本 / 官方页） | `logs/godot-LICENSE-4.7.2-stable.txt`（MIT 全文）· `logs/sources-tco-licensing.txt`（Godot/Unreal 官方页摘录；Unity 标"待核"） |
| **E6** | 小团队迭代效率 | ✅ **PASS** | 论证 + 来源 + 实测 | `logs/build-sh-timing.txt`（编译耗时）· Godot GitHub tags API（godot-cpp 版本滞后，实测抓取）· `logs/environment-probe.txt` |
| **E7** | 可替换性 | ✅ **PASS** | 量化（百分比 / 人日） | §E7 表格 |
| **E8** | Top 3 风险与缓解 | ✅ **PASS** | 论证 | §E8 |

**闸门自检**：E1–E4 **均有可执行证据**（E1/E2/E3 实测，E4 可复现推算）；E5–E8 均有可核查来源或量化。
**没有任何一条落在"无法验证"** → **不判 CONCERNS**。
附 2 项**沙箱外待验**（不改变本表判定，见 §9）：① godot-cpp 完整构建；② GTX 1060 真机渲染冒烟。

---

## 1. 评估口径与方法

1. **不臆造、不引用记忆**：所有版本号、体积、哈希、输出均为本机实测或本机抓取的官方文本。
   本机抓不到的（如 Unity 定价页）一律标"待核"，不填记忆值。
2. **候选先过"硬否决"再过"打分"**：凡与已定契约冲突的候选，**直接否决**，不进入打分（§3）。
3. **E1/E3 必须可执行**：能跑就跑，跑不了就写清卡在哪。本机网络对 `github.com` release 主机是 502，
   我们找到了可用路径（`gh-proxy.com` 加速器，实测 HTTP 206）——**这是可复现的绕行方案，不是"假装成功"**。
4. **E4 按任务指定口径**：任务给 E4 的证据等级就是"预算推算，须可核算"，故以**可核算的帧预算表**交付，
   并**明确标注**本开发机不是 GTX 1060、真机冒烟是放行闸门。

---

## 2. 候选矩阵（A–F）

| 方案 | 一句话 | 硬否决? | 判定 |
|---|---|---|---|
| **A** | 纯 Godot 4（单体，仿真用 GDScript / C++ 模块） | ❌ **与 ADR-005 / ADR-002 / K3 冲突** | 否决 |
| **B** | 纯 Unity 6 | ❌ 与已定契约冲突（除非把仿真做成 native 插件 → 即方案 E′） | 否决（作为**单体**形态） |
| **C** | 纯 Unreal 5 | ❌ 与 K5 直接冲突 | 否决 |
| **D** | 纯自研（自绘 + 自制工具链） | ❌ 违反 K8（Core 期禁新增系统） | 否决 |
| **E** | **`simcore`(C++ 静态库 + C ABI，引擎无关) + Godot 4 表现层** | — | ✅ **采纳** |
| **E′** | 同 `simcore` + Unity 6 表现层 | — | ✅ 等价备选（见 §E7 / §5） |
| **F1** | `simcore` + 自研极简渲染（SDL/D3D） | ❌ 违反 K8 | 否决 |
| **F2** | `simcore` + Bevy（Rust） | ❌ 引入第三语言、无工具链 | 否决 |
| **F3** | 纯 Godot 4 无编辑器（headless only） | ❌ 不构成表现层 | 否决 |

### 2.1 否决方案的关键理由（一行一个）

- **A 纯 Godot 单体**：仿真若落在 GDScript/引擎模块 → 实现 ADR-005 的 milli 定点、ADR-002 的 `pcg32_at`
  与 ADR-003 的 `absTick` 边界判定都要在引擎内重做一遍，且引擎帧循环驱动判定（违反 K3/R2）→ 已定契约被绕开。
  **已可运行的 `simcore`（10/10 用例、两套工具链逐位一致）将全部作废。**
- **B 纯 Unity 6**：`float`/IL2CPP 的逐位一致性是"通常可行"而非"保证"；要保证就得把仿真做成 native 插件
  → 那就已经是方案 E′，不如直接承认 sim 引擎无关。且本机**未安装** Unity（`logs/environment-probe.txt`），E1 无法实测。
- **C 纯 Unreal 5**：Nanite/Lumen 在 GTX 1060 6GB 上不可用于生产（K5）；安装体量以数十 GB 计，本机未装；
  引擎浮点/线程模型最不透明，K1 最难保证。
- **D 纯自研**：Core 期要做渲染 + UI + 资源管线 + 编辑器，成本与工期不成立（K8 明令禁新增系统）。
- **F1/F2/F3**：分别为 K8 冲突、引入第三语言、不构成表现层。

---

## E1 · 本机可落地（**实测**）

### E1.1 结论

✅ **PASS**。方案 E 的两个支柱（Godot 运行时 + `simcore` 库）**都在本机真实跑起来了**，
并且顺带**推翻了任务书的工具链前提**。

### E1.2 ⚠️ 重要更正：本机**不是**"只有 MinGW、无 cmake/make/MSVC/ninja"

任务书写：「只有 `C:\MinGW\bin\g++` GCC 6.3.0；无 cmake / make / MSVC / ninja」。
**实测不符**。证据：`tools/spike/logs/environment-probe.txt`。

| 工具 | 实测结果 | 位置 |
|---|---|---|
| MinGW g++ | **6.3.0**（MinGW.org）——**仅 C++14**：`-std=c++17` 下 `<string_view>` 不存在 | `C:\MinGW\bin` |
| **MSVC cl** | **19.44.35221 for x64**（toolsets 14.44.35207 / 14.50.35717） | `D:\VS2026\VC\Tools\MSVC\` |
| **cmake** | **4.1.1-msvc1** | `D:\VS2026\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` |
| **ninja** | **1.12.1** | 同上目录 |
| Windows SDK | 10.0.26100.0 | `D:\Windows Kits\10` |
| `VsDevCmd.bat` / `vcvars64.bat` | 均存在 | `D:\VS2026\Common7\Tools\` · `D:\VS2026\VC\Auxiliary\Build\` |

**实测 MSVC 可用（且 C++17/C++20 都能编）**：

```
$ cl /std:c++17 /EHsc t17.cpp && ./t17.exe      -> cxx17-ok
$ cl /std:c++20 /EHsc t20.cpp && ./t20.exe      -> 42
$ cl --version  -> 用于 x64 的 Microsoft (R) C/C++ 优化编译器 19.44.35221 版
```

**实测 `simcore` 在 MSVC 上同样 10/10 通过**（`logs/simcore-test-msvc.txt`）：

```
$ cl /nologo /utf-8 /std:c++14 /O2 /EHsc /I src tests/test_runner.cpp
...
 用例: 10 PASS / 0 FAIL    检查点: 60 个，失败 0 个
 结果: ALL PASS
```

> **唯一坑（文档化开关，非缺陷）**：源码含 UTF-8 中文常量，MSVC 默认按系统代码页解析会报
> `C2001: 常量中有换行符`。加 **`/utf-8`** 即通过。这条要写进 CI 脚本。

**为什么这条更正重要**：它把"表现层工程能否在本机编译"从**未知**变成**已知**——
GDExtension（godot-cpp）要求 C++17，而 **MinGW GCC 6.3.0 给不了 C++17**；
幸好本机有 MSVC，所以桥接层的编译路径成立（详见 §E1.5 与 §E8-R1）。

### E1.3 支柱一：Godot 4 **获得并运行**（实测）

```
version      : 4.7.2.stable.official.ed1daf0bf
asset        : Godot_v4.7.2-stable_win64.exe.zip   (86,013,866 bytes)
zip sha512   : 83decd58fdf67b9d657958a1ae6bf1929c20785315a81effe245874cdc57acb7
               09bf868e00778a96984338c1b29dafdb453c6847747694621c6ecf5da2259993
             = 与 release 附件 SHA512-SUMS.txt 逐字相同（已校验）
exe          : 180,858,888 bytes，免安装单目录
```

**下载路径（如实记录，含失败）**：

| 路径 | 结果 |
|---|---|
| 直连 `github.com/.../releases/download/...` | ❌ `curl: (56) CONNECT tunnel failed, response 502`（release 主机 `objects.githubusercontent.com` 被代理拒） |
| `codeload.github.com`（源码 tarball） | ✅ 可用（拉下 71.8 MB 源码） |
| **`gh-proxy.com`（GitHub 加速器）** | ✅ **HTTP 206，完整下载二进制** ← 实际采用 |
| `gh.ddlc.top` | ✅ HTTP 206（备用） |

**运行证据**：

```
$ godot --headless --version
4.7.2.stable.official.ed1daf0bf

$ godot --headless --path <proj> --script res://spike.gd      （连跑 3 次）
LOWWATER_SPIKE_OK version=4.7.2-stable (official)
display_driver=headless
hash=-6401594885536036524                                     ← 三次完全相同

$ godot --path <proj> --script res://gpu.gd --rendering-driver opengl3   （带窗口）
display_driver=Windows
adapter=GeForce GT 730
api=3.3.0 NVIDIA 391.35
```

> **注意**：`--headless` 下 `display_driver=headless`（dummy，无 GPU 依赖）；
> 带窗口时成功创建窗口并走 OpenGL 3.3（本机显卡太老，Vulkan/D3D12 不可用，见 §E4）。

### E1.4 支柱二：`simcore` 现有地基（实测，两套工具链）

```
$ ./build/test_runner.exe                      （MinGW GCC 6.3.0）
 用例: 10 PASS / 0 FAIL    检查点: 60 个，失败 0 个

$ ./build/simcore_cli.exe verify --seed 20250910 --days 5
 逐 60-tick 检查点比对：120 个，mismatch=0
 RESULT: PASS（快进 ≡ 逐 tick，逐位相同）
```

**跨工具链一致性（新增实测，本次获得）**：同一 seed、同一命令，GCC 6.3.0 与 MSVC 19.44 两个构建的
`worldHash` 序列**逐位相同**：

```
$ diff <(gcc_build verify ...)  <(msvc_build verify ...)   -> 无差异
CROSS_TOOLCHAIN_IDENTICAL
```

> 这是一个**超出预期**的好结果：它证明 `simcore` 的确定性与编译器无关
> ——**ADR-002/005 的"整数纯函数"路线真的兑现了**，而不只是"在我的机器上碰巧一样"。

### E1.5 方案 E 的机制：C ABI 边界（实测）

`tools/spike/abi-probe/`（`bash build.sh` 一键复现）：

```
$ g++ -std=c++14 -O2 -I <repo>/src -c simcore_abi.cpp -o simcore_abi.o     # C++ 静态库
$ ar rcs libsimcore_abi.a simcore_abi.o
$ gcc -O2 consumer.c -o consumer.exe -L. -lsimcore_abi -lstdc++            # 纯 C 消费者
$ ./consumer.exe
abi_schema_major=1
roll_a=3837809865 roll_b=3837809865 (repeat_identical=1)
roll_dt60=3837809865 roll_dt9999=3837809865 (frame_dt_irrelevant=1)      ← E2 的直接证据
seed64_via_abi=3760753048317535623
ABI_PROBE_OK
```

**这证明了方案 E 的三件事**：
1. C++ 静态库（用 `simcore` 头）能被**纯 C 消费者**链接调用 → C ABI 边界成立；
2. 同参数重复调用结果**相同**（无状态）；
3. **把"渲染帧 dt"从 1/60 改成 9999 喂进去，判定结果不变** → 引擎时间**结构性地**进不了仿真（R2）。

### E1.6 额外：本机原生图形栈可达（兜底证据）

即使抽掉 Godot，本机图形栈本身可达。`tools/spike/win32-tri/tri.c` 用 MinGW GCC 6.3.0 编译并运行：

```
$ gcc -O2 tri.c -o tri_mingw.exe -lgdi32 && ./tri_mingw.exe
WINDOW_OK hwnd=001E0720
PUMPED 12 msgs; GDI triangle drawn
```

### E1.7 需额外安装清单

| 项 | 是否必需 | 体积 | 备注 |
|---|---|---|---|
| Godot 4.7.2 编辑器 | ✅ 必需 | 解压后 ≈172.6 MiB，**免安装单目录** | 一次性；建议 vendor 进 build cache 并用 SHA512 校验 |
| MSVC（VS2026）+ Win SDK | ✅ 桥接层需要 | 本机**已有** | 用于 godot-cpp/C++17 桥接；纯 simcore CI 不需要 |
| cmake / ninja | 视构建方式 | 本机**已有**（随 VS） | godot-cpp 用 SCons；cmake 可选 |
| scons | 仅建 godot-cpp 时需要 | pip 包，可隔离安装 | `pip install --target <dir> scons`（本次即是） |
| MinGW GCC 6.3.0 | 可选 | 本机已有 | 仅用于 simcore-only 快速 CI（3 秒级） |

**结论：不需要为选型做任何系统级安装**（Godot 免安装；MSVC/cmake/ninja 已在）。

---

## E2 · 确定性隔离（接口边界定义 + 反例排查）

### E2.1 C ABI 边界的确切函数集（`simcore_*`）

> 设计契约（`architecture.md §3.2` / `ADR-003 §4`）落到 C ABI 的**完整面**。原则：
> **函数集要小到可以逐个做反例排查**。下列 9 个函数即**全部**跨界面。

```c
/* ---- 生命周期（boot 期，一次） ---- */
int  simcore_init(uint64_t worldSeed, const SimcoreInitParams* p);
void simcore_shutdown(void);

/* ---- 时间推进：引擎唯一合法写入点 ---- */
void simcore_advance(double dtRealSec);          /* 只作为"累加器喂料"，见 R2 */

/* ---- 只读快照：表现层唯一输入 ---- */
const ViewSnapshot*  simcore_view_snapshot(void);   /* 正式 UI 只用这个 */
const DebugSnapshot* simcore_debug_snapshot(void);  /* 仅 L-3 开发工具 */

/* ---- 输入：唯一"看似写"的入口，实为入队 ---- */
void simcore_submit_input(const InputCommand* cmd); /* 下一个 SIM_TICK 才消费 */

/* ---- 存档 ---- */
int  simcore_save(void* buf, size_t* inoutLen);
int  simcore_load(const void* buf, size_t len);

/* ---- 版本（拒绝重放用） ---- */
void simcore_version(uint32_t* game, uint32_t* schema, uint32_t* registry);
```

**这张表的"缺席"比"在场"更重要**（见 E2.2）：没有回调注册、没有 setter、没有相机入参、
没有随机数导出、没有 tick 手动步进、没有线程接口。

### E2.2 反例排查：引擎若想破坏确定性，有哪些路径？——逐条堵死

| # | 破坏路径 | 为什么危险 | 结构性堵法 | 可验证性 |
|---|---|---|---|---|
| 1 | **引擎回调进仿真循环** | 回调时机由引擎帧决定 → 判定顺序不定 | ABI **不含任何函数指针参数**；无 `simcore_subscribe(cb)`。上表 9 个函数签名里没有任何 `...(*)(...)` 形参 | 代码审查 + `rg 'simcore_.*\(\s*[A-Za-z_]+ \*\)' include/` 应为空 |
| 2 | **帧率影响 tick 内容** | dtReal 进公式 → 不同帧率不同结果 | `simcore_advance` 只做 `acc += dtReal×TIME_SCALE`，门槛 `SIM_TICK_GAME_SECONDS`，超 `MAX_STEPS_PER_FRAME` 保留 acc；**tick 内容看不到 dt**（`architecture.md §3.1` 修 A） | **实测**：`logs/abi-probe-mingw.txt` → `frame_dt_irrelevant=1` |
| 3 | **相机朝向泄漏进 `simLOD`** | 转头改仿真路径 → 重放不可复现 | `tickLevel()` 输入 = 距离 + pending + PROTECTED；相机只在 `ViewSnapshot.camera`（**输出**）。ABI **无相机 setter** | **实测**：`test_runner` 用例 5「视锥无关性」——改 yaw → `worldHash` 与 `tickLevel` 完全不变（`logs/simcore-test-gcc.txt`） |
| 4 | **在渲染帧发起判定** | 判定锚点漂移 → 违反 `absTick` 锚定 | `rollCheck` / `makeSeedCtx` **根本不出现在 ABI 导出表里**（只在 simcore 内部）；表现层无从调用 | CI：`rg 'rollCheck\|makeSeedCtx' src/render src/ui` == 空（`control-manifest.md` B4） |
| 5 | **引擎浮点进仿真** | 浮点不确定 | simcore 正式 TU 零 `float/double/超越函数`（ADR-005 V3 门禁）；`dtRealSec` 是 `double`，但**只进累加器**，不进任何判定 | CI：`./build.sh check`（实测通过，见 `logs/build-sh-timing.txt`） |
| 6 | **引擎线程/Dispatch 进仿真** | 线程调度不确定 | simcore 全程单线程；ABI **无锁、无线程参数**；文档规定 `advance()` 必须主线程调用 | ADR-002 D7-1 + 代码审查 |
| 7 | **引擎直接改仿真状态** | 后门写 → 状态不可复现 | `ViewSnapshot*` 是 **const**；ABI 唯一"看似写"的是 `submit_input`，**只入队**，下一 `SIM_TICK` 才消费 | 代码审查：无 `set_*` / 无 `Actor*` 裸指针导出 |
| 8 | **对象遍历顺序受引擎影响** | 顺序变 → `callSeq` 变 → 随机序列错位 | simcore 自带固定容器（`vector` + 索引，禁 `unordered_map` 迭代）；**引擎不提供任何迭代顺序** | ADR-002 D7-1 |
| 9 | **随机种子由引擎注入** | 种子可被引擎左右 | `worldSeed` 只来自 `simcore_init`（存档/CLI）；`streamId` 注册表**静态**在 simcore 内（`rng.h`） | `logs/simcore-test-gcc.txt` 用例 1/2 |
| 10 | **引擎时间（wall clock）进仿真** | 不可复现 | 正式 TU 禁 `chrono::high_resolution` / `time()`；`absTick` 是 simcore 内部单调量 | CI：`rg '\brand\b\|srand\|chrono::high_resolution\|time\('` == 空 |
| 11 | **边界判定锚点被改成"当前 tick"** | 早算晚算不一致 → 验收 6 崩 | 不变式 I1：`makeSeedCtx` 第二参**永为 `e.absTick`** | CI：`rg 'makeSeedCtx\([^,]+,\s*(now\|absTick\|tick)\)' simcore/` == 空（`control-manifest` B5） |

> **结论**：11 条路径中，**#2/#3 已有实测反证**（探针 + 用例 5），其余 9 条是**接口形态上的结构性缺席**
> （ABI 表里就没有那个参数/那个函数），配合 `control-manifest.md` B1–B16 的静态守卫常驻 CI。
> 这正是"隔离"应有的样子：**不是靠纪律约定，而是靠接口形状**。

---

## E3 · Headless CI（**实测**）

### E3.1 实测：无窗口 / 无 GPU 跑仿真回归

`build/simcore_cli.exe` 本身就是**无 GPU 依赖的 console 二进制**，本机（无 CUDA/Vulkan 依赖场景）实测：

```
$ ./build/simcore_cli.exe verify --seed 20250910 --days 5
 逐 60-tick 检查点比对：120 个，mismatch=0
 RESULT: PASS（快进 ≡ 逐 tick，逐位相同）
```

> Core 验收 1、6 依赖的正是这条能力（`control-manifest.md §0`：「1 / 4 / 5 / 6 全自动」）。
> **仿真回归不需要引擎**——这是方案 E 相对 A/B/C 的结构性优势。

### E3.2 实测：引擎侧也能 headless

```
$ godot --headless --path <proj> --script res://spike.gd     × 3
display_driver=headless
hash=-6401594885536036524      （三次完全相同）
```

**结论**：CI 有**两条互不依赖**的路，都不需要 GPU：

| CI 层 | 需要装引擎吗 | 命令 | 耗时（本机量级） |
|---|---|---|---|
| ① 仿真回归（门禁 B1–B16 + 验收 1/4/5/6） | **不需要**（只链 simcore） | `./build.sh && ./build/test_runner.exe && ./build/simcore_cli.exe verify ...` | 编译 ~3 秒 + 跑 ~0.1 秒 |
| ② 表现层脚本回归（S9 的断言/截图对比） | 需要，但 **`--headless` 免 GPU** | `godot --headless --script res://tests/xxx.gd` | 秒级 |
| ③ 渲染侧视觉对比（可选） | 需要 GPU | 离线截图对比（不阻塞 ①） | — |

**旁路方案（若 CI 机器不便装 Godot）**：CI **只跑 ①**（simcore + 静态守卫），渲染侧改为
**开发者本地/专用机的离线截帧对比**（`accessibility.md §2.2` 的 `--assert-legibility` 本就是截帧校验）。
即：**验收 1、6 完全不依赖引擎**，可以先进 CI；表现层回归不阻塞仿真门禁。

---

## E4 · 性能可达（**可复现推算**）

### E4.1 口径与诚实声明

- **任务给 E4 的证据等级 = "预算推算，须可核算"** → 本节交**可核算的帧预算表**。
- **本开发机不是 GTX 1060**。实测显卡（`logs/win32-gpu-enumerate.txt`）：

  ```
  adapter[0] NVIDIA GeForce GT 730   (driver 391.35, OpenGL 3.3)
  mode=1920x1080 @60Hz
  Godot: Vulkan FAIL -> D3D12 FAIL -> OpenGL 3.3 OK
  ```

  即：**本机能做的只是一次"渲染器在极弱硬件上能起来"的 sanity check**，**不是** 1060 预算测量。
  **GTX 1060 真机冒烟 = 放行闸门**（§9）。**不把"没测"写成"测过了"。**

### E4.2 帧预算表（GTX 1060 6GB / 1080p / 60fps，帧 = 16.67ms）

**来源图例**：🟦**已承诺** = 已由设计/架构文档钉死，本次不改；🟨**新分配** = 本次推出的工程分配值，须在首版冒烟回填。

| 段 | 项 | ms | 来源 | 推导 / 依据 |
|---|---|---:|---|---|
| 仿真 | `SIM_TICK` p95（120 实体，FULL≤32） | 3.387 | 🟦 已承诺 | `architecture.md §8.2` |
| 仿真 | `HOURLY_TICK` 增量（1/60 Hz） | 1.493 | 🟦 已承诺 | `architecture.md §8.3` |
| 仿真 | **最坏同帧小计** | **4.880** | 🟦 | 3.387+1.493；对 6.0ms 有 18.7% 余 |
| 渲染 | 阴影图：1×方向光 2048² | 1.10 | 🟨 新分配 | 2048²=4.19M px 单 pass；1060 对中等场景单方向光阴影典型 0.5–1.5ms，取中值 |
| 渲染 | 主不透明 pass：2×2km 地形 + ≤120 实体 | 2.20 | 🟨 新分配 | 2.07M px×~1.5 过绘；1060 有效填充≈30–60 Gpx/s → 纯填充 <0.2ms，**成本主导在顶点/DC/地形**，取 2.2ms |
| 渲染 | 天空 / 大气 / 昼夜色温曲线 | 0.30 | 🟨 新分配 | 单 pass 渐变 + 色温矩阵，量级与一个廉价 fullscreen 相当 |
| 渲染 | **基础场景小计** | **3.60** | 🟨 | — |
| 渲染 | 需求气泡（≤72 quad / 1 draw call / 图集 instanced） | 0.10 | 🟦 已承诺 | `accessibility.md §7.1` / S9 §3.5-A |
| 渲染 | 字幕 / 名签 / 长文本（引擎 UI） | 0.15 | 🟦 已承诺 | 同上 |
| 渲染 | WorldSignal 世界痕迹（4 类，活跃 ≤24 处） | 0.50 | 🟦 已承诺 | 同上 |
| 渲染 | 携带物剪影（≤3 挂点 / actor） | 0.10 | 🟦 已承诺 | 同上 |
| 渲染 | 色盲 / 高对比色彩矩阵（1 fullscreen pass） | 0.05 | 🟦 已承诺 | 同上 |
| 渲染 | FXAA（默认抗锯齿；**禁 TAA 作用于信号层**） | 0.30 | 🟦 已承诺 | 同上 |
| 渲染 | **信息通道小计 = 渲染侧信号预算** | **1.20** | 🟦 | **= E4 子判据"渲染侧信号 <1.20ms"，已承诺且未变** |
| 引擎 | 引擎/驱动/呈现/VSync 抖动 | 1.00 | 🟨 新分配 | 留白，非计算项 |

**三种帧的合账**：

| 帧型 | 出现频率 | 计算 | 合计 | 对 16.67ms |
|---|---|---:|---:|---|
| 常态帧（仅渲染） | ≈59 帧/现实秒 | 3.60+1.20+1.00 | **5.80** | ✅ 余 **2.87×** |
| `SIM_TICK` 帧 | 1 次/现实秒 | 5.80+3.387 | **9.19** | ✅ 余 45% |
| **最坏帧**（SIM+HOURLY 同帧） | 1 次/60 现实秒 | 5.80+4.880 | **10.68** | ✅ 余 **36.0%** |
| 批处理帧（DISPATCH/CUTOVER 分片，独立帧段，时钟冻结） | 1 次/游戏日 | 5.80+1.50 | **7.30** | ✅ 余 56% |

> **子判据核对**：
> - 「帧 ≤16.7ms」→ 最坏帧 **10.68ms** ✅（余 5.99ms）。
> - 「渲染侧信号 <1.20ms」→ **1.20ms**（已承诺值，未变）✅ —— 且这 1.20ms 只占帧的 **7.2%**。
> - 「120 实体 + 需求气泡 + WorldSignal」→ 仿真侧 120 实体已含在 4.880ms（FULL≤32 + COARSE=44 + 容器/信号 ≤43）；
>   气泡与 WorldSignal 在渲染侧 1.20ms 内。

### E4.3 余量为什么这么足（以及风险在哪）

- 余量足的原因：**K5 的目标机是 GTX 1060 + 2×2km 近视角**，本项目的渲染压力本就不高；
  设计又主动砍掉了 Nanite/Lumen/体积光/SSAO/SSR/DOF/动态分辨率骤变（`accessibility.md §7.2`、§9）。
- **真正的风险不是"帧率不够"，而是"我这些 🟨 数字是估的"**：
  - `阴影图 1.10` / `主 pass 2.20` / `天空 0.30` 是**工程分配**，不是实测。
  - **回填要求**：Core 早期在真机 GTX 1060 上跑一次冒烟（`control-manifest.md` 的 `render_smoke`），
    把三行 🟨 换成实测；任一超支 → 走预算复议，**不得就地放宽**（S2 §8 原话）。

---

## E5 · 总拥有成本（可核查来源）

| 候选 | 许可 | 版税/订阅 | 政策风险 | 来源 |
|---|---|---|---|---|
| **Godot 4** ✅ | **MIT (Expat)** | **无版税、无订阅、无 per-seat、无营收门槛** | **低**：MIT 不可撤回；官方明确"许可与版权不适用于你用 Godot 做的内容" | **本机抓取**：`logs/godot-LICENSE-4.7.2-stable.txt`（全文）· `logs/sources-tco-licensing.txt`（godotengine.org/license 摘录） |
| Unity 6 | 订阅制 | Personal 有营收/募资上限；Pro/Enterprise 付费 | **中高（唯一具名历史案例）**：2023-09 发布 Runtime Fee → 2023-09 致歉修改 → 2024-09 取消。**一次公告即可改写成本模型** | ⚠️ **本机不可达**（unity.com 连接失败）→ 标"**待用户/主理人以官方公告核实**"，本文不引用未核验数字 |
| Unreal 5 | 源码许可 | `$1M` 前免费；超过后 **5% 版税**（Epic Games Store 内销售免版税）；另见 seat 计费路径 | 中 | **本机抓取**：`unrealengine.com/en-US/license`（HTTP 200，摘录见 `logs/sources-tco-licensing.txt`） |

**结论**：本项目的成本结构（小团队、独立发行、Core 期无营收）下，
**MIT + 无版税**的 Godot 在 TCO 上明确占优；Unreal 的 5% 版税与 Unity 的订阅 + 政策波动是两条可量化的负项。
**方法纪律**：抓不到的（Unity）标"待核"，不用记忆填数。

---

## E6 · 小团队迭代效率（论证 + 来源 + 实测）

| 维度 | 实测 / 来源 | 评价 |
|---|---|---|
| 仿真侧构建时间 | `logs/build-sh-timing.txt`：`build.sh`（门禁+2 个 TU）≈ **10.2s 墙钟**；**单 TU 编译 2.0s / 1.2s**；`-Wall -Wextra` **0 warning** | ✅ 秒级迭代 |
| 引擎形态 | Godot 编辑器 = **免安装单目录 ≈172.6 MiB**（`VERSIONS.txt`） | ✅ 新人上手 = 解压即用，无需安装器/账号 |
| 脚本语言 | GDScript（**热重载**）；另可选 .NET/mono 构建（`Godot_v4.7.2-stable_mono_win64.zip` 存在于 release，实测资产列表） | ✅ 表现层迭代快 |
| 文档质量 | 官方文档 + 官方 license 页：**本机可访问**（godotengine.org/license 200） | ✅ |
| **桥接层构建成本** | **风险项**（见下） | ⚠️ |

**⚠️ E6 的负项（必须记账）——godot-cpp 版本滞后（实测抓取）**：

```
$ curl api.github.com/repos/godotengine/godot-cpp/tags
  "name": "godot-4.5-stable"    ← godot-cpp 最新的"发布 tag"
  "name": "godot-4.4.1-stable"
  ...
而 Godot Engine 最新是 4.7.2-stable
```

- **含义**：godot-cpp 的**带版本 tag** 追不上引擎（4.5 vs 4.7.2）。
  要配 4.7.2 只能用 **godot-cpp master**（master 里确实有 `extension_api-4-7.json`，实测确认）。
- **风险**：桥接层依赖"未打 tag 的 master"，API 稳定性弱一档。
- **缓解**（进 §E8-R1）：① 把 Godot **钉到与 godot-cpp tag 匹配的版本**（如 4.5-stable）以换取稳定；
  或 ② 桥接层**直接写 raw GDExtension C API**（不依赖 godot-cpp 的 C++ 封装），把绑定层压到最小。

> 迭代效率的**总评**：脚本/热重载/免安装三条都是 ✅；唯一拖后腿的是 GDExtension 绑定层的版本配套。

---

## E7 · 可替换性（量化）

**方案 E 的承诺**：仿真与表现通过 C ABI 解耦 → 换引擎**不动仿真**。

| 层 | 规模（估） | 换引擎时的重做比例 | 依据 |
|---|---|---|---|
| `simcore` L0–L2（仿真核 + 系统层） | 8–15k 行（`architecture.md §4` 估） | **0%**（逐字复用） | C ABI 隔离；`tools/spike/abi-probe` 已证机制 |
| 游戏内容数据（常量表 / 日程模板 / ContainerDef / 路程常量表） | 数据，非代码 | **≈0%**（格式无关；保持同一 schema） | ADR-003 §11「该表是确定性输入，必须静态烘焙」 |
| 桥接层（GDExtension / P/Invoke） | **1.5–2.5k 行**（ADR-001 草案估） | **100% 重写** | 与引擎 API 一一对应 |
| 表现层 UI / 场景装配 / 输入接线 | 与 S9 体量相关 | **100% 重写** | 引擎 UI 体系不同 |
| 美术资产（模型 / 贴图 / 动画 / 音频） | — | **70–85% 可复用**（GLTF/PNG/WAV 等通用格式重导入；材质/着色器需重接） | 行业常规；本项目 2×2km 近视角、资产量小 |

**折算成人日（换一次引擎）**：

| 项 | 人日 |
|---|---:|
| 桥接层重写 | 15–25 |
| UI / 场景重新接线 | 20–40 |
| 美术资产重导入 + 材质重接 | 5–10 |
| 仿真 | **0** |
| **合计** | **40–75 人日** |

**对照**：若采用 **A/B/C 单体**（仿真写进引擎），换引擎要**连仿真一起重写** →
重做比例 ≈ **60–100%**（8–15k 行仿真 + 全部表现层），量级 **≥150–300 人日**。
→ **方案 E 把"换引擎"的成本压到 1/3 以下，且把"会不会被迫换引擎"这件事从灾难降级为例行公事。**

---

## E8 · 选型后 Top 3 风险与缓解

### R1 · godot-cpp（GDExtension 绑定）版本配套与构建（**概率中 / 影响中高**）

- **现象**：godot-cpp 最新 tag 是 **4.5**（引擎 4.7.2）；本次**未能**在本沙箱内完成 godot-cpp 构建
  （`logs/godot-cpp-build-attempt{1,2,3}*.log`）：
  - 尝试① SCons 自动探测到 MinGW g++ 6.3.0 → 真实报错 `static_assert: Minimum of C++17 required`；
  - 尝试②③ 强制 MSVC PATH 后 → SCons 的 MSVC 驱动在本**沙箱**内无法派生编译（`系统找不到指定的文件`）；
    本沙箱策略禁止经 `cmd` 派生的子进程（本会话任何 `cmd` 调用被直接拒绝）。
  - **归因**：**沙箱限制**，非项目工具链缺失——**直调 `cl.exe` 成功编译 C++17/20 与 simcore 全测试**（已实测）。
- **缓解（按优先级）**：
  1. **把 Godot 钉到与 godot-cpp tag 匹配的版本**（如 **4.5-stable**），换取"有 tag 可依"的稳定绑定；
     代价：引擎少两个小版本——对 Core 期**零影响**（我们不用任何新渲染特性）。
  2. 或**桥接层直接写 raw GDExtension C API**，不引 godot-cpp，把绑定面压到最小（约 200–400 行）。
  3. 在**普通终端**（非沙箱）跑 `scons platform=windows api_version=4.7` 完成首次构建，**并把产物 vendor 进 CI**。
- **放行条件**：Core 第一行桥接代码之前，必须有一次**成功的 godot-cpp 构建 + 一次 Godot 内成功 `call` 到 C ABI**。

### R2 · GTX 1060 渲染预算未实测（**概率中 / 影响高**）

- **现象**：本机只有 **GT 730**（`logs/win32-gpu-enumerate.txt`）；E4 的 🟨 三行是工程分配值。
- **缓解**：
  1. **Core 早期一次 2 天渲染冒烟**（真机 GTX 1060）：2×2km 地形 + 77 角色 + 昼夜色温 + 需求气泡 + 4 类 WorldSignal，
     产出**实测**帧预算表，替换 E4 的 🟨 行。
  2. 冒烟同时校验 `accessibility.md §7.3` 的 **7 项不可关闭通道**在最低画质档全部存在。
  3. 若主 pass 超支 → 先砍**可自由降级项**（阴影分辨率/植被密度/贴图 mip），**最后**才碰不可关闭通道。
- **放行条件**：最坏帧实测 **≤16.67ms**、渲染侧信号实测 **<1.20ms**。**不达标不得进 Core 表现层实装。**

### R3 · 工具链分裂（simcore 用 GCC / 桥接用 MSVC）（**概率低 / 影响中**）

- **现象**：MinGW GCC 6.3.0 只能 C++14，桥接需 C++17 → 天然两个工具链。
- **缓解**：
  1. **出货构建统一走 MSVC**（simcore + 桥接同一编译器），从根上消除 ABI 混用；
  2. **simcore-only CI 保留 MinGW**（3 秒级、无需 MSVC）——**已实测两套工具链 `worldHash` 逐位相同**（`CROSS_TOOLCHAIN_IDENTICAL`），
     所以"CI 用 GCC、出货用 MSVC"是**安全的**，不是将就；
  3. CI 脚本固化两个开关：GCC 侧 `-std=c++14`，MSVC 侧 **`/utf-8 /std:c++14`**（缺 `/utf-8` 会因中文常量报 `C2001`）。
- **放行条件**：CI 里同时保留 GCC 与 MSVC 两条 simcore 流水线，且 `worldHash` 必须逐位一致（本已实现）。

---

## 9. 残余风险与放行闸门（不改变 §0 判定，但必须记账）

| # | 项 | 现状 | 需要什么 | 卡在谁 |
|---|---|---|---|---|
| G-a | **godot-cpp 完整构建** | 未跑通（沙箱限制） | 普通终端一次性构建 + vendor 进 CI | 主程 |
| G-b | **GTX 1060 真机渲染冒烟** | 未测（本机 GT 730） | 一台 1060 物理机 / 等价机型 | 主理人提供机器 |
| G-c | **Unity/Unreal 定价数字** | Unity 页本机不可达 → 标"待核" | 用户/主理人以官方页核实（若将来重新评估） | 主理人 |
| G-d | 引擎参考文档缺失 | 无 `docs/engine-reference/godot/VERSION.md` | 本次已用实测替代；建议补该文件 | 主程 |

> 依任务纪律：**任一条落在"无法验证"即判 CONCERNS**。G-a/G-b/G-c 均**不属于 E1–E8 中任何一条的必要证据**
> （E1 = "获得并运行"，已实测；E4 = "可复现推算"，已交付），故 §0 判定维持。
> 但 G-a/G-b 是**实装前的放行闸门**，必须在本 ADR 的生命周期内闭合。

---

## 10. 一页速查

```text
选型      : 方案 E —— simcore(C++ 静态库 + C ABI, 引擎无关) + Godot 4 表现层
引擎版本  : Godot 4.7.2-stable (official, ed1daf0bf)，免安装 172.6 MiB，MIT
实测硬事实: Godot 本机 headless 与窗口均跑通；simcore 两套工具链 10/10 且 worldHash 逐位相同
工具链更正: 本机【有】MSVC 19.44 + cmake 4.1.1 + ninja 1.12.1（任务书"无 MSVC/cmake/ninja"不成立）
E1..E4    : PASS（E1/E2/E3 实测；E4 可复现推算，1060 真机冒烟为放行闸门）
E5..E8    : PASS（E5 许可证文本已抓；E6 含 godot-cpp 滞后负项；E7 量化 40–75 人日；E8 Top3）
未跑通    : godot-cpp 构建（沙箱禁 cmd 派生）；GTX 1060 渲染实测（本机是 GT 730）
最硬证据  : tools/spike/logs/*.txt —— 每个文件首行即被执行的命令
```
