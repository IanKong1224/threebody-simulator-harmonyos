# 构建、签名与安装

## 环境

- 主机测试：CMake 3.16+，支持 C++14 的编译器，Python 3.9+；macOS/Linux。部分文件记录测试使用 POSIX API，Windows 主机测试尚未适配。
- HarmonyOS 工程：目标/兼容 SDK 6.0.0(20)，Hvigor 模型 5.0.0。
- 本地候选版实际验证使用 macOS、DevEco Studio 26.0.0 所附 Hvigor 6.26.8 及 SDK。验证范围见 RELEASE_STATUS.md。
- XEngine 和 Graphics Accelerate Kit 使用官方 SDK 头文件，运行时加载设备系统库。仓库不分发厂商 SDK 或系统库。

默认开发工具路径为 macOS 的 `/Applications/DevEco-Studio.app/Contents`。可以指定：

```sh
python3 scripts/harmony.py build basic --deveco-home /path/to/DevEco/Contents
```

也可设置 `DEVECO_HOME`。自定义 SDK/Java 位置可以设置 `DEVECO_SDK_HOME`、`JAVA_HOME`。Windows 预留路径处理，但未实际验证；请先使用 DevEco 打开生成的工程构建，不把本地 macOS 验证视作 Windows 保证。

## 独立构建目录

运行 `python3 scripts/harmony.py prepare <版本>` 后：

| 版本 | DevEco 打开的目录 | 包名 |
|---|---|---|
| basic | build/harmony/basic | com.threebody.simulator.basic |
| particles | build/harmony/particles | com.threebody.simulator.particles |
| accelerated | build/harmony/accelerated | com.threebody.simulator.accelerated |

共享源码在仓库根目录。`build/harmony/` 是生成的工程，不要把长期代码改动只留在那里；再次 prepare/build 会刷新代码，但保留该版本在 DevEco 生成的根签名配置。不要同时在 DevEco 和脚本中构建同一个版本。

仓库只保存 `build-profile.example.json5` 无签名模板。真实签名留在生成目录或 DevEco 的用户目录，均不进入发布包。所有脚本构建共用锁；意外终止后，先确认无构建进程，再移除 `build/.harmony.lock`。

## 无签名构建

```sh
python3 scripts/harmony.py build basic
python3 scripts/harmony.py build particles
python3 scripts/harmony.py build accelerated --vendor-kits auto
```

脚本先安装本地 Node-API 类型声明依赖，再构建 Release。默认无签名，即使本机存在签名配置也不会自动发布签名包；临时使用模板后恢复原本配置。失败不复制历史 HAP。

加速 SDK 选项：

- `auto`：检测两套 SDK 头文件，分别启用可用接入；运行时继续检查设备能力。
- `on`：两套头文件缺一即构建报错，适合确认自己确实编入了接入代码。
- `off`：不编译厂商接入代码，保留明确的不可用状态和原生渲染。

构建成功不代表设备支持相应加速。显式 `off` 的产物使用 `threebody-accelerated-native-only-...hap` 文件名，与包含接入代码的包分开。

## 真机签名

1. 准备对应版本，打开它的生成目录，等待 DevEco 同步依赖。
2. 连接手机、允许调试，在项目签名配置中登录你自己的开发者账号并生成该包名、该设备的签名。
3. 在 DevEco 运行，或者关闭 IDE 构建后执行：

```sh
python3 scripts/harmony.py build particles --signed
```

每个版本独立签名。换手机后可能需要重新授权设备。此流程不读取另一个版本或旧开发者的私人证书。签名包也不一定能安装到其他人的设备。

常见错误：

- `9568320 / no signature file`：安装的是无签名包，或该构建未启用正确签名。
- `9568423 / device is unauthorized`：设备没有被当前签名 Profile 授权。
- `Ability does not exist`：先确认安装成功、包名匹配，再尝试启动。

## 安装

将官方 `hdc` 加入 PATH，先查看设备：

```sh
hdc list targets
hdc -t <你的设备ID> install -r dist/threebody-particles-release-signed.hap
```

只有安装成功后再启动：

```sh
hdc -t <你的设备ID> shell aa start -a EntryAbility -b com.threebody.simulator.particles
```

若存在多台设备必须明确指定。`-r` 覆盖同包名应用；不要为了排查签名错误直接卸载，以免丢失本地轨迹。发布反馈前隐藏设备 ID、用户名、签名路径和口令。

## 修改着色器

仓库含源 GLSL 与生成的 C++ shader 头文件，普通 HAP 构建无需 glslang。修改 GLSL 后使用官方 glslangValidator：

```sh
python3 scripts/compile-shaders.py
```

也可用 `GLSLANG` 指定工具路径。不要手改生成头文件。不同 glslang 版本的二进制可能不同；本地确认和 CI 会验证着色器编译，不能仅用二进制字节一致作为画面正确性证明。
