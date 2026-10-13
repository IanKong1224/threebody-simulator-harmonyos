# 首发范围与验证记录

整理日期：2026-10-13。发布者确认以个人名义开源，署名 @差评硬件部，许可证 Apache-2.0，GitHub 账号 IanKong1224。此记录不代表重新完成了真机测试；原装机工程保留。

## 已完成

- 单仓库三版本；每版生成独立 DevEco 工程和签名配置。
- 源码按白名单导出；排除私人签名、历史 HAP、备份、设备日志、本机缓存及旧 Benchmark。
- 无签名构建默认安全；显式 `--signed` 才使用该版本本地配置。
- 加速 SDK 支持 AUTO/ON/OFF；不分发厂商头文件、系统库或开发工具。
- 修复 VSync/渲染失败后物理线程覆盖错误信息的竞态，并增加关闭期间的回归覆盖。
- 补充 README、构建/模型/帧率说明、开发路线、第三方清单、“焚决”、反馈模板。
- 主机 CMake 测试明确启用 Release 断言，增加 DP5(4) 轨迹测试。
- 帧节奏回归检查准确的 60/120 Hz 间隔策略、实际提交间隔下限和物理结果；不要求共享 CI 主机达到指定 FPS。生成帧测试等待完整计数窗口。
- 提供敏感内容检查、确定性源码 ZIP 生成，以及 GitHub Actions 配置。

## 本地验证

| 项目 | 结果/边界 |
|---|---|
| macOS C++ 主机测试 | 17/17 通过，包含 RK4/RK45、预设有限时长、轨迹、帧率与 VSync |
| Python 构建/发布回归 | 7/7 通过，覆盖签名隔离、失败恢复、锁、排除敏感材料和授权门禁 |
| 基础版 / Pro Release | 全新生成目录无签名构建成功 |
| Pro Max Release，厂商 Kit ON | 两套 SDK 接入编译通过，无签名 HAP 构建成功 |
| Pro Max Release，厂商 Kit OFF | 回退代码编译通过，无签名 HAP 构建成功 |
| GLSL | Vulkan shader 编译、GLES shader 校验与头文件生成通过 |
| 发布内容检查 | 无匹配的签名材料、个人绝对路径、已知 token 模式或非预期二进制；不是全能泄漏检测 |
| GitHub CI / Linux / macOS | [源码检查通过](https://github.com/IanKong1224/threebody-simulator-harmonyos/actions/runs/38385682679)：两平台各 17 项 C++ 与 7 项 Python 测试，另有 Linux 着色器校验 |
| Windows | 尚未验证 |
| 真机 | 本候选没有重新签名安装；画面、厂商运行时能力和 120 Hz 仍需真机复核 |

独立源码 ZIP 解压复验结果在本文件末尾记录。

## 授权与分发

- 版权署名：`Copyright 2026 @差评硬件部`。
- 个人发布账号：`IanKong1224`。
- 许可证：Apache-2.0，完整文本见根目录 LICENSE，署名见 NOTICE。
- 首发内容为源码、教程和测试；不包含设备签名、SDK、驱动或面向任意设备的通用 HAP。
- 分发检查结果与授权信息记录在 `release-readiness.json`；正式源码归档使用 `python3 scripts/pack-source.py --public`。

公开仓库：[IanKong1224/threebody-simulator-harmonyos](https://github.com/IanKong1224/threebody-simulator-harmonyos)。先检查源码，观察 CI 后标记版本。后续变更继续使用同样的检查，不将本地日志和私人构建目录加入 Git。

## 源码压缩包独立复验

将白名单源码 ZIP 解压到全新目录后，重新生成三个 DevEco 工程：

- 基础版、Pro、Pro Max（强制厂商 Kit ON）均完成 Release 无签名 HAP 构建。
- Pro Max 的 XEngine 与帧生成头文件检查均通过，并实际编译对应接入文件。
- 在解压目录重新编译运行 17 项 C++ 测试和 7 项 Python 回归，全部通过。
- 本机仍需已安装的 DevEco / SDK / CMake 工具；验证证明不依赖原工程的源码、签名、构建缓存或固定目录层级。

这些本地复验没有连接手机。之后修正了依赖主机实际速度的计时测试，17 项本地 C++ 测试和 Pro Max（厂商 Kit ON）构建再次通过；远程 Linux/macOS 测试均通过。各次 CI 的执行结果以仓库 Actions 页面对应提交为准。
