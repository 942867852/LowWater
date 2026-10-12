# tools/gpu-smoke · 换机 GPU 渲染冒烟测试包

- **对应**：`docs/architecture/engine-evaluation.md §E4`（GTX 1060 帧预算推算）与 `§9` 放行闸门 **G-b**
- **目标引擎**：**Godot 4.5-stable**（与 ADR-001 钉定版本一致）
- **换机清单**：见 `docs/architecture/gpu-smoke-env.md`

---

## 0. 先说清楚：这个包**测什么、不测什么**

| | 说明 |
|---|---|
| ✅ **测** | **渲染负载**：120 实体、需求气泡、4 类 WorldSignal 痕迹、昼夜光照四档、相机绕行/转头，在 1080p 下的帧时间（p50/p95/p99/最坏）与"各子项渲染成本" |
| ✅ **测** | 绊线 `SET_TRAP` 的 **≥1.5px 最小宽度钳制**在目标机上是否成立（同时给出"未钳制"的像素宽度作对照） |
| ✅ **测** | 表现层 LOD 切换是否违反 `accessibility.md §2.4` 的 **N1/N2**（转头不错乱） |
| ❌ **不测** | **`simcore` 集成**（桥接层尚未构建，本包**不依赖任何 .dll/.lib**）。simcore 集成冒烟**待桥接层完成后另出一包** |
| ❌ **不测** | 仿真侧 CPU 预算（`SIM_TICK` / `HOURLY_TICK`）。报告里用 E4 的 **4.880ms** 常量**直接加回**，不是实测 |
| ❌ **不测** | 音频、存档、UI 交互、长时间稳定性 |

> **为什么用纯 GDScript**：目标机器要求**零编译、零安装**。因此全部用 GDScript + 程序化生成的图标，
> 除 Godot 二进制外**不需要任何编译工具**。

---

## 1. ⚠️ 本机验证状态（诚实标注 —— 请务必先读）

| 项 | 状态 |
|---|---|
| **在 GTX 1060（或等价）目标机上的运行结果** | ❌ **未验证**。开发机显卡是 **NVIDIA GeForce GT 730 / driver 391.35**，**不是**目标机，无法替代 |
| 项目能被 Godot 4.5-stable 打开、全部 GDScript 能被解析 | ✅ **已验证**（本机实测，0 个 `SCRIPT ERROR`） |
| headless 启动并能跑完 9 个阶段、写出 `result.json` / `report.md` | ✅ **已验证**（本机实测） |
| 在**真实 GPU** 上窗口化完整跑完一遍 | ✅ **已验证**，但跑在 **GT 730 + OpenGL 3.3 回退**上，结果是 **FAIL**（最坏帧 29.1ms）——**这只是证明管线通了，数字对 GTX 1060 无参考价值**。原始输出留在 `_reference/gt730-2026-09-13/` |
| 判据逻辑（PASS/FAIL 分支、超预算子项定位、绊线钳制） | ✅ **已验证**——GT 730 那次跑出的是 FAIL，报告正确定位到 4 个超预算子项；headless 那次 `verdict=INVALID_NO_GPU` 也被正确标出 |
| 绊线钳制算法（把 0.020m 厚度放大到 ≥1.5px） | ✅ **已验证**（实测钳制后 1.500px，未钳制 0.195px） |

**一句话**：**管线是通的，判据是对的；但"GTX 1060 上能不能过"这一问，本包在你换机之前没有答案。**

---

## 2. 三步跑起来（Windows）

```bat
REM 1) 取引擎（首次；已有则自动跳过）
fetch-godot.bat

REM 2) 跑冒烟
run-smoke.bat

REM 3) 看结果
notepad results\report.md
```

- 想先试通不耗时：`run-smoke.bat -- --frames=60`（每阶段只采 60 帧）
- 强制指定后端：`run-smoke.bat --rendering-driver vulkan`（默认自动：Vulkan → D3D12 → OpenGL3）
- Linux/macOS 用 `./fetch-godot.sh` 与 `./run-smoke.sh`

**目标机没网？** 把这两个文件放进 `_bin\` 即可，脚本会自动跳过下载（**两个都要，缺一不可**）：

```
_bin\Godot_v4.5-stable_win64.exe
_bin\Godot_v4.5-stable_win64_console.exe
```

官方下载地址（SHA512 见 `fetch-godot.*`，必须核对）：
`https://github.com/godotengine/godot/releases/download/4.5-stable/Godot_v4.5-stable_win64.exe.zip`

---

## 3. 目录结构

```
tools/gpu-smoke/
├── project.godot                 工程配置（1080p / 无边框 / 关 vsync / 不限制帧率）
├── smoke.tscn                    主场景（根节点挂 smoke_main.gd）
├── scripts/
│   ├── smoke_main.gd             主流程：阶段状态机 + 逐帧采样 + 写结果
│   ├── world_builder.gd          场景构造 + 各层开关 + LOD/绊线逻辑
│   ├── glyph_atlas.gd            程序化生成 10 个形状 glyph（零美术资产）
│   └── report.gd                 汇总 → result.json / report.md
├── fetch-godot.bat / .sh         取 Godot 4.5-stable 并校验 SHA512（已存在则跳过）
├── run-smoke.bat  / .sh          一键运行
├── _bin/                         Godot 二进制放这里（*.exe 已被 .gitignore 忽略）
├── results/                      ← 输出目录（每次运行覆盖）
│   ├── result.json               机器可读
│   └── report.md                 人可读
└── _reference/gt730-2026-09-13/  开发机 GT 730 的参考输出（FAIL 样例，证明管线通）
```

---

## 4. 测试内容（逐条对应 E4 / accessibility 硬约束）

| # | 内容 | 对应 |
|---|---|---|
| 1 | **120 个实体**：32 个 full 行为（会走、会转身）+ 88 个简模；外加 300 个静态垂直元素 + 2×2km 地形 | E4「主 pass：2×2km 地形 + ≤120 实体」；`LOD_FULL_MAX = 32` |
| 2 | **需求气泡**：10 形状 glyph 闭集 × 底盘 × 3 档填充；**位置钟位**（12 槽绕头）+ **运动**（上下浮动）——三重编码齐全 | `accessibility.md §2.1 / §3` |
| 3 | **4 类 WorldSignal 痕迹**：<br>`REROUTE` 脚印链（12 枚/处）<br>`ADD_WATCH` 过夜灯 + 坐具 + 坐着的<br>`DEEPEN_STASH` 拖拽沟槽 + 被拖进屋的柜子<br>`SET_TRAP` 绊线 + 铃铛<br>4 类 × 6 处 = **24 处**（活跃 ≤24 处） | `accessibility.md §4` |
| 4 | **绊线 ≥1.5px 钳制**：世界厚度 0.020m；每帧按 `分辨率/距离/fov` 反算所需厚度并放大（`apply_tripwire_clamp`），同时报告**未钳制**的像素宽度作对照 | `accessibility.md §7.2` |
| 5 | **昼夜光照四档**：晨 06:00 / 午 12:00 / 昏 20:00 / 夜 24:00，逐档切色温与环境光 | S9 §6.1 **C-1 时间锚** |
| 6 | **相机绕行 + 转头**：半径 24–44m 绕行 + 额外偏航摆动，触发 LOD 切换，检验 N1/N2 | `accessibility.md §2.4` |

---

## 5. 测量方法：**分阶段累积开启 + 帧时间差分**

纯 GDScript 拿不到 GPU timestamp，所以用**差分法**：每个阶段只比上一阶段多开一个子项，测帧时间增量。
每阶段 **预热 90 帧 + 采样 300 帧**。

| 阶段 | 累积开启 | 用于 |
|---|---|---|
| `P0_base` | 地形 + 120 实体 + 正午光照 | 基线 |
| `P1_bubbles` | +需求气泡 | 气泡成本 |
| `P2_hudtext` | +名签 | 文本成本 |
| `P3_signals` | +4 类 WorldSignal | 信号成本 |
| `P4_carry` | +携带物剪影 | 剪影成本 |
| `P5_matrix` | +色彩矩阵 | fullscreen pass 成本 |
| `P6_fxaa` | +FXAA | **= 信息通道全开**（→ 对照 1.20ms） |
| `P7_camera` | +相机绕行/转头 | LOD 检验 |
| `P8_lighting` | +昼夜四档切换 | **最坏帧** |
| `P9_lod_control` | 对照组：关掉 N2 闸门 | 证明 LOD 检测器非空转 |

---

## 6. 输出

### 6.1 `results/result.json`（机器可读）

关键字段：

```
verdict                      PASS / FAIL / INVALID_NO_GPU
environment.engine_version / rendering_driver / gpu_adapter / gpu_api_version
environment.screen_size / refresh_hz / viewport_size / cpu_name
fxaa_applied                 后端是否真的支持 screen_space_aa（OpenGL3 不支持，会被记账）
phases[]                     每阶段 {mean,p50,p95,p99,max,min}_ms
subitem_cost_ms              6 个子项的实测增量
subitem_alloc_ms             E4 的分配值
over_budget_subitems[]       超预算子项的 key
information_channel_ms       P6 - P0
frame.worst_render_p95_ms    最坏帧【渲染侧】
frame.worst_frame_p95_ms     最坏帧【+仿真预算 4.880ms】
deviation_vs_estimate        与推算最坏帧 10.68ms 的 delta 与 ratio
tripwire.min_px / min_px_unclamped / samples
lod.executed_in_cone / deferred_in_cone / missing_carrier / band_missing
criteria[]                   C1..C6 逐条 {id, desc, pass, measured}
```

### 6.2 `results/report.md`（人可读）

含：机器/驱动表 → 判据逐条表 → 帧时间表 → 子项成本表 → 与推算偏差表 → 绊线 → LOD → **超预算子项清单**。

---

## 7. 判定口径

| # | 判据 | 阈值 | 说明 |
|---|---|---|---|
| **C1** | 最坏帧 p95 | **≤ 16.70 ms** | `P8_lighting` 的 p95 **+ 4.880ms 仿真预算**（本包不含仿真，故加回） |
| **C2** | 渲染侧信息通道增量 | **< 1.20 ms** | `P6_fxaa.mean − P0_base.mean`（6 个子项之和） |
| **C3** | 绊线屏幕最小宽度 | **≥ 1.5 px** | 钳制后实测 |
| **C4** | N2 可读性保持锥内无载体切换 | **= 0** | 锥 = 屏幕中心水平 ±25° / 垂直 ±20° |
| **C5** | N1 0–60m 内始终有可见载体 | **= 0** | 载体 = 气泡 / 携带物剪影 / 角色本体剪影 |
| **C6** | 对照组（关闸门）能检出锥内切换 | **> 0** | 证明 C4 不是空转 |

- `display_driver == "headless"` → 直接判 **`INVALID_NO_GPU`**，帧时间无意义。
- C1/C2 的**偏差**同时给出：与推算最坏帧 **10.68ms** 的差值和倍数。

### FAIL 时怎么办

报告末尾的 **§8 超预算子项**会点名到具体子项。建议的优化顺序（与 `engine-evaluation.md §E8-R2` 一致）：

1. **基础场景**（`P0_base` 本身就高）→ 先砍可自由降级项：阴影分辨率/距离 → 静态 prop 密度 → 贴图 mip / 流送距离 → 模型 LOD 与面数 → 后处理
2. **色彩矩阵**（GT 730 上实测最贵的一次 fullscreen pass）→ 检查是否走了 BackBufferCopy；必要时降为半分辨率或合进 tone mapping
3. **WorldSignal 痕迹** → 降贴花分辨率与密度，但 **≥1 处代表元素不得关闭**
4. **最后**才碰 `accessibility.md §7.3` 的 7 项不可关闭通道——**它们不得关闭**

> ⚠️ 任一超支 → 走**预算复议**，**不得就地放宽**（S2 §8 原话）。

---

## 8. 已知限制（不要把"没测"当"测过了"）

1. **气泡不是 1 draw call**：纯 GDScript 做不到 `accessibility.md §7.1` 承诺的「≤72 quad / 1 draw call / 图集 instanced」，
   本包每个气泡是一个 `Sprite3D`（一次 draw call）。因此**气泡成本会高于真实实现**（保守方向，不是放水方向）。
2. **static prop 用 300 个 Box 近似**真实场景的 DC/顶点压力，不是最终美术资产。
3. **FXAA 在 OpenGL3（Compatibility）后端静默失效**——报告 `fxaa_applied=false` 会记账；此时 P6 增量**不含 FXAA**，
   请不要把该数字当成"FXAA 成本"。GTX 1060 走 Vulkan/D3D12 时 FXAA 生效。
4. **无 GPU timestamp**：全部是帧时间差分，会包含引擎/驱动/呈现抖动，不是纯 GPU 时间。
5. **未测仿真侧 CPU**（4.880ms 是 E4 常量加回），也未测 simcore 集成。

---

## 9. 结果带回来放哪

把 `results/` 下的两个文件拷回仓库：

```
tools/gpu-smoke/results/result.json
tools/gpu-smoke/results/report.md
```

然后（由主程执行，本包不做 git 操作）：
1. 用实测值替换 `engine-evaluation.md §E4.2` 的三行 🟨 `新分配`（阴影图 / 主 pass / 天空）
2. 关闭 `docs/architecture/engine-evaluation.md §9` 的闸门 **G-b**
3. 更新 ADR-001 §6 的 R2
