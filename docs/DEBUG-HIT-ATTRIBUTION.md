# 实机诊断：萃香 5C 与 J6A 重叠伤害归属

本文件适用于测试分支 `agent/gpt6-suika-overlap-diagnostics-20261115` 及其修复分支。只读观测游戏运行状态，不改变战斗伤害或操作。**不自动发布新 Release**。

## 延迟符卡 4000 伤害没进「最痛记录」（15:09 实机反馈）

用户：用魔理沙放了一个**延迟型 4 卡 SC**，炸了非常多血，但没有被计入最痛记录。

### 证据（DamageTrace.log 最后一帧，336 行）

```
frame=13506 char=1 action=5>5 seq=0>0 hp=10000>6000 dealt=4000
combo=4:1125>1:4000 source=-1:0 mixed=1 hits=0 possible=0
live=22 unresolved=22 snapshot=0/0 list=22/22 owned=22/22
crate=0.5 profiles=1677 infer=614:4000 conf=90
explain=[single sc214 x2] best=500:1350
```

`config/HitProfiles.tsv` 里 `1|614|214|spell|sc214|4000|4000|2|980` —— 魔理沙的 4 卡 SC 静态值就是 4000。
`DamageInference` **算对了**（残差 0，0.90 = 完美拟合扣掉 0.10「无在场证据」惩罚），
`observeInferred()` 也把 `614/214/4000/0.90` 存进了 `inferred_`。

### 根因：显示优先级，不是推断失败

`composeHUD()` 原来写死 `if(best.attributed&&best.damage>0) ... else if(inferredUsable)`：
只要**本局曾经**打中过任意一招（这里是 `500:1350`），推定卡就永远排在后面。
延迟 SC 施法者早已回到 `action=5` 站立、22 发弹幕全是 orphan，`HitAttribution`
**结构上不可能**给出 owner，于是 `best_` 永远停在 1350，4000 永远上不了卡。

### 修法：把优先级抽成可测的纯函数

`RecordTracker.hpp` 新增 `RecordSource` / `pickRecordSource(best, inferred, comboPeak, anyDamage)`，
`ModMain.cpp` 只按返回值渲染**已验证 / 推定 / 连段 / 空白**四种卡。推定卡允许覆盖显示，
但必须同时满足三道闸：

1. `confidence >= 0.60`（高于 0.35 的「可显示」下限）；延迟 SC 的 0.90 通过；
2. `inferred.damage > best.damage`（严格大于）；5C/J6A 的 `2280 vs 2280` 仍是实测卡，不回退；
3. `inferred.damage <= comboPeak`（游戏自己的连段峰值）；任何超过全部观测伤害的数字都是拟合伪影。

**不变量不变**：推定值只写 `inferred_`，`best_` 永远是实测单招。因此
`inferenceCanBeatRecord()` 的记录上界剪枝、后续真实命中的排行，仍只用真实数据。
`observeInferred()` 另加保护：更大但置信低 0.15 以上的猜测不能顶掉已存的可靠推定。
推定卡照旧带「≈」与「（推定）/ (EST.)」，不会冒充实测。

回归测试 `testDelayedSpellEstimateCanTakeBestHitCard()` 逐条覆盖上面 12 个分支
（含实机 1350/4000、5C 平局、弱置信、超峰值、空白局、推定不被低置信覆盖）。

## 长招式名把出招指令截掉（06:08 截图反馈）

用户截图：左侧「最痛必杀技」显示 `Meteonic Debris · 236[...`，看不到完整指令。
根因是 `StyledPanel.hpp::write()` 对**整串**用 `DT_END_ELLIPSIS`，Windows 一律砍尾部。

修法：`HudText.hpp` 新增 `CardTitleLayout` / `layoutCardTitle()`，以最后一个 ` · `
为界拆成 `head`（招式名）与 `tail`（` · COMMAND` 及蓄力标签）；
`StyledPanel.hpp::writeCardTitle()` 用 `GetTextExtentPoint32W()` 先量出并锁定 tail 宽度，
只对 head 用 `DT_END_ELLIPSIS`，tail **绝不截断**；整串放得下时保持原渲染不变，
连 tail 都放不下的极端情况才退化为对 tail 本身省略。两处卡片标题（理论卡 + 最痛记录）都改用它。

## 蓄力招式缺席「最痛必杀技」候选（13:18 实机反馈）

用户：萃香把元鬼玉蓄满打出 **2200 最痛伤害**，但「最痛必杀技」卡片显示 **623 地灵-密-**（1483），
明显更低；要求「蓄力后伤害更高的也应该计算进去，并且表明是蓄力版」。Cleista 页面也写了
`j.236[C]` 蓄力元鬼玉逐 Hit **2000**、合计 **2200**。

### 根因：两张表用途不同

- `config/HitProfiles.tsv`（`DamageInference` 用）**早就有** `236[B]` / `j.236[C]` = 2200。
- `config/FrameData.txt`（`g_staticMoves` → 「最痛必杀技」/ 资料卡用）**只有 tap 版**：
  `9|skill|104|880|元鬼玉|236B|...` 等 4 行，一条蓄力版都没有。
  候选池最大只有 `623C` 地灵-疏- 2035 / `623B` 地灵-密- 1483，所以 2200 永远排不上。
- **游戏没有蓄力标志**。DamageTrace.log 同一局：
  ```
  frame=648  action=505>505 seq=2>2 dealt=880   ← tap
  frame=956  action=505>505 seq=2>2 dealt=2200 ← 蓄满
  ```
  两者 action / sequence 完全相同，只有伤害不同；因此实机 BEST HIT 也只能靠伤害区分。

### 修法

1. **数据**：`.research/cleista/gen_charged_framedata.py` 从 20 角色 `moves.json` 取所有带 `[` 的
   蓄力版，按 **Cleista 自己的 tap-vs-charged 对比**，只保留 `damage > tap` 的（用户要求），
   共 217 行插到各自 tap 行之后，`FrameData.txt` 1373 → 1590 行。
   萃香 4 条 `2200` 蓄力元鬼玉入选；`236[B]` card100（1311 < tap 1549）等更弱的蓄力版**不入选**。
   修后萃香最强 skill 候选 = `2200 236[B/C] / j.236[B/C]`，超过 `623C` 2035。
2. **蓄力标记**：`HudText.hpp` 新增 `isChargedNotation()` / `chargedSuffix()`（中「（蓄力）」、
   日「（溜め）」、英「 (CHARGE)」），`panelCardLabel()` 对带 `[` 的卡片追加标签。
   `mergeSkillButtons()` 支持 `236[B] + 236[C] -> 236[B/C]`，不再只显示 B 版。
3. **实机卡片**：`actionName()` 增加 `observedDamage`，用 `pickNotationByDamage()` 在
   tap(880) / charged(2200) 之间按实测伤害选 notation；BEST HIT 与推定卡都传实测值，
   无实测值时仍返回 tap 版（保持旧行为）。`lookupNames()` 让蓄力版复用 tap 版的三语名，
   英文界面不再回落成中文名。
4. **部署**：`FrameData.txt` 必须随 DLL 一起更新——`deploy_run.py` 现在同步复制
   `HitProfiles.tsv` / `FrameData.txt` / `MoveNames.tsv`，否则新候选不会生效。
5. 本地复刻 16/16；新增 `testChargedVariantLabels()` 覆盖蓄力识别、标签、合并、按伤害选择。

## 数据源纠错：Cleista `summary.damage` 是「一次测量」，不是招式上限

用户实机用萃香 **j.5C 打出 2280**，而 `soku.cleista.cc/framedata/suika` 写 j.5C = 1107。
查证后确认：**数据表没错，是我们用错了字段**。

### 证据

- `suika/5c.json`、`suika/j5c.json`、`suika/6c.json` 的 `objectProperties["801"]`
  与 `objectDescriptors` **字节级完全一致**（hash `d9098a6a…` / `bd99041c…`）：
  同一发弹幕对象，`perHit = {damage:150, proration:980, limit:2, spiritDamage:25}`。
  `provenance` 写明 `layoutSource: shady-packer`、`vanillaVerified: true`，
  「confirmed against vanilla th123a.dat / th123c.dat `{character}.pat`」——**逐 Hit 数据来自原版 `.pat`，准确**。
- `moves.json` 里 5C / 6C / 2C / j.5C / j.6C 的 `density` **全部是 `C1x18`**（18 发弹幕），
  但 `summary.damageHits` 是 `18 / 4 / 4 / 8 / 3`，`summary.damage` 是
  `2280 / 578 / 578 / 1107 / 439`，且 `damageFrom: "measurement"`。
  即：**测量时只打中了一部分弹幕**。6C 只中 4 发 = 578，j.5C 只中 8 发 = 1107。
- 用移植公式 `150 × Σ 0.98^i` 验算：8 发 ≈ 1119（实测 1107）、18 发 ≈ 2286（实测 2280）。
  用户实机的 2280 正是 **18 发全中**，完全自洽。
- 网页版 JS **不硬编码伤害数值**：`Oh(t)` 返回 `data/character/<char>/<char>.pat`，
  数值由运行时解析 `.pat`；JS 里实现的是原版移植的 `0x463f50`（Rate / limit / guts /
  proration）与 `0x47aa50`（hit apply）。页面文案 `prov.asset.pat` 明确写
  「damage, limit, proration … sourced from parsed `{character}.pat`」。
  所以**公式与逐 Hit 数据可信**；`prov.asset.hp` 也自承
  「Not yet checked against a real match」——用户实机 2280 恰好是一次真实对局校验。
  `.pat` 属游戏原始资源，站点不托管下载，需玩家自己的游戏文件。

### 误用造成的两个实际故障

1. `HitProfiles.tsv` 曾把 `hitCount` 设成「实测命中数」，`solve()` 只遍历 `1..hitCount`，
   j.5C 最多只能模拟 8 发 → **根本解释不了 2280**。
2. `maxNeutralDamage(9,414)=1107` 使 `castCeiling = 1660`（1.5×），
   会把用户真实的 2280 j.5C 判成「不可能」并撤回记录 → 正是 `192b267` 部署后被回滚的原因。

### 修法

- 生成脚本用 `density` 的 `C1xN` 作为 `hitCount`（弹幕总数），并以**与 C++ 完全同构的
  `simulate()`（含 guts、逐 Hit 截断）** 把测量值外推到满段作为 `neutralTotal`。
  因为 `calibration = neutralTotal / simulate(full)`，这样 `expectedDamage(measuredHits)`
  精确复现 Cleista 测量值，同时满段也可达：j.5C 8 发 = 1107、18 发 = 2217（实机 2280 残差 63，
  容差内、置信 0.97）；6C 4 发仍精确 578、5C 18 发仍精确 2280、3057 重叠仍反解为 5C+j.6A。
- 修表后 `maxNeutralDamage(9,414)=2217`，`castCeiling=3325`：
  **放行真实的 2280，仍能拒绝符卡的 3500**。
- 顺带修 `RecordTracker` 撤回逻辑：拒绝某个 instance 时不再粗暴清空 `best_`，
  而是把该 instance 累计的全部伤害转入 `unattributedDamage()` 并从存活 casts 重建 best，
  避免「拒绝误归因符卡 → 连之前合法的 500 记录一起变空白」。
- `config/FrameData.txt` 仍保留 Cleista 原始测量值（1107）：它只喂 UI 静态招式资料卡，
  **不参与 BEST HIT 记录或上限**，不要和 `HitProfiles.tsv` 混为一谈。

## 12:20 实机复验：符卡 3500 被误记为 j.5C（新 bug）

推断版 DLL（`7c08df2b…`）实机日志 719 行 / 1 次启动 / 81 个伤害帧，`profiles=1677` 确认指纹表已加载。用户报告「用 4 卡发动，居然算在 J5C 造成 3500 伤害」。逐帧证据：

```
f19484  act=607>607 dealt=2000 combo=3:1892>1:2000 src=414:28 mixed=0 hits=1 poss=0 live=31 unres=9  best=410:2280
f19639  act=5>5     dealt=1500 combo=1:2000>1:1500 src=414:28 mixed=0 hits=1 poss=1 live=44 unres=42 best=414:3500
f30503  act=414>414 dealt=150  combo=18:2280>1:150 src=414:80 mixed=0  ← 真正打 j.5C，150/hit
```

- **不是推断器猜错**：`best=` 是 `RecordTracker` 的已验证单招记录。归因器把命中的唯一有身份弹幕（`414:28`）当成整帧来源并给 `mixed=0`，即使场上还有 9 / 42 个无主弹幕 —— `hits.size()==1 && !actorHit` 分支只检查 `possible`，不检查 orphan。
- **记录层放大**：`RecordTracker::observeDamage()` 按 `instance` 累计 `casts_[instance]`，既无伤害上限也无时间窗，只有新回合才 `clear()`。相隔 155 帧的 `2000 + 1500 = 3500` 因此被算成同一次 j.5C 施法并写进 `best_`。
当时误以为 Cleista 的 **1107** 是 j.5C 满段上限（见上节纠错：那只是 8 发测量样本，
真实满段 18 发约 2280），但 3500 仍超出任何合理上限，所以拒绝本身是对的。
- **结构性修复（数据驱动，不动已验证通过的 5C/J6A 归因逻辑）**：
  - `DamageInference::maxNeutralDamage(character, action)` 返回该招**满弹幕流**伤害（现为 2217）；调用方按 1.5× 生成 `castCeiling`（覆盖天气 / 攻防 / 反击倍率）。
  - `RecordTracker::observeDamage` 增加 `damageCeiling`：累计越界即把该 instance 永久标记 `rejected`，该 instance 累计的全部伤害转入 `unattributedDamage()`；若它已写过 `best_`，用 `bestInstance_` 撤回并**从存活 casts 重建**，不清空整张卡。
  - `ModMain.cpp` 把「该 instance 即将越界」识别为 `suspectCast`，与 `mixed` 同等对待，触发 combo 反解（evidence 含角色当时的 607），不再信任错误身份。
  - 回归测试 `testRecordCeilingRejectsImpossibleCast()` 用 2000/1500/2217/2280 回放，并新增「真实 2280 j.5C 不得被拒」断言；本地复刻 52/52 断言通过。
- **诚实边界**：日志没有 born 级事件，尚不能断定符卡弹幕为什么继承 `414:28`（`born()` 的 300–399 继承或 `parentObject` 链皆有可能），所以这次**没有改归因器**，避免破坏 5C/J6A 已通过的修复。修复后不会再出现「j.5C 3500」这种错误招式；由于 BEST HIT 仍是 verified 优先，符卡 3500 会保持为观测到的连段伤害而非升为最痛单招。若要让符卡正确进榜，需加 per-object origin 诊断后定位 born 路径。

## 从「通用兜底」到「按伤害公式反解招式」（11:10 修正）

> 10:30 版曾写下「**无法根治**：每一笔伤害都标出 100% 正确的招式名」，并据此只做
> `COMBO DAMAGE` 通用标签兜底。**这个结论是错的**。用户已提供网页版非想天则
> `https://soku.cleista.cc/practice` 与 Cleista framedata：伤害计算管线与逐 Hit 数据都可读，
> 就应该算出来，而不是贴一个通用标签。以下为修正后的做法。

**可以做到**：把游戏自己的连段累计伤害（combo 计数器）当硬约束，按网页版移植的伤害公式
逐 Hit 模拟候选招式，反解出最可能的施法组合及各自贡献；用 `comboHits` 做第二重约束；
残差过大时明确放弃，绝不硬凑招式名。
**仍然做不到**：在不 Hook 游戏 Hit 事件的前提下把每次伤害都 100% 证明为某一招。因此推定
结果必须带「（推定）/ (EST.)」标记，且永远不能覆盖已验证的单招记录。

### 数据来源与公式校准

- `/practice` 的 JS bundle 显示伤害计算来自原版移植的 `0x463f50`（Rate / limit / guts /
  proration scaler）与 `0x47aa50`（hit apply）。`PLAYER+0x4B0` 是 `comboRate`，
  `+0x4B4` `comboCount`、`+0x4B6` `comboDamage`、`+0x4B8` `comboLimit`。
- `/framedata-data/manifest.json` 的 `framedata-dag-1` 解出 20 角色 1677 条逐 Hit 指纹，
  落到 `config/HitProfiles.tsv`：`character|action|card|kind|notation|neutralTotal|perHitBase|hitCount|prorationPermille`。
  `hitCount` 取 `density` 的弹幕总数而非实测命中数（见上节纠错）。
- 实测校准：萃香 5C 为 `150 × 18`、补正 `980`；网页版 comboRate 逐 Hit 乘 0.98
  （7 Hit 时 `0.8681257 = 0.98^7`）；`gutsFactor = floor(70 + hp*3/1000)/100`。
  用 `neutralTotal / simulatedFullCast` 逐招校准，使各招的**测量命中数**在 neutral 状态
  精确复现实测总伤（5C 2280 / j.6A 1200 / 6C 578 / j.5C 1107），同时允许模拟到满弹幕流。

### 反解策略（用户给出的剪枝思路）

1. **记录上界剪枝（调用侧）**：单招贡献不可能超过本段连段的总伤害；若
   `comboDamage <= best().damage`，本段连段无法刷新最痛记录，直接跳过 `solve()`，
   不进入任何模拟。连段累计值随命中增长，超过记录后节流逻辑会自动补算。
2. **候选上界剪枝**：理论满段伤害 + 容差 < 观测 combo 总伤的候选，直接排除。
3. **体术优先**：单发 body hit 解释最简单，打分加成；本 combo 真实活跃过的施法优先。
4. **弹幕分情形**：逐 Hit 模拟 1/2/3/4/5/6/8/9/12/15/16/18/满段命中数；两段施法时把
   第一段结束的 `comboRate` 与 HP 传给第二段。
5. **combo 硬约束**：同时匹配累计伤害与 `comboHits`（±1）；残差超过
   `max(24, 4%×target, 6%×target)`，或「有活跃证据但最优解释不用任何证据且残差 >
   基础容差」，视为无解并返回 invalid，HUD 才退回通用兜底。

### 真实日志数值回放（`.research/cleista/verify2.py` 本地复刻，与 C++ 同逻辑）

| 观测 combo | hits | 反解 | 主招贡献 |
|---|---|---|---|
| 2280 | 18 | 单招 5C ×18 | 5C 2280 |
| 3057 | 19 | 5C ×18 + j.6A ×1 | 5C 2280（j.6A 775） |
| 1200 | 1 | 单招 j.6A | j.6A 1200 |
| 578 | 4 | 单招 6C ×4 | 6C 578 |
| 1244 | 9 | 5C ×9 | 5C 1264 |
| 1928 | 15 | 5C ×15 | 5C 1966 |
| 2763 / 2832 | 16 / 17 | 5C ×15 + j.6A ×1 | 5C 1966 |
| 5000 | 25 | **无合理解释**（残差 1443） | 不显示招式 |

3057 被拆成 `2280 + 775` 而不是膨胀成单招；5000 这种算不出来的值明确放弃，不再硬凑
`623C + 5C`。`tests/test_core.cpp` 的 `testDamageInferenceSuikaComboReplay()` 用内联的
78 行萃香指纹固定以上行为。

### 代码落点

- `src/core/DamageInference.{hpp,cpp}`：`read / simulate / expectedDamage / solve`。
- `HitAttributionResult::candidates`：mixed / unresolved 帧也带出「本帧真实活跃过的施法」
  作为 evidence，而不是全表瞎猜。
- `HitSnapshots.hpp` 读 `PLAYER+0x4B0` 的 `comboRate`，诊断日志写作 `crate=`。
- `ModMain.cpp`：combo 起始记录 HP 与 evidence；对 `mixed || 无来源` 的伤害帧调用
  `solve(comboAfter.damage)`；诊断日志新增 `profiles=`（指纹表加载条数，**0 表示表没随包
  部署**）、`infer=`、`conf=`、`explain=`。
- `RecordTracker::observeInferred()` 只存推定记录，**不写 best_**；`composeHUD()` 四级：
  已验证单招 → 推定招式（带 EST 标记，只显示该招贡献）→ `COMBO DAMAGE` 权威峰值 →
  真没打中才显示「尚未记录命中」。
- 打包链：CMake `install()` 与 `build.yml` 的 `$required` 都必须包含 `HitProfiles.tsv`，
  否则实机读不到表会静默退回兜底，表现为「改了却没变化」。

## 第 4 次启动：用户 repro 被完整复现，前一版修复不够（10:08）

用户反馈「5C 和 J6A 重叠 3057 伤害，还是显示尚未命中」。上一版只把「未识别招式」改成空白，**没有让重叠命中产生真实招式记录**，等于没修。新日志（第 3 个 `# New run`，行 935 起）复现：

```
L982  frame=101 combo=0:0>9:1244    dealt=1244 source=-1:0 mixed=1 unresolved=32 best=-1:0
L983  frame=102 combo=9:1244>15:1928 dealt=684  source=-1:0 mixed=1 best=-1:0
L984  frame=103 combo=15:1928>16:2763 dealt=835 source=-1:0 mixed=1 best=-1:0
L1000 frame=119 combo=16:2763>17:2832 dealt=69  source=-1:0 mixed=1 best=-1:0
L1076 frame=424 combo=17:2832>18:2280 dealt=2280 source=410:3 mixed=0 best=410:2280  ← 补一次干净 5C 才有记录
L1246 frame=655 combo=0:0>18:2280 dealt=2280 source=-1:0 mixed=1 best=-1:0
L1253 frame=662 combo=18:2280>19:3057 dealt=777 source=-1:0 mixed=1 best=-1:0
```

**真正根因在归因层，不是显示层**：L960/L970（actor 处于 410）生成的 5C 弹幕带 `source=410:1 unresolved=0`；从 L975 起 actor 切到 307，但 **5C 弹幕流继续生成约 25 帧**。`HitAttribution::born()` 里 `if(casting.action<400)return {};` 把这批「同一招的后半段」全部判成无主弹幕，orphan 从 8 涨到 32。归因器于是把**同一个 5C 施法**误当成「两个独立来源」→ `mixed=1` → 整帧丢弃 → BEST HIT 空白。

**第二次结构性修复**：
- `HitAttribution` 记住最近一次射击/特殊施法（`canonical>=400`）的 origin，窗口 60 帧（实测最后一次 410 帧到延迟命中恰好 15 帧）。普通技（300–399）期间新出现的无父弹幕继承它，不再变 orphan。
- `hits.size()==1 && !actorHit` 分支新增保护：若 `possible` 里有与 hit 不同的 origin（如 j6A 的活跃攻击框），仍判 `mixed`，防止 777 被算进 5C 变回 3057 膨胀。
- `RecordTracker` 的「BEST HIT 只排名已验证单招」不变量保留不变。

预期结果：重叠作为首击时 `frame=101/655` 归到 `410`，BEST HIT 显示 `5C 2280`；`frame=662` 的 777 保持 mixed 不排名。新增 `testSuika5CStreamAfterJ6AKeepsItsCast()` 与 `testOverlapFirstSessionNeverBlank()` 固定该行为。

## 实机日志结论（3 次启动，525 行，138 个伤害帧，已验证）

`snapshot=0/0` 全程通过，弹幕列表 getter 正常，此前「读取失败」的推测不成立。角色 `char=9`（萃香）397 行、`char=2` 132 行。萃香两局真正造成伤害的 10 帧：

| 情形 | 帧（第 1 局 / 第 2 局） | source | mixed | dealt |
|---|---|---|---|---|
| 单独 5C | 282 / 125 | `410:1` | 0 | 2280 / 2280 |
| 单独 J6A | 420 / 271 | `307:2` | 0 | 1200 / 1200 |
| 5C+J6A 重叠 | 732, 735 / 517, 518, 520, 543 | `-1:0` | 1 | 2280, 777 / 1956, 206, 795, 65 |

重叠帧 `unresolved=20~32`，是 `HitAttribution::born()` 有意拒绝把「J6A 期间生成的 5C 延迟弹幕」认成 J6A 所致 —— 这条防误归属规则正确，保留。第三局（另一角色）另有 128 个伤害帧，同样呈现「干净单招正常归属 + 大量 `mixed=1` 帧被正确排除」，`best` 依次为 `406:285 → 321:417 → 322:659 → 309:710 → 304:832`。

**必须如实说明的边界**：全部 525 行里，`source=-1:0 && mixed=0 && dealt>0` 为 0 帧，`best=-1:<正数>` 也为 0 帧；`best=-1:0` 只出现在前两局各自第一次干净 5C 之前（231–281、74–124）。也就是说，**当前部署的诊断 DLL 在这三次会话中并没有产生「未识别招式」的条件**：每局都是先打中一次可归属的单招，之后 `best()` 就一直返回该真实记录。日志无法直接复现用户看到的那一下。

**残留缺陷（由代码确定，不依赖日志复现）**：`RecordTracker` 仍有两条通路能把无法归属的伤害写进最痛记录 —— ① 非混合但无来源的帧直接赋 `best_={damage,-1,false,false}`；② `best()` 在 `best_` 为空时返回 `mixedFallback_`。`ModMain.cpp` 把这类记录渲染成「未识别招式」。因此**一开局第一下就是 5C+J6A 重叠**（尚无干净单招记录）时必然复现；先打一次干净 5C 后 `best=410:2280`，看起来又「好了」—— 这正好解释了反复横跳，也解释了为什么日志里抓不到。

**结构性修复（不依赖启发式）**：确立不变量「BEST HIT 只排名已验证的单次施法」。混合 / 无来源伤害只累加到 `unattributedDamage()`，永不进入 `best()`；删除 `mixedFallback_` 与 HUD 的「未识别招式」分支，无记录时显示既有的「尚未记录命中 / NO HIT RECORDED」。`tests/test_core.cpp` 新增 `testSuikaDamageTraceReplay()`，按上面 10 个真实帧逐帧回放并断言 `best()` 始终为 `410:2280` 且 `attributed=true`，另加「重叠为首次命中」的回归用例。

## 旧版问题与推测

- 用户报告萃香延迟 5C 单独全部命中约 2280；在其命中时使用 J6A，HUD 将混合伤害 3057 错标为 J6A 的单招最高，v0.5.7 实机仍可复现。
- 之前只检测“同帧多个明确碰撞”，但若旧 5C 弹幕在爆炸前暂时没有启用 hitBox，或者它在角色开始 J6A 后才创建/被枚举，单个 J6A 碰撞仍可能取得整帧总伤害。这个推测**没有经过游戏实机证实**。
- 本测试分支按“来源置信度不足则不排名”的原则：没有亲缘关系的弹幕不能继承当前 A 技动作；旧弹幕即使本帧 hitBox 不活跃也作为潜在的第二来源；未识别的在场弹幕阻止单个 J6A 冒领整帧伤害。
- 虽能减少假最高纪录，也可能使部分本可确认的单招伤害暂时不进入排行，须真实游戏验收。后续可能需要读取单个 Hit 事件/更底层伤害归属，而不仅是 HP 前后差。

## 测试步骤（Steam Deck / Windows 练习模式）

1. 退出游戏，备份原 `modules/SokuMaxDamageHUD`，从此测试分支最新**成功**的 GitHub Actions 运行页下载 Artifacts 中的 Win32 ZIP（不要用 v0.5.7 Release）。
2. 解压并安装 `SokuMaxDamageHUD`，保留自己的卡图目录。编辑 `modules/SokuMaxDamageHUD/SokuMaxDamageHUD.ini` 中 `[Damage]` 段，将 `EnableDamageTrace=1`。本功能**默认关闭**，只写本地 `DamageTrace.log`，无需网络。
3. 新开练习局，选萃香，分别试：单独 5C 全中；单独 J6A；5C 延迟命中同时 J6A；5C 命中完后再 J6A。最好各自独立新开一局。
4. 重点观察：单独 5C 的实际纪录可累加；单独 J6A 正常识别；二者真正同时命中时，不能把混合总伤害当作 J6A 或未知单招的最高；单独攻击不应全部丢失。
5. 若依然错误，请提供 `DamageTrace.log` 中这组操作对应的行（可先从文件中移除其他局数据）。每行包含玩家动作 ID、帧前后 HP/连击计数、弹幕列表快照状态、已识别来源、冲突标志、当前最高纪录，**不包含账号信息，也不会上传到服务器**。日志有本地进程对象指针编号，不愿共享时可先沟通。
6. 测完把 `EnableDamageTrace=0` 关闭，之后可删日志。只有用户确认修正实际有效后，才允许合并到 main / 发布新 Release。

## 状态字段

- `snapshot=0/0` 表示前后均成功读取对象列表；`1` 没有对象列表，`2` 版本/Hook 导致 `getList` 虚表不符合预期，`3` 对象列表损坏或数量异常。
- `list=a/b` 是游戏列表项目数；`owned=a/b` 是检出的属于本机角色的游戏对象数。
- `source=307:x` 表示 j6A 来源；`410:x` 表示 5C 来源；`mixed=1` 说明不确定是否由单招独占本帧伤害。
- `live` 和 `unresolved` 表示追踪到的候选弹幕数及无法确认来源的数量。

本体游戏 / 原版 1.10a DLL 的命中事件回调和单 Hit 因子，后续建议交给能实机运行非想天则的本地 Codex 分析。**CI 通过不等于实机验证完成**。
