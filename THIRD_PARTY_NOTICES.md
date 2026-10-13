# 依赖、素材与来源清单

本项目代码与文档由发布者授权采用 Apache-2.0，署名见 NOTICE。本文件记录本次分发的内容边界；外部 SDK、开发工具及系统库仍适用各自条款。

| 内容 | 来源/使用方式 | 是否随源码分发 | 状态 |
|---|---|---|---|
| HarmonyOS / ArkUI / Node-API / NativeWindow / VSync | 开发者本地官方 SDK、设备系统库 | 仅自己的调用代码，不包含 SDK | 遵守对应 SDK 条款 |
| Vulkan / OpenGL ES | Khronos API，经官方平台 SDK | 仅本项目后端和 GLSL | 不附带头文件或驱动 |
| XEngine、Graphics Accelerate Kit | 官方 SDK 头文件、运行时动态加载 | 自己的适配器；不含厂商二进制 | 需目标 SDK/设备支持 |
| glslangValidator | 开发工具，生成 SPIR-V 头文件 | 不分发工具；含本项目生成的 shader 数据 | 工具由开发者/CI 安装 |
| Python / CMake / C++ 标准库 | 构建和测试工具 | 不分发工具 | 无 Python pip 运行依赖 |
| 简单三体 SVG 图标 | 本地工程已有几何图标，两处相同 | 是 | 随本项目源码一并采用 Apache-2.0 |
| 八字轨道数值与算法背景 | 公共数学模型；见 docs/PHYSICS.md | 初值和实现，不含论文文本/图像 | 引用不等于复制第三方实现 |
| 其余代码与着色器 | 本项目开发快照 | 是 | 随本项目源码采用 Apache-2.0；无另行导入的第三方源码包 |

华为的 [XEngine Vulkan 示例](https://gitee.com/harmonyos_codelabs/xengine-code-vulkan-demo-cpp) 仓库采用 Apache-2.0。该示例可作为 API 学习来源，但不能据此推断 XEngine SDK 或所有示例依赖可随意再分发。本仓库没有导入该示例的 Sponza 模型、Assimp 库或 SDK 二进制。

本次分发已移除旧模板 PNG，启动图标使用上述简单 SVG。源码清单检查未发现厂商库、外部模型、照片或贴图；测试中的 SDK 替身只用于接口测试，不是实际加速实现。后续引入或改编第三方内容时，必须补充对应来源、原始许可证、版权声明、修改说明和必要 NOTICE。
