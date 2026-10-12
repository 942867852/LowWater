# 换机 GPU 渲染冒烟 · 环境清单（带给另一台机器前请逐条对照）

- **对应**：`docs/architecture/engine-evaluation.md §9` 放行闸门 **G-b**（GTX 1060 渲染预算未实测）
- **要跑的包**：`tools/gpu-smoke/`（纯 GDScript，**零编译、零安装**）
- **包的详细用法**：`tools/gpu-smoke/README.md`
- **诚实声明**：本包在**开发机上没有 GTX 1060**（是 GT 730），因此**目标机上的运行结果本仓库无法预先验证**。
  开发机上已验证的是"管线通不通"，不是"预算够不够"。

---

## 1. 硬件下限（"GTX 1060 6GB 或等价"到底指什么）

| 项 | 目标机（推荐） | **下限（低于此则测试结果不可用）** |
|---|---|---|
| GPU | NVIDIA **GTX 1060 6GB** | **VRAM ≥ 4 GB**，且**支持 Vulkan 1.0 或 D3D12**。<br>参考等价：GTX 1050 Ti 4GB / GTX 970 4GB / RX 570 4GB / GTX 1650 4GB |
| 显示器 | **1920×1080 @ 60Hz** | 必须能稳定输出 1080p。**低于 1080p 的显示器请不要测**——像素数变了，帧预算结论不成立 |
| CPU | 任意 4 核以上（本项目渲染压力主要在 GPU/驱动，仿真不在本包内） | 4 核 |
| 内存 | 16 GB | **≥ 8 GB** |
| 硬盘 | SATA SSD | 任意（本包不读盘） |

> **为什么"支持 Vulkan 1.0 或 D3D12"是硬下限**：Godot 4.5 的三档渲染后端是
> **Vulkan → D3D12 → OpenGL3(Compatibility)**。落到 **OpenGL3** 的话，
> **FXAA 会静默失效**（`Screen space AA is currently unavailable on the Compatibility renderer`），
> 报告里 `fxaa_applied=false`，此时"FXAA 0.30ms"这一项**测不出来**，
> 与 E4 的预算表就不是同一口径了。GTX 1060 走 Vulkan 没问题。

---

## 2. 系统与驱动

| 项 | 要求 |
|---|---|
| 操作系统 | Windows 10 21H2 及以上（或 Windows 11）。**也可在 Linux 上跑**，但本包脚本优先照顾 Windows |
| NVIDIA 驱动 | **≥ 472.12**（GTX 1060 的最后一个"新特性分支"之一；**实测开发机上的 GT 730 是 391.35，太老，别照抄**）。<br>建议直接装**当前最新的 Game Ready 驱动** |
| 显卡模式 | **务必插在独显上**（台式机把显示器线接到 GTX 1060 的输出口，不要用主板集显输出） |
| 电源/性能 | Windows 电源计划设为**高性能**；笔记本请插电 |
| 跑测试时 | **关闭**游戏录制/直播/OBS/浏览器硬件加速/其它占 GPU 的程序 |

### 怎么确认驱动与后端（到了目标机上先跑这一条）

```bat
_bin\Godot_v4.5-stable_win64_console.exe --headless --version
```

应打印 `4.5.stable.official.876b29033`。
跑完冒烟后，`results/report.md` 的第 1 节会打印 GPU 型号、API 版本、**实际使用的渲染后端**与刷新率——
**先看这一节**，确认后端是 Vulkan 或 D3D12（不是 OpenGL），分辨率是 1920×1080。

---

## 3. 需要拷贝的文件清单

### ✅ 结论：**只需要拷 `tools/gpu-smoke/` 这一个目录**（不必拷整个仓库）

```
把 D:\CppProgram\LowWater\tools\gpu-smoke\   整个目录
      ↓
拷到 U 盘 / 移动硬盘 / 网盘
```

目录内已经**自包含**全部测试所需文件：

```
tools/gpu-smoke/
├── project.godot          工程配置（1080p / 无边框 / 关 vsync）
├── smoke.tscn             主场景
├── scripts/*.gd           4 个脚本（主流程 / 场景构造 / 图标生成 / 报告）
├── fetch-godot.bat/.sh    取引擎（含 SHA512 校验；已存在则跳过）
├── run-smoke.bat/.sh      一键运行
├── _bin\                  ← Godot 二进制放这里（首次用 fetch-godot 自动填）
└── results\               ← 跑完自动产出 result.json + report.md
```

**不需要**拷贝：`src/simcore/`（本包不测 simcore）、`docs/`、`tools/spike/`、任何 `.dll` / `.lib`。

> 带回来的东西也只要 `results\result.json` 和 `results\report.md` 两个文件（见 §6）。

---

## 4. 网络需求与离线路径

| 场景 | 做法 |
|---|---|
| **目标机有外网**（推荐） | 直接跑 `fetch-godot.bat`，它会自动下载 `Godot_v4.5-stable_win64.exe.zip`（72.6 MiB）并**核对 SHA512** |
| **目标机无外网** | 在**能上网的机器**上下载下面这个文件：<br>`https://github.com/godotengine/godot/releases/download/4.5-stable/Godot_v4.5-stable_win64.exe.zip`<br>下载后**务必核对 SHA512**：<br>`certutil -hashfile Godot_v4.5-stable_win64.exe.zip SHA512`<br>应等于：<br>`E8EF4867466BB01A2B762C4EC404F54266B40A5CBBA0EFA95E3AB325CD7D349B5DC11259E1C1525195080DC82294FF28044F7AC4ACAD833E4B054B4257E9AA1A`<br>解压后把**两个** exe（**缺一不可**）拷到 `tools\gpu-smoke\_bin\`：<br>`Godot_v4.5-stable_win64.exe`<br>`Godot_v4.5-stable_win64_console.exe`<br>然后 `run-smoke.bat` 会自动识别并跳过下载 |
| 下载很慢/被墙 | `fetch-godot` 内置备用通道 `https://gh-proxy.com/...`（上一轮实测可用）；两个通道都失败就走上面的离线路径 |

---

## 5. 最小操作步骤（1-2-3，非技术用户照做即可）

> 全程约 **5 分钟**（含下载）。跑测试时**不要动鼠标键盘**。

**1️⃣ 拷过去**
把 `tools\gpu-smoke` 整个目录拷到目标机（例如 `D:\gpu-smoke`）。

**2️⃣ 双击 `fetch-godot.bat`**
等它打印出 `4.5.stable.official.876b29033`。
（如果已经手动放过 Godot 到 `_bin\`，它会直接说"已存在，跳过下载"。）

**3️⃣ 双击 `run-smoke.bat`**
会弹出一个 1920×1080 的无边框窗口，画面里有 120 个小人、气泡、脚印/灯/沟槽/绊线，
相机自己绕圈，天色在晨/午/昏/夜之间换。**约 1–2 分钟后自动关闭**。

然后打开 `results\report.md`：

```bat
notepad results\report.md
```

- 第一行就是结论：**`PASS` / `FAIL` / `INVALID_NO_GPU`**
- 若出现 `INVALID_NO_GPU` → 说明 Godot 跑成了无 GPU 模式（多半是后端没起来），**结果无效，请检查驱动**
- 若 `FAIL` → 看报告最后的 **§8 超预算子项**，它会点名是哪个子项超了

### 想先确认能跑通（可选）

```bat
run-smoke.bat -- --frames=60
```

每阶段只采 60 帧，约 30 秒出结果，用来确认环境没问题。确认后再跑默认的完整版。

---

## 6. 结果怎么带回来

把目标机上这两个文件拷回来：

```
tools\gpu-smoke\results\result.json     ← 机器可读（给主程回填 E4 用）
tools\gpu-smoke\results\report.md       ← 人可读（给主理人/用户看）
```

**放回仓库的路径（原样覆盖即可）**：

```
D:\CppProgram\LowWater\tools\gpu-smoke\results\result.json
D:\CppProgram\LowWater\tools\gpu-smoke\results\report.md
```

> 若想保留多次结果，建议另存到
> `tools\gpu-smoke\_reference\<机器名>-<日期>\`（仓库里已有一个样例：
> `_reference\gt730-2026-09-13\`，是**开发机 GT 730 的 FAIL 参考输出**）。

---

## 7. 回来之后由主程做的事（不在你这一步）

1. 用实测值替换 `engine-evaluation.md §E4.2` 的三行 🟨「新分配」：`阴影图 1.10` / `主 pass 2.20` / `天空 0.30`
2. 回填 `information_channel_ms`（对照 **<1.20ms**）与 `worst_frame_p95_ms`（对照 **≤16.7ms**）
3. 关闭 `engine-evaluation.md §9` 的闸门 **G-b**，更新 `ADR-001 §6-R2`
4. 若 `FAIL`：走**预算复议**，**不得就地放宽**（S2 §8 原话）

---

## 8. 换机前最后检查一遍

- [ ] 显示器是 **1080p**（不是 1440p / 4K / 1366×768）
- [ ] 显示器线插在 **GTX 1060** 上，不是主板集显
- [ ] NVIDIA 驱动是**较新版本**（不是 391.x 这种老版本）
- [ ] 已拷贝 **整个 `tools\gpu-smoke` 目录**
- [ ] `_bin\` 里**两个** exe 都在（或目标机能联网让 `fetch-godot.bat` 自己下）
- [ ] 跑测试时关掉 OBS / 录制 / 浏览器硬件加速
- [ ] Windows 电源计划 = 高性能
