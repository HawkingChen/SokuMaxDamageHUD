# 实机诊断：萃香 5C 与 J6A 重叠伤害归属

本文件适用于测试分支 `agent/gpt6-suika-overlap-diagnostics-20261115` 及其修复分支。只读观测游戏运行状态，不改变战斗伤害或操作。**不自动发布新 Release**。

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
- `/framedata-data/manifest.json` 的 `framedata-dag-1` 解出 20 角色 1685 条逐 Hit 指纹，
  落到 `config/HitProfiles.tsv`：`character|action|card|kind|notation|neutralTotal|perHitBase|hitCount|prorationPermille`。
- 实测校准：萃香 5C 为 `150 × 18`、补正 `980`；网页版 comboRate 逐 Hit 乘 0.98
  （7 Hit 时 `0.8681257 = 0.98^7`）；`gutsFactor = floor(70 + hp*3/1000)/100`。
  用 `neutralTotal / simulatedFullCast` 逐招校准，使满段在 neutral 状态精确复现实测总伤
  （5C 2280 / j.6A 1200 / 6C 578 / j.5C 1107）。

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
