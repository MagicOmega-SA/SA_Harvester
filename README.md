# SA_Harvester

《心灵终结》3.3.6 的独立采矿插件。刚满载且尚未返程时，向有空位的矿场补发卸矿命令。矿车停滞时尝试恢复采集、分流，或移开占住矿口的空车。

## 安装

从 `dist` 选择一个 DLL，和 `SA_Harvester.ini` 一起放到 `gamemd.exe` 同级目录，通过原客户端开局。更换版本时移走原来的采矿插件 DLL。

- `SA_Harvester_NoLog.dll` 适用于日常游玩，关闭日志输出。
- `SA_Harvester.dll` 可通过 `Log=1` 记录采矿指令，日志写入 `SA_Harvester.log`。

## 配置

`AutoNearestOre=1` 时，矿车倒空并离开矿口后，优先采集离出站位置最近、可以下达采矿命令的矿格。按格心直线距离比较，金矿和彩矿同等处理。

`EnableMultiplayer=1` 时，三个功能也可用于局域网和在线对局，只操作本方矿车，只搜索已揭开的矿格。观战时自动停用。

`AutoFull`、`AutoClear` 和 `AutoNearestOre` 可分别关闭，修改后重启游戏生效。`StallFrames` 按游戏逻辑帧计，默认 150。

## 构建

使用 32 位 MinGW-w64 运行 `python build.py`，两个 DLL 输出到 `dist`。可将工具链目录加入 `PATH`，或用 `MOE_MINGW` 指定其 `bin` 目录。

## 许可证

本项目的源码和构建脚本采用 [MIT 许可证](LICENSE)。
