# ADR-001 · 引擎选型与仿真核边界

- **状态**：**✅ 已确认生效** —— 用户已于 **2026-09-13** 确认：① 采纳方案 E；② **引擎版本钉 `Godot 4.5-stable`**；③ 将另备一台 GTX 1060（或等价）机器做渲染冒烟
- **上一状态**：已决策（待用户确认）→ PHASE3-003 提出，PHASE3-004 经用户确认生效
- **日期**：用户确认 **2026-09-13**（Phase 3 · PHASE3-004）· 草案 PHASE3-001 · 定案 PHASE3-003
- **决策人**：engineering-lead（程基岩）提出 → 主理人游承峰调度 → **用户确认为最终生效条件**
- **决策依据**：`docs/architecture/engine-evaluation.md`（8 条条件评估矩阵 + 逐条证据）
- **实测证据包**：`tools/spike/`（`VERSIONS.txt` · `logs/` · `abi-probe/` · `win32-tri/` · `godot-spike/`）
- **影响面**：全部代码仓库、构建管线、CI、工具链、招人与工期
- **相关**：ADR-002（RNG）· ADR-003（离屏 LOD / tick 边界）· ADR-004（事件总线）· ADR-005（milli 定点）· `06-consistency-review.md §七-A`
- **桥接手册**：`docs/architecture/godot-cpp-bridge.md`（**不降级路径**，已实测跑通）· `docs/architecture/gpu-smoke-env.md`（换机冒烟环境清单）

---

## 1. 背景（Context）

《枯水期》Core 的技术需求不是"做一个开放世界 RPG"，而是"做一个**可逐位复现、可导出证据、可在低配机上跑完整社区模拟**的开放世界 RPG"。约束全部来自已拍板的设计文档：

| # | 约束 | 出处 | 对引擎的含义 |
|---|---|---|---|
| K1 | 逐位确定性：`worldHash` 每 60 tick 比对，两次运行须**逐位相同** | S0 §C3 | 仿真不得依赖引擎的浮点、随机数、deltaTime、线程调度 |
| K2 | 禁 `double` 与超越函数（sin/exp），三角函数走查表 + 定点 | S0 §C7-3 | 引擎数学库必须可绕过 |
| K3 | 判定必须在固定 tick 发起，**禁在渲染帧发起** | S0 §C2 红线④ | 引擎帧循环不得驱动判定 |
| K4 | 仿真（非渲染）≤6ms/帧；场内实体 ≤120，full-behavior ≤32 | Core 验收 5 | 仿真要能被单独剖分与压测 |
| K5 | 目标机 GTX 1060 6GB / 16GB RAM / SATA SSD / 1080p，2×2km、第三人称近视角 | 任务硬约束 | 渲染压力低，重型管线（Nanite/Lumen）无收益 |
| K6 | C8 进 Core：调度可视化面板 + 归因链导出 + 固定种子确定性重放 | 概念文档 C8 | 工具链是一等公民 |
| K7 | 10 日 save/load 差异 ≤5%，需 headless 批量重放做回归 | Core 验收 6 | 仿真必须能脱离渲染独立运行 |
| K8 | 小团队、Core 期禁止新增系统 | 概念文档红线 | 迭代速度与可维护性 > 画面上限 |

**分析支点（未变）**：K1/K2/K3 共同推出一条架构结论——**确定性仿真必须是"引擎无关的纯数据模块"**。
一旦把仿真写进引擎的 `MonoBehaviour`/`Node`/`Actor` 生命周期，就会被引擎的 deltaTime、帧率、对象遍历顺序污染，K1 立刻崩塌。而 K7 又要求这个模块能在 CI 里 headless 跑 10 日（14400 tick）。

**真正要拍板的从来不是"用哪个引擎"，而是两件事**：

- **Q1**：仿真核是否做成**独立于引擎的库**（`simcore`）？
- **Q2**：表现层用哪个引擎？

**本次 PHASE3-003 的新增要求**：选型必须**通过可执行证据**定下来（而不是继续论证）。
因此本 ADR 的每一条结论都指向 `engine-evaluation.md` 与 `tools/spike/` 里的实测或可核查来源。

---

## 2. 决策（Decision）

> ### ✅ **决定（用户 2026-09-13 确认生效）：采纳方案 E。**
>
> **`simcore` = C++ 静态库 + C ABI，引擎无关；表现层 = Godot 4，引擎版本钉定 `4.5-stable`。**
>
> **一句话理由**：我们的难点是"确定性 + 可观测 + 低配"，不是"画面"；把仿真做成引擎无关的库，
> 可以同时满足 K1（确定性可控）、K7（CI headless 回归）与 K8（小团队不被引擎绑死），
> 而 Godot 4 的轻量（免安装、MIT、对 GTX 1060 友好）正好匹配 K5 的渲染预算。

**为什么钉 4.5-stable 而不是 4.7.2-stable**（用户 2026-09-13 拍板的理由）：

| 理由 | 说明 |
|---|---|
| **godot-cpp 有匹配 tag** | `godot-cpp` 有 `godot-4.5-stable` tag；4.7.2 没有匹配 tag，绑定面只能靠手写 |
| **绑定稳定 > 引擎新特性** | Core 期用不到 4.6/4.7 的任何新渲染特性；宁可要"有 tag 可依"的稳定绑定 |
| **已实测闭环** | 4.5-stable 二进制已取得并 SHA512 核验；**godot-cpp 4.5-stable 已用 MSVC 编出，GDExtension 在 Godot 4.5 中加载成功，GDScript 已调到 `simcore` 函数**（`logs/bridge-godot45-call.txt`） |

**状态说明**：本 ADR 已由用户确认为**最终生效**。**实现已启动**（桥接层最小链路已打通）。

### 2.1 决策要点（含本次实测带来的**重大事实更正**）

| # | 决策要点 | 证据 |
|---|---|---|
| **D1** | 仿真核 `simcore` 独立成库，**C ABI 边界**，引擎不得进入仿真循环 | `tools/spike/abi-probe/` · `logs/abi-probe-mingw.txt`（`ABI_PROBE_OK`、`frame_dt_irrelevant=1`）；`engine-evaluation.md §E2` 的 9 函数集 + 11 条反例 |
| **D2** | 表现层 = **Godot 4.5-stable**（**用户 2026-09-13 拍板**；`4.5.stable.official.876b29033`，zip SHA512 与官方 `SHA512-SUMS.txt` 逐字一致） | `tools/spike/VERSIONS.txt §1` · `logs/godot-SHA512-SUMS-4.5-stable.txt` · `logs/bridge-godot45-call.txt` |
| **D2b** | **桥接层走 godot-cpp（不降级）**：godot-cpp `godot-4.5-stable` 已用 MSVC 19.44 编出静态库；最小 GDExtension 在 Godot 4.5 中加载成功，GDScript 已调到 `simcore` 函数（`world_hash=334314409100154065`，两次一致） | `docs/architecture/godot-cpp-bridge.md` · `tools/spike/bridge/` · `logs/bridge-godot45-call.txt` · `logs/godot-cpp-build-cmake-success.txt` |
| **D3** | **本机工具链事实更正**：除了 `C:\MinGW\bin\g++`(GCC 6.3.0，**仅 C++14**)，本机**已有 MSVC 19.44 + cmake 4.1.1 + ninja 1.12.1 + Win SDK 10.0.26100**（`D:\VS2026`）→ 桥接层（需 C++17）**有编译路径** | `logs/environment-probe.txt` · `logs/simcore-test-msvc.txt` |
| **D4** | `simcore` 保持 **C++14 兼容子集**，双工具链可编（GCC 6.3.0 与 MSVC 19.44 结果**逐位相同**）；桥接层用 MSVC `C++17`。出货构建统一 MSVC，simcore-only CI 保留 MinGW | `CROSS_TOOLCHAIN_IDENTICAL`（`logs/simcore-cli-verify-{gcc,msvc}.txt` 的 diff 为空） |
| **D5** | 下载/依赖策略：Godot 走**校验和锁定**的归档（本机 github release 主机被代理拦截，走加速器或镜像） | `VERSIONS.txt` §1（含 502 失败记录与可用路径） |
| **D6** | GTX 1060 渲染预算 = **可复现推算**，真机冒烟为**放行闸门**（本开发机是 GT 730，**不能**替代） | `engine-evaluation.md §E4` · `logs/win32-gpu-enumerate.txt` |

### 2.2 若采纳，一并生效的三条硬规则（沿用草案，未改）

- **R1 · 仿真/表现单向依赖**：`simcore` 不得 `#include` 任何引擎头；表现层不得写仿真状态（只能读快照 + 提交输入）。违反即在 CI 静态检查失败。
- **R2 · 时间只从 `simcore` 出**：表现层的 `delta` 只作为"累加器喂料"，不得用于任何判定、插值之外的逻辑（ADR-003 §4）。**本次已实测反证**：把 frame_dt 从 1/60 改成 9999 喂进 ABI，判定结果不变。
- **R3 · 引擎选型可延后/可切换**：因为 `simcore` 是 C ABI 静态库，表现层切换（Godot → Unity → 其它）的成本被限制在 **桥接层 + 面板 UI**（量化见 `engine-evaluation.md §E7`：**40–75 人日，仿真 0% 重做**）。

---

## 3. 备选方案与被否决理由（**将来复盘选型的唯一依据**）

> 纪律：**被否决的方案必须写清否决理由**，不能只写"不选"。否决分两类——
> **契约否决**（与已定 ADR-002~005 或 K1–K8 冲突，硬否决）与 **证据否决**（本机无法实测/成本不成立）。

### 3.1 契约否决（硬否决）

- **方案 A · 纯 Godot 4 单体（仿真用 GDScript 或 C++ 引擎模块）** → **否决**
  - **理由**：仿真若落在引擎内，**ADR-005 的 milli 定点**（禁 `float`/`double`）、**ADR-002 的 `pcg32_at` 无状态纯函数**、
    **ADR-003 的 `absTick` 锚定边界判定**都要在引擎内重做；且引擎帧循环会驱动判定（**违反 K3 / R2**）。
  - **代价**：已可运行的 `simcore`（**10/10 用例、两套工具链逐位一致**）**全部作废**。
  - 备注：这与"Godot 不好"无关——是**单体形态**与确定性契约不兼容。

- **方案 B · 纯 Unity 6** → **否决（作为单体形态）**
  - **理由**：C#/IL2CPP 的逐位一致是"通常可行"而非"保证"；要保证就得把仿真做成 native 插件
    ——**那已经是方案 E′，不是单体**。单体外壳下 K1 无保证。
  - 备选地位：**E′ = 同一份 `simcore` + Unity 6 表现层**是**等价备选**（R3 保证），
    仅当"团队已有强 Unity 积累"时才更优。
  - 证据否决：本机**未安装** Unity，E1 无法实测。

- **方案 C · 纯 Unreal 5** → **否决**
  - **理由 1（K5 直接冲突）**：Nanite/Lumen 在 GTX 1060 6GB 上不可用于生产；全关之后又回到"为什么不用更轻的引擎"。
  - **理由 2**：引擎浮点/线程模型最不透明，K1 最难保证；小团队负担最大。
  - **理由 3（成本）**：`$1M` 后 **5% 版税**（已核验，见 `engine-evaluation.md §E5`）。
  - 证据否决：本机未装（仅有 Epic Launcher）。

- **方案 D · 纯自研（自绘 + 自制工具链）** → **否决**
  - **理由**：需自建渲染、导航、UI、资源管线、编辑器——**违反 K8（Core 期禁新增系统）**，工期不成立。

### 3.2 证据否决 / 组合否决

- **方案 F1 · `simcore` + 自研极简渲染（SDL/D3D）** → 否决：仍需自建 UI/资源管线，违反 K8。
- **方案 F2 · `simcore` + Bevy（Rust）** → 否决：引入**第三语言**，本机无 Rust 工具链。
- **方案 F3 · 纯 Godot 4 headless-only** → 否决：**不构成表现层**（S9 的 606 行实现无处落地）。
- **方案 E′ · `simcore` + Unity 6** → **保留为等价备选，非否决**：若团队熟练度压倒一切，可切换；
  但**本机无法实测其 E1**，且需订阅成本与政策风险评估（`§E5`）。

### 3.3 为什么在 E / E′ 之间选 E

**`simcore` 的存在比引擎品牌重要一个数量级。** 在 E 与 E′ 之间，选择依据被限定为**"本机可实测"与"团队熟练度"**，
不用技术理由互驳（仿真侧风险已被 R1/R3 隔离）。落在 E 的决定性事实是：
**Godot 4.5-stable 在本机**已实测**跑通（headless + 窗口 + GDExtension 加载），而 Unity 在本机**未安装、无法实测**。

---

## 4. 后果（Consequences）

### 4.1 正面

- **确定性风险从"全局"收敛到"一个可在 CI 里 headless 验证的库"**，且**已被实测**：
  两套工具链（GCC/MSVC）`worldHash` 逐位相同 → "确定性"不再依赖单一编译器行为。
- **10 日重放回归可在无渲染下跑**：`simcore_cli verify` 本机实测毫秒级；CI 每次提交可跑。
- **预算可剖分**：`simcore` 计时与引擎帧计时天然分离，验收 5 可分别举证。
- **引擎升级/换引擎不波及仿真**：量化到 **40–75 人日**（`§E7`），仿真重做 **0%**。
- **TCO 明确占优**：Godot = MIT，无版税/订阅/per-seat（许可证全文已抓取）。
- **C ABI 机制已跑通**：C++ 静态库 + `extern "C"` + 纯 C 消费者，`ABI_PROBE_OK`。

### 4.2 负面 / 成本

- **需维护两套构建**（`simcore` + 引擎工程）与一个 C ABI 桥接层。
  *缓解*：CI 把 `simcore` 编成静态库，引擎工程只链库；桥接层估 1.5–2.5k 行。
- **表现层与仿真层的双份状态**（渲染插值 vs 仿真真值）需显式同步协议。
  *缓解*：`ViewSnapshot` 双缓冲 + 只在 tick 边界发布（`architecture.md §10 R5`）。
- **`simcore` 用 C++14 子集**（受本机 MinGW 制约）→ 不能用 C++17/20 便利特性。
  *缓解*：C++14 子集已足够（现有代码 10/10 通过）；且**双工具链验证**是净收益。
- **工具链分裂**（出货 MSVC / CI MinGW）。
  *缓解*：出货统一 MSVC；CI 两条都保留且断言 `worldHash` 逐位一致（**已实现**）。见 §6 R3。
- **Godot 3D 工具链弱于 Unity/Unreal**。
  *缓解*：本项目 2×2km 近视角、规模小、且主动砍掉全部重型管线（`accessibility.md §7.2/§9`），风险低。
- ~~**GDExtension 桥接的版本配套问题**（godot-cpp 最新 tag 4.5 vs 引擎 4.7.2）~~ → **已解决**：引擎钉 4.5-stable + godot-cpp `godot-4.5-stable` tag，二者匹配；已实测构建与加载全通（§2.1 D2b）。

### 4.3 中性但必须记账

- 若将来改用 E′（Unity 表现层），**R1/R2/R3 同样适用**，只是桥接层换成 P/Invoke / C++ 插件；**仿真预算数字不变**。
- **本 ADR 的实测结论有 2 项"沙箱外待验"**（§7），必须在实装前闭合。

---

## 5. 受影响文档 / 需回填项

| 文档 | 需要做什么 | 阻塞级别 |
|---|---|---|
| `docs/architecture/engine-evaluation.md` | **本次新建**：8 条件矩阵 + 逐条证据 | ✅ 已完成 |
| **`tools/spike/VERSIONS.txt`** | **版本变更同步**：`[1]` 节由 4.7.2-stable 改为 **4.5-stable**（新版本/体积/SHA512 已实测写入；4.7.2 记录降级为"历史"） | ✅ 已同步（PHASE3-004） |
| **`README.md`** | **版本变更同步**："当前进度"的引擎选型改为已确认；"已知阻塞与放行闸门"里 **放行闸门 1（godot-cpp 构建）已关闭**；新增引擎行 `Godot 4.5-stable` | ✅ 已同步（PHASE3-004） |
| `docs/architecture/architecture.md` | 依本 ADR 定稿 L0–L4 分层；`§2.3` 引擎行由"待拍板"改为"**Godot 4.5-stable**"；`§11.1 G5` 关闭 | 本 ADR 生效后 |
| `docs/architecture/control-manifest.md` | 静态依赖检查项（禁引擎头 / 禁渲染帧判定）**已写入，可启用** | 本 ADR 生效后启用 |
| `build.sh` / `build.bat` / CI | 增补 **MSVC 流水线**（`/utf-8 /std:c++14`）与 **Godot headless 脚本回归**；Godot 二进制按 SHA512 vendor（**4.5-stable**） | 实装前 |
| **`docs/architecture/godot-cpp-bridge.md`** | **本次新建**：不降级路径手册（前置条件 / 三步命令 / 9 个已知坑 / 验收标准 / 降级代价量化） | ✅ 已交付 |
| **`docs/architecture/gpu-smoke-env.md`** | **本次新建**：换机 GPU 冒烟环境清单（硬件下限 / 驱动 / 拷贝清单 / 离线路径 / 1-2-3 步骤 / 结果带回） | ✅ 已交付 |
| **`tools/gpu-smoke/`** | **本次新建**：纯 GDScript 换机冒烟包（零编译），用于闭合 R2 | ✅ 已交付（**目标机运行结果待用户换机后回填**） |
| `docs/engine-reference/godot/VERSION.md` | 建议补：4.5-stable 版本/SHA/渲染回退链/已知坑（`/utf-8`、MinGW 误探测、Windows 命令解释器派生限制） | 建议本周期 |
| `docs/engine-reference/godot/VERSION.md` | **新建**：记录本次实测的版本/SHA/渲染回退链/已知坑（`/utf-8`、godot-cpp 滞后） | 建议本周期 |
| `design/gdd/systems/09-presentation-ux.md` | 无需改（S9 只读 `ViewSnapshot`，与引擎无关） | — |
| `src/simcore/README.md` | 补一句"已实测两套工具链逐位一致" | 低 |

---

## 6. 选型后的 Top 3 风险与缓解

（详版见 `engine-evaluation.md §E8`，此处为 ADR 级摘要）

| # | 风险 | 概率/影响 | 缓解（要点） | 放行条件 |
|---|---|---|---|---|
| **R1** | ~~godot-cpp（GDExtension 绑定）版本配套与构建~~ → **已闭合（2026-09-13）**：引擎钉 4.5-stable + godot-cpp `godot-4.5-stable`；**CMake+Ninja+MSVC 已编出静态库（1025 目标全过），GDExtension 在 Godot 4.5 中加载成功，GDScript 已调到 `simcore` 函数** | ~~中/中高~~ → **低** | ① 已钉版本（用户拍板）；② **不降级**，走 godot-cpp；③ 把 `tools/spike/bridge/build-bridge.sh` 纳入 CI | ✅ **已达成**：`LOWWATER_BRIDGE_OK` + `world_hash=334314409100154065`（两次一致）。残留：把构建脚本接进 CI 并 vendor 产物 |
| **R2** | **GTX 1060 渲染预算未实测**（本机为 GT 730） | 中 / 高 | 用户已确认**换一台 GTX 1060（或等价）机器**做冒烟；`tools/gpu-smoke/` 包已就绪（纯 GDScript，零编译）；清单见 `docs/architecture/gpu-smoke-env.md` | 最坏帧实测 **≤16.7ms**（p95）且渲染侧信号 **<1.20ms**；7 项不可关闭通道在最低画质下全部存在。**本包在目标机上的运行结果未验证——开发机是 GT 730** |
| **R3** | **工具链分裂**（simcore GCC / 桥接 MSVC） | 低 / 中 | ① 出货统一 MSVC；② simcore-only CI 保留 MinGW（**已实测逐位一致**，故安全）；③ CI 固化开关（MSVC 必带 `/utf-8`，否则中文常量报 `C2001`） | CI 同时保留两条流水线，`worldHash` 逐位一致 |

> **次级风险（已记录，不进 Top 3）**：Godot 编辑器二进制需 vendor（网内 github release 主机被代理拦截，
> 走加速器/镜像）；`AttributionLog` 内存 1.9MB 偏大（与引擎无关）；S8 区域内容缺位导致路程常量表缺位。

---

## 7. 知识缺口与"沙箱外待验"（**不臆造，逐条留痕**）

1. ~~**godot-cpp 完整构建未在本沙箱跑通**~~ → **已跑通（2026-09-13）**。
   路径：**CMake 4.1.1 + Ninja 1.12.1 + MSVC 19.44**，加 `-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`
   绕开"链接步骤被包进 Windows 命令解释器"的沙箱限制；1025 个目标全过。
   随后 `cl` + `link` 编出 `lowwater_bridge` DLL，Godot 4.5 加载成功、**GDScript 调到 simcore 函数**。
   上一轮 SCons 的三条失败日志保留作对照（`logs/godot-cpp-build-attempt{1,2,3}*.log`），不删改。
   **残留知识缺口**：SCons 路径仍未跑通（同一沙箱限制）；官方 SCons 路径
   （`scons platform=windows target=template_debug arch=x86_64 use_mingw=no`）待在普通终端复核。
   ⚠️ **已发现文档错误**：godot-cpp 4.5 **已移除 `api_version` 选项**，旧文档里的 `api_version=` 不要再写。
2. **GTX 1060 实机渲染冒烟未做**（**唯一未闭合的放行闸门**）。本机是 GT 730（`logs/win32-gpu-enumerate.txt`），
   Godot 在本机走 OpenGL 3.3 回退（Vulkan/D3D12 不可用）。E4 的 3 行"新分配"值须由真机回填。
   **已就绪**：`tools/gpu-smoke/`（纯 GDScript，零编译）+ `docs/architecture/gpu-smoke-env.md`（换机清单）。
   **本机已验证**：工程可被 Godot 4.5 解析；headless 与真实 GPU 两种模式都能跑完整 9 阶段并产出
   `result.json` / `report.md`（GT 730 实测最坏帧 29.1ms，**FAIL，符合"本机非目标机"预期**，
   样例见 `tools/gpu-smoke/_reference/gt730-2026-09-13/`）。**未验证**：GTX 1060 上的数字。
3. **Godot 4.x 的 GDExtension ABI 稳定性**：本次以实测替代了"版本基线文档"；
   仍建议补 `docs/engine-reference/godot/VERSION.md`。
4. **Unity 6 的浮点确定性边界**：仅在**将来重评 E′**时才需要官方/实测证据；本次不作判断（未核验不写数字）。
5. **Unity 定价页本机不可达** → `§E5` 中 Unity 一栏标"待核"，不以记忆充数。

---

## 8. 下一步（"表现层第一行代码怎么开始"——本 ADR 必须回答的问题）

选型已定，路径如下（**可执行**）：

1. ~~**闭合 R1**~~ → **已完成**。照 `docs/architecture/godot-cpp-bridge.md` 的**三步**即可复现：
   ① 开 VS 开发者命令行（`vcvarsall.bat x64`）；② `bash tools/spike/bridge/build-bridge.sh`；
   ③ `<godot4.5> --headless --path .../godot-test --import` 然后 `--script res://test.gd`。
   **收尾**：把该脚本接进 CI，并把 godot-cpp 产物 vendor。
2. **写第一行桥接代码**：新建 `src/bridge/`，产出 `liblowwater_gdextension`，
   暴露 **1 个**函数先打通链路：
   ```cpp
   // src/bridge/simcore_extension.cpp  （C++17，MSVC 构建）
   #include <godot_cpp/classes/ref_counted.hpp>
   #include "simcore/simcore.h"
   // GDExtension 类 LowWaterSim：
   //   _ready()      -> simcore_init(worldSeed, &params)
   //   _process(dt)  -> simcore_advance(dt)          // 唯一时间入口（R2）
   //   get_view()    -> 把 simcore_view_snapshot() 投影成 Godot 侧极简结构
   ```
   **验收**：Godot headless 跑 `--script`，能 `print` 出 `simcore_version()` 与一次 `worldHash`。
3. **启动 R2**：用户换机后跑 `tools/gpu-smoke/run-smoke.bat`，产出实测帧预算表（约 2 分钟）。
   带回归档：`tools/gpu-smoke/results/{result.json,report.md}`。
4. **CI**：把 `simcore` 回归（已有）与 `godot --headless` 脚本回归接进同一流水线；
   Godot 二进制按 SHA512 vendor。
5. **回填**：按 §5 更新 `architecture.md`（引擎行 + G5 关闭）与 `control-manifest.md`（启用 B1/B4）。

---

## 9. 用户确认记录与遗留问题

### 9.1 用户已确认（2026-09-13，PHASE3-004）

| # | 问 | 用户答复 | 落地 |
|---|---|---|---|
| **Q1** | 是否确认**方案 E**（`simcore` 引擎无关 + Godot 4 表现层）？ | ✅ **确认** | 本 ADR 状态改为"已确认生效" |
| **Q2** | 是否接受本机工具链事实更正（有 MSVC/cmake/ninja）并把桥接层定为 **MSVC C++17**？ | ✅ **确认**（并据此实测跑通了 godot-cpp） | 见 `godot-cpp-bridge.md` |
| **Q3** | 引擎版本钉 4.7.2-stable（无匹配 tag）还是 **4.5-stable**（有匹配 tag）？ | ✅ **钉 `4.5-stable`**（理由：godot-cpp 有匹配 tag、绑定稳定；Core 期用不到 4.6/4.7 新特性） | §2 决策；`tools/spike/VERSIONS.txt §1` |
| **Q4** | 是否提供一台 GTX 1060（或等价）机器做渲染冒烟？ | ✅ **确认会换机** | `tools/gpu-smoke/` + `gpu-smoke-env.md` 已就绪 |
| **Q5** | 若团队已有强 Unity 积累，是否改用 **E′**？ | 未选择 E′（维持 E） | R3 仍保证可切换（40–75 人日） |

> 用户额外提出的关键诉求：**"我如何做才能不降级使用"** → 交付 `docs/architecture/godot-cpp-bridge.md`，
> 并以**实测跑通**（而非论证）作答：godot-cpp 已编出、GDExtension 已加载、GDScript 已调到 `simcore` 函数。

### 9.2 遗留待办（按阻塞级别）

| # | 待办 | 卡在谁 | 阻塞什么 |
|---|---|---|---|
| A1 | **在 GTX 1060（或等价）机器上跑 `tools/gpu-smoke/`** 并回填 E4 三行 🟨 | 用户（换机） | **R2 · 唯一未闭合的放行闸门** |
| A2 | 把 `tools/spike/bridge/build-bridge.sh` 接进 CI，vendor godot-cpp 产物 | 主程 | 桥接层可重复构建 |
| A3 | SCons 官方路径在**普通终端**复核（`scons platform=windows target=template_debug arch=x86_64 use_mingw=no`） | 主程 | 备选构建路径的可用性（当前只有 CMake 路径被验证） |
| A4 | 补 `docs/engine-reference/godot/VERSION.md` | 主程 | 引擎版本基线文档 |
| A5 | 回填 `architecture.md §2.3` 引擎行 + 关闭 `§11.1 G5` | 主程 | 文档一致性 |
| A6 | 若将来重评 E′（Unity），Unity 定价页本机不可达 → 需以官方公告核实 | 主理人/用户 | 仅影响未来重评 |

## 10. 一页速查

```text
决策      方案 E —— simcore(C++ 静态库 + C ABI, 引擎无关) + Godot 4 表现层
状态      ✅ 已确认生效（用户 2026-09-13）· 实现已启动
引擎版本  Godot 4.5-stable (official, 876b29033) · MIT · zip 76,130,638 B (72.6 MiB)
          zip SHA512 = e8ef4867…e9aa1a（与官方 SHA512-SUMS.txt 逐字一致）
钉版理由  godot-cpp 有 godot-4.5-stable 匹配 tag · 绑定稳定 > 引擎新特性 · 已实测闭环
硬规则    R1 单向依赖 │ R2 时间只从 simcore 出 │ R3 引擎可切换
实测佐证  Godot 4.5 headless/窗口/GDExtension 加载均跑通 │ simcore 两工具链逐位相同 │ C ABI PROBE_OK
桥接      ✅ godot-cpp 4.5-stable 已编出（CMake+Ninja+MSVC）→ 不降级
          ✅ GDScript → godot-cpp → simcore：world_hash=334314409100154065（两次一致）
否决      A 纯Godot单体(违反ADR-005/002/K3) │ B 纯Unity单体(K1无保证) │ C 纯UE5(K5冲突+5%版税) │ D 纯自研(K8)
放行闸门  R1 ✅ 已闭合 │ R2 ❌ 未闭合 —— GTX 1060 冒烟待用户换机
证据位置  docs/architecture/{godot-cpp-bridge.md, gpu-smoke-env.md, engine-evaluation.md}
          tools/spike/{VERSIONS.txt, logs, abi-probe, bridge} · tools/gpu-smoke/
```
