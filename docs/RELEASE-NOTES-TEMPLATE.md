# GitHub Release 说明模板（面向玩家）

每次发布时，优先写**简体中文**，不要直接把 CI、源码改动列表或开发术语堆给玩家。

推荐内容：

# SokuMaxDamageHUD vX.Y.Z｜什么最痛

一句话介绍本 MOD：给《东方非想天则》增加一个用来查看伤害修正、最痛必杀技、最大实际伤害和手牌伤害的中文 HUD。

## 这次更新

用 3～6 条说明**玩家能看见或用到的变化**，说明具体操作/效果；有风险的地方简短写已知限制。不把 CI 成功误写成实机全验证。

## 安装／更新

1. **退出游戏**，下载下方 `SokuMaxDamageHUD-vX.Y.Z-win32.zip`，不要下载 GitHub 自动的 Source code。
2. 将 ZIP 中的 **`SokuMaxDamageHUD` 文件夹**解压到游戏 **`modules`** 目录；升级前可备份旧文件夹，保留个人配置及本地生成的 `icons`。
3. 在 SokuLauncher／SokuModLoader 中启用本 MOD，再启动游戏。SWRSToys 用户需确认 DLL 位于 `modules/SokuMaxDamageHUD/SokuMaxDamageHUD.dll` 并在 `SWRSToys.ini` 正确加载。

适用《东方非想天则》v1.10a。可说明支持的 HUD 语言及工作模式（WindowResizer 无边框全屏、窗口模式；不支持独占全屏）。

最后给 README、Issues 的链接，并写明安装包不包含版权游戏卡图，卡面在本机提取。

## 发布机制

- 在 `docs/releases/vX.Y.Z.md` 保存完整中文用户说明。
- 合并新版本的 `[publish-release]` 标记提交时，CI 构建/测试/打包成功后创建对应 Release，并优先读取上述文档。
- 如需**只修正已发布的 Release 文案**，不改版本或 DLL，可单独使用 `[update-release-notes]` 标记的 main 提交，CI 成功后执行 `gh release edit`；不能无故重发一个新版本。
