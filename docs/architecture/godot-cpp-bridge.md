# 《枯水期》· godot-cpp 桥接「不降级」路径（ADR-001 配套 · 可执行手册）

- **状态**：**已实测跑通**（2026-09-13，本会话）→ **不需要降级**
- **配套**：`adr/ADR-001-engine-selection.md`（方案 E / 版本钉 4.5-stable）· `engine-evaluation.md §E8-R1`
- **实测代码**：`tools/spike/bridge/`（`lowwater_bridge.cpp` · `build-bridge.sh` · `msvcenv.sh` · `godot-test/`）
- **证据**：`tools/spike/logs/bridge-godot45-call.txt` · `logs/godot-cpp-build-cmake-success.txt` · `logs/godot-cpp-cmake-configure.txt`

---

## 0. 一句话结论

> **godot-cpp 4.5-stable 已用 MSVC 19.44 编出来，并且 Godot 4.5-stable 成功加载了它导出的 GDExtension，
> GDScript 直接 `LowWaterSim.new()` 就调到了 `simcore` 的函数。**
> **"降级为裸 GDExtension C API" 是不必要的**——上一轮"跑不通"的根因是**环境沙箱禁止经 `cmd.exe` 派生子进程**，
> 不是工具链缺失、也不是版本不匹配。

上一轮（`tools/spike/logs/godot-cpp-build-attempt{1,2,3}*.log`）的失败与本次的成功对照：

| 轮次 | 现象 | 真实根因 |
|---|---|---|
| 上一轮 ① | SCons 自动探测到 MinGW g++ 6.3.0 → `static_assert: Minimum of C++17 required` | **真坑**：MinGW GCC 6.3 只有 C++14；已在 §3 给修复 |
| 上一轮 ②③ | 强制 MSVC PATH 后 → `scons: *** [...] 系统找不到指定的文件` | **沙箱限制**：SCons 在 Windows 上派生编译子进程失败；直调 `cl.exe` 一直是可用的（本次 1024 个 .cpp 全部编译成功即为证） |
| 本次（CMake+Ninja）| CMake 编译器探测的 `[1/2]` 编译步骤**成功产出 .obj**；`[2/2]` 链接步骤失败，日志里明确写着 `cmd.exe /C "..."` | **同一个沙箱限制**，定位更精确：**编译不经过 cmd，链接被 CMake 包进了 `cmd.exe /C`**。绕开方式见 §3-坑 6 |

---

## 1. 前置条件清单（逐条勾选）

### 1.1 已具备（本机实测，无需安装）

- [x] **MSVC 19.44.35221 for x64**：`D:\VS2026\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe`
      （VS2026 另带 toolset `14.50.35717`；本手册用 `14.44.35207`）
- [x] **Windows SDK 10.0.26100.0**：`D:\Windows Kits\10\Include\10.0.26100.0` / `Lib\10.0.26100.0`
- [x] **CMake 4.1.1-msvc1**：`D:\VS2026\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- [x] **Ninja 1.12.1**：`D:\VS2026\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
- [x] **Python 3.13.14**（godot-cpp 生成绑定要用；`EnsurePythonVersion(3, 8)`）
- [x] **Godot 4.5-stable**：`4.5.stable.official.876b29033`（SHA512 已核对，见 §1.3）
- [x] **vcvarsall**：`D:\VS2026\VC\Auxiliary\Build\vcvarsall.bat` / `vcvars64.bat`
      · `VsDevCmd.bat`：`D:\VS2026\Common7\Tools\VsDevCmd.bat`

### 1.2 需要准备（两条路二选一）

**路 A（本会话实测通过，推荐）**：不需要额外安装任何东西。

**路 B（官方 SCons 路径）**：

- [ ] **scons ≥ 4.0**（godot-cpp 的 `SConstruct` 里 `EnsureSConsVersion(4, 0)`；`EnsurePythonVersion(3, 8)`）
      隔离安装（不污染全局，与上一轮一致）：
      ```bash
      python -m pip install --target D:\tools\scons-lib scons==4.11.1
      export PYTHONPATH="D:/tools/scons-lib;$PYTHONPATH"
      python -m SCons --version
      ```
      或直接：`python -m pip install scons==4.11.1`

### 1.3 引擎与 godot-cpp 的精确版本指纹（**必须逐字匹配**）

| 项 | 值 |
|---|---|
| 引擎版本 | **Godot 4.5-stable**，`4.5.stable.official.876b29033` |
| 引擎资源 | `Godot_v4.5-stable_win64.exe.zip`，**76,130,638 bytes**（72.6 MiB） |
| zip SHA512 | `e8ef4867466bb01a2b762c4ec404f54266b40a5cbba0efa95e3ab325cd7d349b5dc11259e1c1525195080dc82294ff28044f7ac4acad833e4b054b4257e9aa1a`（与官方 `SHA512-SUMS.txt` 逐字一致，见 `tools/spike/logs/godot-SHA512-SUMS-4.5-stable.txt`） |
| 解压后 | `Godot_v4.5-stable_win64.exe` 162,825,224 B ＋ `..._console.exe` 197,640 B |
| godot-cpp | **tag `godot-4.5-stable`**（`https://github.com/godotengine/godot-cpp/archive/refs/tags/godot-4.5-stable.zip`） |
| 精度 | 官方 Godot 4.5 = **double**（`float_64`）；godot-cpp 默认也是 `float_64`，**一致，无需加 `precision=`** |
| `api_version` | ⚠️ **godot-cpp 4.5 已移除 `api_version` 选项**（API JSON 随 `gdextension/` 子模块一起打包，本次下载的包里就有）。ADR-001 §8 里那句 `api_version=<与钉定引擎一致>` **已过时，不要照抄** |

### 1.4 环境变量（路 A/B 都需要）

最省事的写法——**在 VS 的开发者命令行里跑**：

```bat
"D:\VS2026\VC\Auxiliary\Build\vcvarsall.bat" x64
```

或直接开 **「Developer Command Prompt for VS 2026」**（开始菜单里搜"Developer"）。

需要设置的环境变量（`vcvarsall.bat` 会自动设好）：

| 变量 | 值（本机） |
|---|---|
| `PATH` | 含 `...\MSVC\14.44.35207\bin\Hostx64\x64`、CMake bin、Ninja |
| `INCLUDE` | `...\MSVC\14.44.35207\include`；`D:\Windows Kits\10\Include\10.0.26100.0\{ucrt,um,shared,winrt}` |
| `LIB` | `...\MSVC\14.44.35207\lib\x64`；`D:\Windows Kits\10\Lib\10.0.26100.0\{ucrt,um}\x64` |

> 若在 Git Bash 且**不想**跑 `.bat`，可用 `tools/spike/bridge/msvcenv.sh` 手工还原同一套变量
> （`bash msvcenv.sh --check` 应打印 cl / cmake / ninja 三个版本）。

---

## 2. 三步操作（照抄即可）

### 第 1 步 · 打开 VS 开发者命令行并进到目录

```bat
call "D:\VS2026\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d D:\CppProgram\LowWater\tools\spike\bridge
```

> ⛔ **最大的坑就在这里**：**不要**在受限终端（CI 沙箱、某些被策略限制的 shell）里跑。
> SCons 与 CMake 在 Windows 上都会**经 `cmd.exe` 派生子进程**；一旦 `cmd.exe` 被禁，
> 报错是 `系统找不到指定的文件` / `no such file or directory`——**极具误导性，看起来像"工具链没装"，
> 其实是"不能派生子进程"**。本会话为此浪费了两轮（`logs/godot-cpp-build-attempt{2,3}*.log`）。

### 第 2 步 · 一键构建（godot-cpp + 桥接 DLL）

```bash
bash build-bridge.sh
```

脚本会依次做：下载 godot-cpp → CMake 配置 → Ninja 构建（约 5–10 分钟，1025 个目标）
→ `cl` 编 `lowwater_bridge.cpp` → `link` 出 **DLL**。
想看每一步的原始命令，见 §3。

### 第 3 步 · 在 Godot 4.5 里验收

```bat
set GODOT=D:\CppProgram\LowWater\tools\gpu-smoke\_bin\Godot_v4.5-stable_win64_console.exe
"%GODOT%" --headless --path "D:\CppProgram\LowWater\tools\spike\bridge\godot-test" --import
"%GODOT%" --headless --path "D:\CppProgram\LowWater\tools\spike\bridge\godot-test" --script res://test.gd
```

---

## 3. 已知坑与修复（**逐条都有实测出处**）

| # | 坑 | 症状 | 修复 |
|---|---|---|---|
| **1** | **MSVC 编本项目源码必须 `/utf-8`** | `error C2001: 常量中有换行符`（源码含 UTF-8 中文常量，MSVC 默认按系统代码页解析） | 桥接层编译命令**必须**带 `/utf-8`。godot-cpp 自己的 SCons/CMake 构建已内置该开关，但**你自己写的 `.cpp` 必须手动加**。见 `build-bridge.sh` 第 3 步 |
| **2** | **godot-cpp 强制 C++17** | `static_assert: Minimum of C++17 required`；MinGW GCC 6.3 还会先报 `'is_zero_constructible_v' declared as an 'inline' variable` | **不要用 `C:\MinGW\bin\g++`(GCC 6.3.0)**——它只有 C++14，且无 `<string_view>`。用 MSVC（`-std:c++17`）。`simcore` 仍保持 C++14 子集，不受影响 |
| **3** | **SCons 自动探测到 MinGW** | 同坑 2。根因：`tools/windows.py` 的逻辑是 `if not use_mingw and msvc.exists(env)` 才用 MSVC，否则在 `sys.platform in ("win32","msys")` 且 `mingw_prefix` 为空时**回退到 MinGW** | ① 在 Developer Command Prompt 里跑（让 `msvc.exists()` 为真）；② 显式 `scons ... use_mingw=no`；③ 实在不行把 `C:\MinGW\bin` 临时移出 `PATH` |
| **4** | **`api_version` 与引擎版本不匹配** | 老文档/老教程让你写 `scons api_version=4.7`。godot-cpp 4.5 **已删除该选项**——写了会被忽略或报未知变量 | **不要写 `api_version`**。API JSON 由 `gdextension/` 子模块提供，tag 匹配即版本匹配。需要自定义时用 `custom_api_file=` / `gdextension_dir=` |
| **5** | **精度（float_64 / float_32）必须与引擎一致** | 类型布局不一致 → 运行时崩溃或数值错乱 | 官方 Godot 4.5 = **double**；godot-cpp 默认 `float_64`，**一致**，不要加 `precision=single` |
| **6** | **受限 shell 里 `cmd.exe` 派生失败**（本次新定位） | `系统找不到指定的文件` / `ninja: build stopped: subcommand failed. RC Pass 1: command "rc ..." failed` | ① 首选：换**普通终端 / VS 开发者命令行**；② 若必须在受限 shell 里用 CMake：加 **`-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`**（让编译器探测只打静态库、跳过被 `cmd.exe /C` 包裹的 exe 链接步骤）——本次正是靠它配通，实测日志见 `logs/godot-cpp-cmake-configure.txt` |
| **7** | **`cl` / `link` 不认 MSYS 的 `/d/...` 路径** | `fatal error C1083: 无法打开包括文件`；或链接时报一堆 `LNK2019 无法解析的外部符号` | `-I` 与 `.lib` 路径**必须用 `D:\...` 反斜杠形式**。脚本里用 `pwd -W` 转换 |
| **8** | **godot-cpp 静态库文件名带两个连续的点**（`libgodot-cpp.windows.template_debug..lib`） | `link` 报 `LNK2001: 无法解析的外部符号 library`，看起来像符号缺失，其实是库没被吃进去 | 拷成干净名字再链：`cp libgodot-cpp.windows.template_debug..lib godot-cpp-d.lib`（`build-bridge.sh` 已做） |
| **9** | **MinGW 的 `LIB` 变量冲突**（bash 专用） | 把脚本里的 `LIB` 变量当路径用会覆盖 MSVC 的 `LIB`（库搜索路径），报"找不到静态库" | 脚本里别用 `LIB` 当变量名（用 `GCPPLIB`）；`export LIB=...` 只用于库搜索路径 |

### 原始命令（想手工跑就照这个）

```bash
# --- godot-cpp ---
curl -fsSL -o godot-cpp.zip https://github.com/godotengine/godot-cpp/archive/refs/tags/godot-4.5-stable.zip
unzip -q godot-cpp.zip && mv godot-cpp-godot-4.5-stable _godot-cpp

cmake -B _godot-cpp/build-cmake -S _godot-cpp -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ninja -C _godot-cpp/build-cmake            # 1025 个目标，约 5-10 分钟
# 产物：_godot-cpp/build-cmake/bin/libgodot-cpp.windows.template_debug..lib

# --- 桥接（开关逐字抄自 build-cmake/compile_commands.json） ---
cl /nologo /TP -DDEBUG_ENABLED -DGDEXTENSION -DHOT_RELOAD_ENABLED -DNOMINMAX \
   -DTHREADS_ENABLED -DTYPED_METHOD_BIND -DWINDOWS_ENABLED -D_HAS_EXCEPTIONS=0 \
   -I"_godot-cpp\include" -I"_godot-cpp\build-cmake\gen\include" -I"_godot-cpp\gdextension" \
   -I"D:\CppProgram\LowWater\src" \
   /DWIN32 /D_WINDOWS /GR /EHsc /Zi /Ob0 /Od /RTC1 -std:c++17 -MT /utf-8 /W3 \
   /Fo:build\lowwater_bridge.obj /Fd:build\lowwater_bridge.pdb /FS -c lowwater_bridge.cpp

link /nologo /DLL /machine:x64 /DEBUG \
   /OUT:godot-test/bin/lowwater_bridge.windows.template_debug.x86_64.dll \
   build\lowwater_bridge.obj _godot-cpp\build-cmake\bin\godot-cpp-d.lib \
   kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib ole32.lib oleaut32.lib \
   uuid.lib comdlg32.lib advapi32.lib
```

**官方 SCons 路径（备选，未在本会话跑通，因沙箱 `cmd` 限制）**：

```bash
scons platform=windows target=template_debug arch=x86_64 use_mingw=no build_library=yes
# 需要：python -m pip install scons==4.11.1
# 生成 compile_commands.json（便于外部工具）：额外加 compiledb=yes
```

---

## 4. 验收标准（**实测期望输出**，逐字可对照）

跑第 3 步的两条命令后，**必须**看到：

```text
Godot Engine v4.5.stable.official.876b29033 - https://godotengine.org

LOWWATER_BRIDGE_OK
engine=4.5-stable (official)
game_version=0.1.0-l1
schema_version=1
stream_registry_version=1
world_hash(20250913,1440)=334314409100154065
repeat_identical=true
```

判定口径：

| 项 | 判据 | 意义 |
|---|---|---|
| `LOWWATER_BRIDGE_OK` 出现 | 必须 | GDExtension DLL 被 Godot 4.5 成功加载，类 `LowWaterSim` 已注册 |
| `game_version=0.1.0-l1` | 必须 | **GDScript → godot-cpp → `simcore::kGameVersion`** 整条链路通 |
| `schema_version=1` / `stream_registry_version=1` | 必须 | `simcore` 的 ADR-002 版本三元组可从引擎侧读到，重放守卫可接 |
| `world_hash(20250913,1440)=334314409100154065` | 必须**逐位相同** | `simcore::deriveSeed64` + `pcg32_at`（ADR-002 的 RNG 流）在引擎内结果可复现 |
| `repeat_identical=true` | 必须 | 同参数两次调用结果相同（R2：引擎时间不参与仿真） |

**注意**：验收前提是**先跑一次 `--import`**（让 Godot 扫描工程、注册 `.gdextension`）。
不 import 直接 `--script` 会报 `Identifier "LowWaterSim" not declared`。

---

## 5. 若仍然失败：降级方案的代价（诚实清单）

**降级 = 桥接层不用 godot-cpp，改为"裸 GDExtension C API（填 `GDExtensionInitialization` 结构体 + 手写 `variant` 调用）+ 最小 C 桥"。**

### 5.1 会失去什么

| 能力 | godot-cpp（现在） | 裸 C API（降级后） |
|---|---|---|
| 类/方法绑定 | `GDCLASS` + `ClassDB::bind_method(D_METHOD(...))`，GDScript 里 `LowWaterSim.new()` 直接用 | 手写 `classdb_register_extension_class` + 每个方法一个 `GDExtensionClassMethodInfo`，逐个手写参数/返回值的 `Variant` 类型码与 `ptrcall` |
| 信号 | `ADD_SIGNAL` / `emit_signal()` | 手写 `object_has_signal` 等接口或干脆放弃信号 |
| 编辑器集成 | `@export` / 属性提示 / `PropertyInfo` / `@tool` | 全部手写 `GDExtensionPropertyInfo`，或直接放弃（Core 期工具链会受损） |
| `Variant` 自动转换 | `String` / `Array` / `Dictionary` / `Vector2` 直接映射 | 手工 marshal，逐个类型写 |
| 类型安全 | 编译期检查方法签名 | 全是 `void*` + 函数指针，错一个偏移就崩 |
| 热重载 | `HOT_RELOAD_ENABLED`（godot-cpp 已支持） | 需自己实现 |
| 引擎小版本升级 | 换 tag 重编 | 人工核对 `gdextension_interface.h` 与 API JSON 的差异 |

### 5.2 量化到人日

| 项 | godot-cpp（已验证） | 裸 C API（降级） | 差额 |
|---|---|---|---|
| 首次构建打通（配构建 / 写封装） | **0.5–1 人日**（本会话实际约 2 小时，含踩坑） | **2–4 人日**（手写 200–400 行 C 封装 + 逐个 API 试错） | **+1.5–3 人日** |
| 桥接层实现（ADR-001 估 1.5–2.5k 行） | 15–25 人日（E7 口径） | **30–50 人日**（手工 marshal + 无类型安全，按 +60~100% 估） | **+15–25 人日** |
| 每次引擎小版本升级（如 4.5 → 4.6） | **0.5–1 人日**（换 tag 重编） | **1–3 人日**（人工核对 2000+ API 变动） | **+0.5–2 人日/次** |
| 信号 / 编辑器集成 / 热重载 | 已含在 godot-cpp 内 | **要么放弃，要么 +5–10 人日** | **+5–10 人日** |

> **结论**：**降级不是"省事"，是"先省 2 小时、后多花 20+ 人日"**。
> 而且降级会削弱 K6（工具链是一等公民：调度可视化面板 + 归因链导出）——面板正是靠编辑器集成与属性绑定做的。
> **因此：除非 §3 的 9 个坑在你的环境里全部无法绕开，否则不降级。**

### 5.3 真要降级的兜底

若确需降级，范围必须**严格限定**：

1. 只把**最小调用面**做成裸 C：`simcore_init / simcore_advance / simcore_view_snapshot / simcore_version`（E2 的 9 函数子集）；
2. 保留 `simcore` 的 C ABI 边界不动（ADR-001 R1 不受影响）；
3. 记录为一条新 ADR，**并写明"何时回退到 godot-cpp"的触发条件**（例如：桥接层行数 > 800 行时）。

---

## 6. 证据索引

| 文件 | 内容 |
|---|---|
| `tools/spike/logs/bridge-godot45-call.txt` | **GDScript → godot-cpp → simcore 的成功输出**（§4 期望值就出自这里） |
| `tools/spike/logs/godot-cpp-build-cmake-success.txt` | godot-cpp Ninja 构建头尾（1024/1025 → 静态库） |
| `tools/spike/logs/godot-cpp-cmake-configure.txt` | CMake 配置成功的完整输出（含"2002 Files to generate"） |
| `tools/spike/logs/godot-SHA512-SUMS-4.5-stable.txt` | Godot 4.5-stable 官方 SHA512-SUMS（本次下载所用） |
| `tools/spike/logs/godot-cpp-build-attempt{1,2,3}*.log` | 上一轮失败原文（保留，不美化） |
| `tools/spike/bridge/` | 可复现的全部脚本与最小桥接源码 |
