# tools/spike · ADR-001 引擎选型实测证据包

- **产出**：PHASE3-003（引擎选型决策）
- **配套文档**：`docs/architecture/engine-evaluation.md`（评估矩阵）· `docs/architecture/adr/ADR-001-engine-selection.md`（终版 ADR）
- **纪律**：本目录**只放实测产物**。凡未跑通的，日志里保留失败原文，不美化、不删改。

---

## 0. 这个目录证明了什么（一句话索引）

| 结论 | 证据文件 |
|---|---|
| Godot 4.7.2 能在本机**获得**（SHA512 与官方一致）并**运行**（headless + 窗口 OpenGL） | `logs/godot-version.txt` · `logs/godot-headless-run*.txt` · `logs/godot-windowed-opengl3.txt` · `VERSIONS.txt` |
| 本机**并非**"只有 MinGW、无 MSVC/cmake/ninja"——VS2026 带 cl/cmake/ninja | `logs/environment-probe.txt` |
| `simcore` 在 **两套工具链**上均 10/10 通过，且 `worldHash` **逐位相同** | `logs/simcore-test-gcc.txt` · `logs/simcore-test-msvc.txt` · `logs/simcore-cli-verify-{gcc,msvc}.txt` |
| **方案 E 的 C ABI 机制**在本机跑通（C++ 静态库 + `extern "C"` + 纯 C 消费者） | `abi-probe/` · `logs/abi-probe-mingw.txt` |
| 引擎时间**进不了**仿真（把 frame_dt 喂进 ABI，判定结果不变） | `logs/abi-probe-mingw.txt`（`frame_dt_irrelevant=1`） |
| headless 无 GPU 跑确定性回归（仿真侧 + 引擎侧） | `logs/simcore-cli-verify-gcc.txt` · `logs/godot-headless-run1..3.txt`（3 次同 hash） |
| 本机图形栈可达（最小 Win32 窗口 + 三角形） | `win32-tri/` · `logs/win32-triangle-mingw.txt` |
| 本机 GPU 是 GT 730，**不是**目标机 GTX 1060 | `logs/win32-gpu-enumerate.txt` |
| Godot 是 MIT（无版税/订阅） | `logs/godot-LICENSE-4.7.2-stable.txt` · `logs/sources-tco-licensing.txt` |
| **未能跑通**：godot-cpp（GDExtension 桥）完整构建 | `logs/godot-cpp-build-attempt{1,2,3}*.log` |

---

## 1. 目录结构

```
tools/spike/
├── README.md                 ← 你在这里
├── VERSIONS.txt              版本指纹（Godot 版本/体积/SHA512、工具链、GPU）
├── godot-spike/              Godot 最小工程（headless 确定性探针）
│   ├── project.godot
│   ├── spike.gd              headless: 打印版本 + display_driver + 固定种子 hash
│   └── gpu.gd                打印 display_driver / 显卡适配器名 / GL API 版本
├── abi-probe/                方案 E 的最小 ABI 证明
│   ├── simcore_abi.cpp       C++ 静态库 TU：包 simcore 头，暴露 extern "C"
│   ├── consumer.c            纯 C 消费者：只经 C ABI 调用
│   └── build.sh              一键复现（MinGW 路径）
├── win32-tri/                本机原生图形栈最小证明
│   ├── tri.c                 Win32 窗口 + GDI 画三角形
│   └── gpu.c                 EnumDisplayDevices 枚举显卡/分辨率
└── logs/                     所有实测输出（每个文件首行是被执行的命令）
```

---

## 2. 怎么复现

### 2.1 前置：本机实际工具链（`logs/environment-probe.txt` 原文）

```text
MinGW g++   6.3.0                C:\MinGW\bin        （-std=c++14 可用；-std=c++17 无 <string_view>）
MSVC cl     19.44.35221          D:\VS2026           （toolsets 14.44.35207 / 14.50.35717）
cmake       4.1.1-msvc1          D:\VS2026\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
ninja       1.12.1               同上
Windows SDK 10.0.26100.0         D:\Windows Kits\10
```

> **重要**：任务书写的"只有 `C:\MinGW\bin\g++`；无 cmake / make / MSVC / ninja"与实测不符。
> 实测本机已具备完整 MSVC 工具链（含 cmake/ninja）。详见 `docs/architecture/engine-evaluation.md §E1`。

### 2.2 simcore 回归（headless，无需 GPU，无需 MSVC）

```bash
./build.sh                       # CI 门禁 + 编译 test_runner / simcore_cli（MinGW GCC 6.3.0）
./build/test_runner.exe          # 期望：10 PASS / 0 FAIL
./build/simcore_cli.exe verify --seed 20250910 --days 5
                                 # 期望：RESULT: PASS（快进 ≡ 逐 tick，逐位相同）
```

### 2.3 用 MSVC 复跑 simcore（证明工具链无关）

```bash
# 在 D:\VS2026 的 Developer 环境里（或用 cl/cmake 自带环境）：
cl /utf-8 /std:c++14 /O2 /EHsc /I src tests/test_runner.cpp /Fe:build\test_runner_msvc.exe
```
> `/utf-8` 必需：源码含 UTF-8 中文常量，MSVC 默认按系统代码页解析会报
> `C2001: 常量中有换行符`。这是**文档化的编译开关**，不是代码缺陷。

### 2.4 C ABI 探针（方案 E 机制）

```bash
cd tools/spike/abi-probe
bash build.sh          # g++ 编静态库 -> gcc 编纯 C 消费者 -> 运行
# 期望：ABI_PROBE_OK；repeat_identical=1；frame_dt_irrelevant=1
```

### 2.5 Godot（网内 github release 主机被拦截，走加速器）

```bash
# 直连失败（实测）：curl: (56) CONNECT tunnel failed, response 502
curl -L -o godot.zip \
  "https://gh-proxy.com/https://github.com/godotengine/godot/releases/download/4.7.2-stable/Godot_v4.7.2-stable_win64.exe.zip"
sha512sum godot.zip   # 必须等于 VERSIONS.txt 里的值
unzip -o godot.zip -d godot_bin
godot_bin/Godot_v4.7.2-stable_win64_console.exe --headless --version

cd tools/spike/godot-spike
<godot> --headless --path . --script res://spike.gd      # 3 次应得到同一 hash
```

---

## 3. 已知未跑通项（诚实留痕）

| 项 | 状态 | 卡在哪 | 需要什么才能通 |
|---|---|---|---|
| **godot-cpp（GDExtension 桥）完整构建** | ❌ 未跑通 | 见 `logs/godot-cpp-build-attempt*.log`：<br>① SCons 自动探测到 MinGW g++ 6.3.0 → `static_assert: Minimum of C++17 required`；<br>② 强制 MSVC PATH 后 → SCons 的 MSVC 驱动在本**沙箱**内无法派生编译命令（`系统找不到指定的文件`）；本会话的沙箱安全策略禁止经 `cmd` 派生的子进程。 | 在**普通终端**（非本沙箱）跑 `scons platform=windows api_version=4.7`；或改用 godot-cpp 的 CMake 路径。**前置条件已全部满足**（MSVC C++17/20 直调 `cl.exe` 成功——见 `logs/simcore-test-msvc.txt`）。 |
| GTX 1060 渲染实测 | ❌ 未测 | 本机显卡是 **GT 730 / driver 391.35**（`logs/win32-gpu-enumerate.txt`），无 Vulkan ICD、无 D3D12 SM6；Godot 回退到 OpenGL 3.3。 | 一台物理 GTX 1060 6GB 机器，或等价机型。**列为 Core 早期放行闸门。** |

> 上表两项都**不影响** ADR-001 的选型结论（引擎已获得并运行、ABI 机制已跑通），
> 但**必须**记录，以免将来把"没测"当成"测过了"。
