# SokuMaxDamageHUD · 什么最痛

**简体中文** · [English](README.en.md) · [日本語](README.ja.md)

《东方非想天则》**v1.10a** 的伤害参考面板 MOD。在游戏窗口下方显示招式估算、伤害修正和本局记录，支持中文、日文、英文。

**战斗数据只读。** 本 MOD 只读取游戏运行时的战斗状态，用于估算和显示；不改变实际伤害、角色属性或战斗状态，不修改网络封包，不干预随机数（RNG），也不向游戏发送操作。自动提图只读取原始资源文件，生成的图片和界面设置保存在 MOD 自己的目录中。

![完整界面：游戏画面与下方 HUD](docs/hud-meiling.png)

*完整界面*

![HUD 细节：伤害修正、最痛记录与当前手牌](docs/hud-youmu.png)

*HUD 细节*

## 安装

需要已能加载 MOD 的非想天则 v1.10a（SokuLauncher / SokuModLoader / SWRSToys）。

1. 退出游戏，从 [Releases](https://github.com/HawkingChen/SokuMaxDamageHUD/releases/latest) 下载 `SokuMaxDamageHUD-v版本号-win32.zip`，不要下载 Source code。
2. 将 ZIP 中的 **`SokuMaxDamageHUD` 文件夹**解压到游戏的 **`modules`** 目录。
3. 在 **SokuLauncher 的模组页面启用 SokuMaxDamageHUD**，然后启动游戏。

安装后的文件位置应为 `游戏目录/modules/SokuMaxDamageHUD/SokuMaxDamageHUD.dll`。

<details>
<summary>直接使用 SWRSToys</summary>

在实际使用的 `SWRSToys.ini` 的 `[Module]` 段加入：

```ini
SokuMaxDamageHUD=modules/SokuMaxDamageHUD/SokuMaxDamageHUD.dll
```

只启用一次，避免用不同条目重复加载同一个 DLL。

</details>

**卡图不需要另行下载。** 首次启动会自动从本机游戏的 `th123a.dat` / `th123b.dat` / `th123c.dat` 提取卡牌与天气图片到 MOD 的 `icons` 目录；不修改游戏资源、不联网，也不覆盖已有图片。无需 Python 或额外脚本。若没有图标，检查游戏资源是否完整、MOD 目录是否可写，以及该目录下的 `IconExtraction.log`；提取失败时仍会显示文字占位图。

更新前建议备份原文件夹，退出游戏后覆盖新版文件。卸载时在启动器中禁用，再删除 `SokuMaxDamageHUD` 文件夹即可。

## 使用

- **伤害影响 · 总修正**：大号百分比显示共通攻防修正；灰色数值显示根性补正，必杀技和符卡 的专属倍率单列。下方卡堆展示影响因素及叠加数量。
- **最痛必杀技**：当前已知可用必杀技中估算伤害最高的一招。B / C 版等伤害时可合并显示为 `623B/C`。
- **最痛记录**：本局已记录到的单招最高伤害，可能包含普通技和符卡。
- **当前手牌**：按手牌顺序展示符卡 的估算伤害。
- 按钮组中的 **CN / JP / EN** 按钮循环切换语言；两个方向按钮切换到另外两种停靠位置；粗箭头按钮收起。游戏画面右下角的半透明 **＋** 可展开。设置会自动保存。

**窗口布局（0.5.2 起）**：通过面板方向按钮或拖动选择下、左、右，不提供快捷键。竖版与游戏窗口等高，横版紧贴下沿。首次展开可自动调整游戏客户区，保持 4:3；用户主动切换到下方时也允许重新腾出空间，包括 WindowResizer 无边框全屏。其他切换、收起、隐藏和退出对战不会改变游戏尺寸。

窗口模式下，移动游戏导致下方放不下 HUD 时，会转到左右空间较多的一侧；侧栏撞到桌面边缘时，仅在另一侧明显更宽时换边，两侧都拥挤则保持，避免循环。拖动时半透明静态替身跟随鼠标，靠近边缘才吸附，松开后固定在最近的下/左/右位置。伤害记录继续采集。

支持普通窗口与 **WindowResizer 无边框全屏**，独占全屏不支持。`SokuMaxDamageHUD.ini` 中 `[HUD] AutoFit=0` 禁止首次自动调整，但主动切到下方仍允许调整；`SideWidth` 设置竖版宽度。HUD 仅在支持的对战场景显示，失焦或退出对战时隐藏。

## 已知限制

- `≈` 表示参考估算，并非精确模拟。招式等级、命中段数、位置及部分特殊状态可能影响结果；首击强化不会直接乘到整招估算上。
- 实际记录按弹幕来源和同次释放累计；同时发生多个来源或特殊机制时仍可能无法识别招式，部分资料尚不完整。
- 联网主客机、观战、回放以及不同 MOD 组合尚未完成全面实机验证。该 MOD 不修改游戏伤害或网络封包。
- 本次新增的自动提图需要使用包含该功能的新构建；旧版发布包不会自动获得此功能。

## 反馈与资料

遇到问题请在 [Issues](https://github.com/HawkingChen/SokuMaxDamageHUD/issues) 提供 MOD 版本、游戏模式、角色、招式及截图或复现步骤。

伤害参考数据来自 [Cleista Framedata](https://soku.cleista.cc/framedata) 等公开资料；日语技能及符卡名已与原版 v1.10a 卡名表核对。卡面由玩家本地游戏生成，不随仓库和 ZIP 分发；游戏资源权利属于原权利人。

[开发与构建](docs/BUILD.md) · [MIT 代码许可](LICENSE)
