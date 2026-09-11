# LowWater · 枯水期

后核战废土题材开放世界 RPG。旧集权政府已崩溃，仅存零星自助式社区；玩家以拾荒为生，轻量 RPG 系统（属性/技能/分支对话）承载叙事，NPC 具备自主日程与需求模拟。

> **一句话定位**：这是一个"没有政府、只有邻居"的辐射式开放世界 RPG——你唯一的本领是捡垃圾，而这个世界里每一块垃圾原本都属于某个具体的人；他明天早上六点会准时去找它。

## 设计支柱

| 支柱 | 主张 | 否决什么 |
|---|---|---|
| **P1 没有救世主，只有邻居** | 任何抽象系统必须在一次短途步行内落到某个有名有姓的人 | 全局声望条、全揭示地图、阵营广播 |
| **P2 看得见的作息** | 模拟必须可读；读不懂的模拟等于没有模拟 | 黑箱效用 AI、随机游走、随机迟到 |
| **P3 世界不为你待机** | 缺位是信息，不是暂停 | 任务永久等你、NPC 悬停等接取 |
| **P4 每样东西都有代价** | 稀缺由具体的人承受 | 定时刷新垃圾堆、无限背包 |

## 目录结构

```
design/gdd/
  game-concept.md              概念文档（支柱 / MDA / 范围分层 / Core 七条验收）
  systems/                     逐系统 GDD（八节结构）
    00-foundation.md           S0 底座：角色判定 / 世界时钟 / 确定性
    01-scarcity-loop.md        S1 稀缺主循环（拾荒 / 负重 / 生理 / 伤病）
    02-npc-simulation.md       S2 NPC 三层模拟（需求 / 调度 / 行为 / 记忆）
    03-economy-barter.md       S3 易货经济与社区仪表
    04-dialogue-contract.md    S4 对话与契约账本
    05-community-creed.md      S5 社区、信条与声望
    06-rumor-network.md        S6 传闻传播网络
    07-work-board.md           S7 缺口工作板
  06-consistency-review.md     跨 GDD 一致性评审 + 全部裁决记录
docs/architecture/
  architecture.md              主架构文档（分层 / tick / LOD / 预算 / 工具链）
  accessibility.md             可访问性分级与特性矩阵
  control-manifest.md          Core 七条验收 → 可执行验证手段
  adr/ADR-000 ~ ADR-005        架构决策记录
src/simcore/                   确定性仿真地基（header-only）
tests/                         测试
tools/                         辅助脚本
```

## 构建与运行

本仓库当前以 **MinGW GCC 6.3.0** 作为可编译下限验证（本机无 cmake / make / MSVC）。

```bash
# 编译并运行烟雾测试
g++ -std=c++14 -O2 -Wall -Wextra -I src tests/smoke.cpp -o build/smoke.exe
./build/smoke.exe
```

**标准纪律**：正式目标为 C++20；为保证可移植性与当前工具链可验证，`src/simcore/` 一律按 **C++14 兼容子集**编写——禁止使用 C++17/20 特性（`string_view` / `variant` / `optional` / 结构化绑定 / `if constexpr` / concepts 等）。下限越低，换用 MSVC 或新版 MinGW-w64 时越不会出问题。

## 不可违背的确定性不变式

这些是架构评审（ADR-000）判定 PASS 的依据，任何改动都必须重跑验证：

1. **时间口径**：`TIME_SCALE = 60` **游戏秒/现实秒**，`SIM_TICK_COST = 60`（1 SIM_TICK = 1 游戏分钟），1 游戏日 = 1440 现实秒 = **24 现实分钟**。
2. **快进 ≡ 逐 tick**：快进必须走同一 tick 序列、同一 RNG 流，两种模式的 `worldHash` 序列**逐位相同**。
3. **视锥无关性**：`tickLevel`（FULL/COARSE/PROTECTED）只由距离与 pending 事件决定；**相机朝向不得参与仿真**，视锥只驱动表现层 `renderLOD`、不进存档。
4. **无裸随机**：所有随机经 `rng(seedCtx)`，`streamId` 静态可枚举；禁止在渲染帧上发起判定。
5. **回写纪律**：当日只入写队列 → 日切/整点批量应用 → 下游读昨日快照。
6. **无通用货币**：五簇通货（净水/燃料/弹药/药品/可育种子）地域比价；食物为第 6 键但 `tradable = false`。

## 当前进度

- [x] Phase 1 概念孵化
- [x] Phase 2 系统设计（8 份 GDD + 一致性评审）
- [x] Phase 3 技术搭建（架构 + 5 条 ADR + 可访问性 + 控制清单）
- [ ] Phase 3 · simcore L1 骨架（进行中：头文件已就位，测试套件与 CLI 待补）
- [ ] Phase 4 预制作 / 垂直切片

## 已知阻塞

- **引擎选型未拍板**（ADR-001）：推荐 `simcore`（C++20 静态库 + C ABI，引擎无关）+ Godot 4 表现层。待用户决断；不阻塞 simcore L1。
- **S8 区域内容 / S9 呈现交互两份 GDD 未写**：`ContainerDef`、路程常量表、UI 布局缺位，垂直切片前必须补齐。
