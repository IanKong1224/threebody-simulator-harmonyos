# 三体模拟器 · HarmonyOS

[![Source checks](https://github.com/IanKong1224/threebody-simulator-harmonyos/actions/workflows/ci.yml/badge.svg)](https://github.com/IanKong1224/threebody-simulator-harmonyos/actions/workflows/ci.yml)

三颗恒星的运动由手机实时计算，轨迹来自历史计算结果。支持全屏观察、预设与自定义初始状态，并逐步增加粒子、恒星表面、光晕和可选渲染加速。

**署名：@差评硬件部 · 个人开源发布：IanKong1224 · 许可证：[Apache-2.0](LICENSE)**

首发范围与验证记录见 [发布说明](docs/RELEASE_STATUS.md)。

## 三个版本，一份源码

| 版本 | 构建参数 | 物理积分 | 画面 | 输出目标 |
|---|---|---|---|---|
| 三体模拟器 | `basic` | 双精度 RK4，近距离限制时间步 | 点状恒星、线条轨迹 | 60 FPS |
| 三体模拟器Pro | `particles` | 同上 | 球体恒星、圆形粒子轨迹 | 请求 120 Hz，按 VSync 送显 |
| 三体模拟器Pro Max | `accelerated` | 双精度自适应 Dormand–Prince 5(4) | 表面细节、HDR 光晕、亮度调节；可选超分和超帧 | 请求 120 Hz；超帧开启时真实帧目标 60、送显目标 120 |

目标不是保证值。设备支持、温度、画面负载和系统调度会影响结果；帧率含义见 [帧率与性能](docs/PERFORMANCE.md)。三个应用使用不同包名，可以共存。

共有功能：

- 拖动旋转、双指缩放、复位视角，底部横条上拉展开控制面板。
- 开始、暂停、继续、重新开始；复位视角不改变物理状态。
- 八字、随机立体、长时立体、稳定立体、三日交错及自定义参数。
- 质量、初始位置和速度编辑；轨迹显示、长度和模拟速度调节。
- 帧率显示；约每 0.5 秒记录一次帧率；轨迹 JSON/CSV 和帧率 CSV 导出。

本项目不提供跑分，也不承诺碰撞和逃逸永不发生。点质量从一开始就有质量；球体表面是视觉表现，不是恒星内部流体模型。光晕使用 HDR/Bloom，**不是实时光线追踪**。参见 [物理模型与预设](docs/PHYSICS.md)。

## 从源码运行

准备 Python 3.9+、DevEco Studio 和匹配的 HarmonyOS SDK。当前工程目标为 HarmonyOS 6.0.0 / API 20；加速能力还取决于 SDK 与设备。已验证环境和限制见 [构建说明](docs/BUILD.md)。

在仓库目录执行：

```sh
python3 scripts/harmony.py prepare basic
```

用 DevEco **打开生成的工程目录** `build/harmony/basic`，等待同步，连接设备，为自己的设备配置签名，再运行。不要把 HAP 当源码工程打开。

另外两个版本分别用：

```sh
python3 scripts/harmony.py prepare particles
python3 scripts/harmony.py prepare accelerated
```

无签名构建：

```sh
python3 scripts/harmony.py build basic
python3 scripts/harmony.py build particles
python3 scripts/harmony.py build accelerated
```

构建产物在 `dist/`。无签名 HAP 不能直接安装。签名后的构建与安装见 [构建说明](docs/BUILD.md)。不同版本的签名互不复用；不要提交任何证书、密码、设备 ID 或签名包。

缺少厂商加速 SDK 时，Pro Max 自动使用不可用提示和原生渲染回退，也可显式关闭接入：

```sh
python3 scripts/harmony.py build accelerated --vendor-kits off
```

## 只验证物理核心，无需手机 SDK

在 macOS 或 Linux 安装 CMake 3.16+、C++14 编译器：

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure --parallel 4
python3 -m unittest discover -s tests -p 'test_*.py'
```

Release 测试仍启用断言。主机测试不能替代真机显示、签名和厂商 SDK 验证。

## 跟着开发过程学习

- [焚决：给 Codex 的开发提示词](prompts/焚决.md)：从零开发的阶段要求，不保证一次对话生成完整产品。
- [开发路线](docs/DEVELOPMENT.md)：每个阶段解决什么问题、怎样验证。
- [架构](ARCHITECTURE.md)：物理、渲染、界面与厂商接入的边界。
- [第三方与来源](THIRD_PARTY_NOTICES.md)：依赖、科学参考及许可边界。
- [贡献指南](CONTRIBUTING.md)、[问题反馈](.github/ISSUE_TEMPLATE/bug_report.md)。

首个公开版本是当前源码快照；没有伪造过去开发阶段的 Git 提交历史。
