# ADR-005 · 仿真数值表示（milli 定点数）

- **状态**：**已提出，待确认后生效**（若被否决，`simcore` 无法进入实现）
- **日期**：Phase 3 · PHASE3-001
- **相关**：S0 §C7-3（禁 `double` 与超越函数）、S0 §D 常量表、S1、S3 价格递推、ADR-002
- **修订**：PHASE3-L1-Q1（2025-09-11）——D1 取整口径改为【对称取整（round half away from zero）】；V3/D4 明确 CI 只统计未定义 `SIMCORE_ENABLE_REFERENCE_FLOAT` 的正式 TU，并剔除 `[CI-EXCLUDE-BEGIN]..[CI-EXCLUDE-END]` 区块
- **起因**：S0 §C7-3 只给了禁令（"禁止 `double` 与超越函数"），没给**替代表示**。不先定这个，每个系统会各自选，逐位复现立刻崩。

---

## 1. 背景（Context）

S0 §C7-3：`仿真状态累加禁止 double 与超越函数（sin/exp）；确需三角函数走查表 + 定点量化。`

但设计文档里存在大量小数常量：

```text
WATER_L_PER_PERSON_DAY = 3.0        FOOD_UNIT_PER_PERSON_DAY = 1.0
SLEEP_RECOVER_PER_HOUR = +12.5      BARTER_OFFSET = (PRESENCE − 5) × 4%
SPEED_MUL = clamp(1.15 − 0.5 × (cur/cap)², 0.45, 1.15)
BASE_WALK_SPEED = 1.4               REPUTATION_DECAY = ×0.97 / 日
safeDays = min(waterL/(3.0×pop), foodUnits/(1.0×pop))
Quality : float 0.4..1.5            staffingRatio / completeFactor / effHead
```

若用 `float`，同一份 C++ 代码在同一台机器上**通常**逐位复现，但风险来自：编译器优化（`-ffast-math`、自动向量化改变归约顺序）、x87 与 SSE 混用、中间结果扩展精度。**逐位复现需要的是"保证"，不是"通常"。**

若用 `double`，S0 明令禁止。

---

## 2. 备选方案（Options）

| 方案 | 描述 | 精度核算 | 判定 |
|---|---|---|---|
| O1 · IEEE `float` + 严格编译选项 | 最省事 | 依赖编译器与平台 | ❌ 是"通常可行"而非"保证可行"；且 S0 §C7-3 精神是去浮点 |
| O2 · Q16.16 二进制定点 | 经典定点 | 精度 1/65536 ≈ 1.5e-5，但**十进制小数不精确**：对 S0 §D + S1/S3 的 **33 个设计量**做核算，**24 个无法精确表示**（0.1 / 3.0 / 0.97 / 12.5 中的多个） | ❌ 不精确表示会引入"设计值 ≠ 实现值"的系统性偏差 |
| O3 · Q31.32 / Q63.64 | 高精度 | 精度足够但**十进制小数同样不精确**，且 64 位乘法溢出与移位规则复杂 | ❌ 复杂度不划算 |
| **O4 · milli 十进制定点（1/1000）** | `int32/int64` 存「真实值 × 1000」 | 对 **33 个设计量逐个核算：全部可精确表示，0 个失败** | ✅ **采纳** |
| O5 · micro（1/1e6） | 更高精度 | 也全部可精确表示，但 32 位范围只剩 ±2147，价格/库存易溢出 | ⚠️ 仅用于局部（见 D3 例外） |

### 精度核算方法（可复算）

```python
DESIGN_VALUES = [3.0, 1.0, 64, 96, -8, -4, -5, 12.5, 10,14,18,22, 5, -8, 6, 1.0,
                 5,15,30,50,75, 25, 5, 60, 10, 4, 0.6, 0.04, 1.15, 0.5, 0.45, 1.4,
                 0.97, 0.60, 2, 120, 150, 360, 3, 7, 0.4, 1.5]     # S0 §D + S1 附录 A
def exact(v, scale):  return abs(v*scale - round(v*scale)) < 1e-9
# milli (scale=1000) : 失败数 = 0
# Q16.16(scale=65536): 失败数 = 24
```
**另做 10 日价格递推对照**：milli 定点 vs `double`，`max |Δprice| < 1e-3`（远小于显示精度 0.01），且 milli 版本**逐位可复现**。

---

## 3. 决策（Decision）

### D1 · 统一采用 **milli 定点**：`using Milli = int32_t;`（语义 = 真实值 × 1000）

```cpp
using Milli  = int32_t;    // 主类型，范围 ±2.147e6（真值 ±2147）
using MilliL = int64_t;    // 中间运算类型，防溢出；运算后立即夹回 int32
```

**乘法/除法规则（唯一实现，禁止各自写）**：

```cpp
// 取整口径：round half away from zero（对称取整）。C++ 整数除法向零截断，
// 故负分支对 |积| 做 +500 再截断，即得 half-away。
inline Milli  mulM(Milli a, Milli b);   // round_half_away(a*b / 1000)
inline Milli  divM(Milli a, Milli b);   // round_half_away(a*1000 / b)；b != 0 断言
inline Milli  addSat(Milli a, Milli b); // 饱和加减，溢出即断言（debug）/ 夹到边界（release + 计数上报）
```

- **加减**：直接整数加减（同量纲），溢出用饱和 + 断言。
- **乘 / 除**：先升 `int64`，归位（乘除 1000）后用**对称取整**（round half away from zero）——
  必须在同一处实现，避免各家取整方向不同。恒有 `mulM(-a,b) == -mulM(a,b)`、
  `divM(-a,b) == -divM(a,b)`、`divM(a,-b) == -divM(a,b)`。
- **比较**：整数比较，无容差。

#### D1-a · 取整口径（对称取整）与修正原因（PHASE3-L1-Q1 · 2025-09-11）

| 口径 | 表达式（乘积 p） | `+2.5` | `-2.5` | 评价 |
|---|---|---|---|---|
| 旧（向 +∞ 偏置） | `(p + 500) / 1000` | `+3` | `-2` | ❌ 负侧单向偏置 → 系统性漂移 |
| **新（对称取整）** | `sign(p) · round(|p| / 1000)` | `+3` | `-3` | ✅ 借贷对称，无净漏损 |

**修正原因**：原口径对**负积**是「向 +∞ 方向偏置」（`-2.5 → -2`）。资源模拟里经济系统存在
**借贷对称性**（S3 的 `reservedByPromise`、S4 的 DELIVER/违约 delta 都有正负两向），单向偏置是
**系统性漂移源**，会让长期账目持续漏损且难以归因，故裁决改为对称取整。`divM` 原式
`(a*1000 + b/2)/b` 在**除数有符号 / 异号**时同源偏置（`divM(-100000,3000)` 旧值 `-33332`，应为 `-33333`），
一并修正。

**验证要求**：对全部测试向量恒有 `op(-x) == -op(x)`；`±2.5 / ±1.5 / ±0.5` 三个舍入边界逐位验证。
golden 向量见 `tests/golden_fixed_vectors.h`（生成器 `tools/gen_golden_fixed.py`，可复现重算）。

#### D1-b · `divIntM`（纯整数截断除）语义声明（PHASE2-CLOSING 补记）

> `divIntM` 的语义**明确为"向零截断"**（C++ 整数除法本身即对称，`a/b == -(-a/b)`），**不与 `mulM/divM` 的"对称舍入"合并**——改它反而引入舍入错误。故 `divIntM` **禁止在精度敏感路径使用**（如价格递推、账目结算、概率/权重换算等依赖对称性、对舍入方向敏感的计算），仅可用于不累积舍入偏差的整数计数场景。

### D2 · 各量的定点刻度（唯一映射表）

| 量 | 刻度 | 类型 | 说明 |
|---|---|---|---|
| `Quantity.amount`（6 cluster） | ×1000 | `Milli` | `WATER/FUEL→L`、`AMMO→rd`、`MEDS→dose`、`SEED/FOOD→portion` |
| 价格（`PriceTable` / `quote` / 比价） | ×1000 | `Milli` | 显示时 /1000 保留 2 位 |
| 声望 `Reputation`（−100..+100） | ×100 | `int32`（专用 `Rep100`） | **特例**：设计只需 2 位小数精度，用 ×100 省范围；`REPUTATION_DECAY ×0.97` 用 `×97/100` 整数实现 |
| 生理点（水/食/睡，0..192/96） | 整数 | `int16` | 本就是整数（64 点/L × 3 L = 192） |
| `safeDays` | ×1000 | `Milli` | `min(waterL/(3.0×pop), foodUnits/(1.0×pop))` 全用 `Milli` 运算 |
| 比率类（`staffingRatio` / `completeFactor` / `SPEED_MUL` / `BARTER_OFFSET` / `Quality`） | ×1000 | `Milli` | 0..2000 区间 |
| 时间（`dayKey` / `minuteOfDay` / `absTick`） | 整数 | `int32` / `int64` | 不参与定点 |
| 位置（世界坐标） | ×1000（mm） | `int32` per axis | `worldHash` 量化到 **0.1m** = 100mm（S0 §C3 已定） |
| 角度 | 1/65536 圈 | `uint16` | `turn` 定点；三角函数走 **65536 项查表 + 定点线性插值** |
| 概率 / 权重 | ×1000 | `Milli` | 与 `rollCheck` 的 `modTotal` 一致 |

### D3 · 局部例外（需显式标注，不得默认）

- `micro`（×1e6）**只允许**用于"极小且需高相对精度"的派生量；Core 期**暂不使用**，需要时走 ADR 增补。
- 三角函数：`sinTurn(uint16) -> Milli`，查表 65536 项（`int16` 存 milli → 64KB 表），线性插值用 `Milli` 运算。**禁止** `std::sin`。
- 开方：S1 §A-3 之类若需 `sqrt`，用**整数牛顿迭代**（定点），禁止 `std::sqrt`。`SPEED_MUL` 里的 `(cur/cap)²` 是平方不是开方，直接 `mulM`。

### D4 · 编译期与运行期护栏

```cpp
static_assert(sizeof(Milli) == 4);
// debug 构建：所有 mulM/divM 溢出 → assert 中断
// release   ：饱和 + 计数上报（每日汇总进 debug 面板）
// CI 静态检查（V3 口径见下）：只统计【未定义 SIMCORE_ENABLE_REFERENCE_FLOAT 的正式 TU】
//   （src/simcore/*.h,*.cpp），扫描前剔除 [CI-EXCLUDE-BEGIN]..[CI-EXCLUDE-END] 区块与注释；
//   命中 'double|float|std::sin|std::cos|std::exp|std::pow|std::log|std::sqrt' 数必须为 0
//   执行：./build.sh check
```
> **`float` 也禁**（不只 `double`）：原因是仿真侧不该有任何 IEEE 浮点；渲染侧照常用 `float`，但**不跨界**（ADR-001 R1）。

### D5 · 与 ADR-002 的衔接

- `rollCheck` 的 `modTotal / dc / margin` 全部是 `int`（已是整数）。
- 若某判定需要小数修正（如 `BARTER_OFFSET` 的 4%），先算成 `Milli`，再在**唯一一处**量化为 `int`（`roundM(x)`），量化规则写进 `simcore/check/mod_quantize.h`，全项目共用一套。

---

## 4. 后果（Consequences）

**正面**
- 逐位复现从"依赖编译器行为"变成"依赖整数运算"，**可保证**。
- 设计常量 1:1 落地，无"实现偷偷改了 0.97"的风险。
- 溢出可断言、可统计，比浮点 NaN 传播好定位得多。

**负面 / 成本**
- 所有公式要写 `mulM/divM`，可读性下降 → **缓解**：提供 `Milli` 的运算符重载（仅 `simcore` 内部，debug 版带溢出断言）。
- 需要一次"常量迁移"：把 S0 §D 的数值全部以 `Milli` 字面量落到 `simcore/constants.h`，**并加静态断言**：`static_assert(MILLI(3.0) == 3000)` 之类，防止手改错。
- 精度上限 1e-3：对本项目所有量级（L、portion、点、比率）都绰绰有余；10 日价格递推与 double 偏差 <1e-3（< 显示精度 0.01 的 1/10）。

**中性**
- `Reputation` 用 ×100 而非 ×1000 是一个刻度混用点，需在代码注释与 `constants.h` 里显式标注，避免误当 `Milli`。

---

## 5. 验证

| 验证 | 手段 | 判据 |
|---|---|---|
| **V1 · 常量精确性** | 单测：S0 §D + S1 附录 A 全部常量 `round(v*scale) == v*scale` | 失败数 = 0 |
| **V2 · 递推一致性** | 10 日价格递推：milli vs `double` 参考实现 | `max abs Δ < 1e-3`；milli 版两次运行逐位相同 |
| **V3 · 无浮点** | CI 静态检查（D4；只统计正式 TU，剔除 CI-EXCLUDE 区块与注释） | 命中数 = 0 |
| **V4 · 溢出审计** | 跑 30 日，统计 `mulM/divM` 饱和计数 | 计数 = 0 |
| **V5 · 查表精度** | `sinTurn` 与 `std::sin` 在 65536 采样点比对 | `max abs Δ < 2e-3`（含插值误差），且两次运行逐位相同 |

> **V3 口径（PHASE3-L1-Q1）**：`fixed.h` 的浮点**参考镜像**（`#ifdef SIMCORE_ENABLE_REFERENCE_FLOAT`）
> 默认关闭，且**必须集中**在带 `// [CI-EXCLUDE-BEGIN]` / `// [CI-EXCLUDE-END]` 标记的单一区块内。
> CI 只统计**未定义该宏的正式 TU**（该宏仅在 `tests/test_runner.cpp` 内定义），并剔除该区块与注释。

---

## 6. 受影响文档

| 文档 | 建议 |
|---|---|
| `design/gdd/systems/00-foundation.md §C7-3` | 建议补一句：「仿真侧统一用 milli 定点（1/1000），见 ADR-005」——**不改动任何常量数值** |
| `design/gdd/systems/00-foundation.md §D` | 常量数值**全部不动**；实现侧在 `simcore/constants.h` 里以 `Milli` 字面量落地并静态断言 |
| `docs/architecture/control-manifest.md` | 已写入 V1–V5 门禁 |
| `src/simcore/README.md` | §4.3/§4.4 记录取整口径与 CI 无浮点口径；§3 增用例 10 |
| `tests/golden_fixed_vectors.h` + `tools/gen_golden_fixed.py` | golden 向量与新口径生成器（PHASE3-L1-Q1） |

---

## 7. 待主理人确认（Ask）

1. **Q1**：采用 milli（1/1000）定点，确认吗？
2. **Q2**：`Reputation` 用 ×100（而非 ×1000）这个例外，确认吗？
3. **Q3**：`simcore` 内部是否允许 `Milli` 的运算符重载（`+ - * /` 走 `mulM/divM`）？——可读性 vs 显式性，倾向允许（debug 版带断言）。

---

## 8. 知识缺口

- 10 日价格递推的偏差 <1e-3 是我在**递推公式的简化模型**上核算的（价格更新公式取自 S3 §3.1 的 `effStock/demand → P(D+1)` 结构）。S3 完整公式（含 `PriceShock` 阈值与议价 roll 的耦合）实装后需**重跑一次该核算**，若偏差放大到接近显示精度 0.01，需把价格提升到 micro（×1e6）或改用 ×10000。
