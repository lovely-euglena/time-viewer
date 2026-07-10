


# TimeDisplay

一款功能丰富的 Windows 桌面时间显示应用程序，支持 NTP 网络时间同步、全屏模式、自定义字体等多种功能。

## 功能特点

- **时间显示**：实时显示当前时间和日期，支持网络时间与本地时间切换
- **NTP 时间同步**：支持通过网络时间协议自动同步系统时间，内置多个 NTP 服务器
- **全屏模式**：支持全屏显示，适合用于时钟展示或大屏显示
- **窗口置顶**：支持窗口置顶模式，始终显示在最前
- **透明度调节**：支持命令行设置窗口透明度（0~255）
- **嵌入字体**：程序内嵌思源黑体（裁剪版），无需依赖系统字体，跨设备观感统一
- **自定义字体**：支持通过 `--font-name` 指定系统字体，或通过 `--font-path` 加载外部字体文件
- **触摸支持**：支持触摸操作，可用于触摸屏设备
- **DPI 感知**：支持高 DPI 显示（Per-Monitor V2），确保在不同分辨率下清晰显示
- **动态字体**：根据窗口大小自动调整字体大小，拖动窗口时字体实时跟随变化
- **D2D 加速渲染**：使用 Direct2D/DirectWrite GPU 硬件加速渲染，性能优于传统 GDI
- **自动降级**：GPU 不可用时自动回退到 D2D 软件渲染或 GDI 软渲染，兼容 Windows 7~11
- **命令行选项**：支持丰富的命令行启动参数

## 系统要求

- Windows 7 或更高版本（Direct2D 需要 KB971644 平台更新）
- Visual Studio 2019 / 2022 / 2026（推荐，使用 v142/v143/v145 工具集）
- Windows SDK 10.0+

## 构建说明

### 方法一：Visual Studio（推荐）

1. 使用 Visual Studio 打开 `TimeDisplay.sln` 文件
2. 在解决方案管理器中选择目标配置（Debug/Release）和平台（x86/x64）
3. 按 Ctrl+Shift+B 或选择 **生成 → 生成解决方案**

> 注：项目已适配 VS 2026 工具集（v145），打开 .sln 后如提示工具集升级，确认即可。

### 方法二：命令行生成

```cmd
msbuild TimeDisplay.sln /p:Configuration=Release /p:Platform=x64
```

## 使用说明

### 基本操作

| 操作 | 说明 |
|------|------|
| 全屏按钮 / F11 | 切换全屏/窗口模式 |
| 退出按钮 / Esc | 退出全屏模式或关闭程序 |
| 同步按钮 / F5 | 手动同步 NTP 时间 |
| 置顶按钮 / F6 | 切换窗口置顶状态 |
| Ctrl+Shift+Q | 退出程序 |
| 双击标题栏 | 切换全屏模式（也支持触摸双击） |

### 命令行选项

| 选项 | 说明 |
|------|------|
| `-b`, `--no-button` | 隐藏所有按钮 |
| `-f`, `--full-screen` | 启动时自动进入全屏模式 |
| `-k`, `--nok-key` | 禁用全局热键 |
| `-p`, `--topmost` | 启动时自动置顶窗口 |
| `-t`, `--transparency <0-255>` | 设置窗口透明度（0=完全透明，255=不透明） |
| `--font-name <名称>` | 指定显示字体（系统中已安装的字体族名称） |
| `--font-path <路径>` | 从外部字体文件加载字体（支持 .ttf/.otf） |
| `-h`, `--help` | 显示帮助信息 |
| `-v`, `--version` | 显示版本信息 |

### 示例

```cmd
TimeDisplay.exe                          # 普通模式启动
TimeDisplay.exe -f -p                    # 全屏+置顶启动
TimeDisplay.exe --no-button --transparency 200  # 无按钮半透明
TimeDisplay.exe -k -b                    # 禁用热键并隐藏按钮
TimeDisplay.exe --font-name "Microsoft YaHei"   # 使用微软雅黑字体
TimeDisplay.exe --font-path "myfont.ttf" --font-name "MyFont"  # 加载外部字体
```

## 项目结构

```
├── main.cpp                 # 主程序逻辑，窗口创建、时间显示、事件处理
├── d2d_renderer.cpp/h       # Direct2D/DirectWrite 渲染器（硬件加速 + 软件回退）
├── fullscreen.cpp/h         # 全屏模式管理
├── help.cpp/h               # 帮助信息与版本信息显示模块
├── ntp_client.cpp/h         # NTP 客户端实现，用于网络时间同步
├── app.rc / resource.h      # 资源文件（图标、嵌入字体等）
├── app.manifest             # 应用程序清单（DPI 感知等）
├── Font/
│   ├── SourceHanSans-Regular-Subset.otf   # 裁剪嵌入的思源黑体
│   └── OFL.txt                            # SIL Open Font License
└── FontNeed.txt             # 字体字符覆盖性验证清单
```

## 技术栈

- Win32 API
- C++17 (多线程、原子操作)
- Direct2D / DirectWrite（硬件加速 GPU 渲染 + 软件回退）
- NTP 协议 (RFC 4330)
- Windows Socket (winsock2)
- DWM (窗口圆角)

## 兼容性说明

渲染器采用 **硬件加速 + 自动降级** 策略：

| 场景 | 表现 |
|------|------|
| Windows 10/11（有 GPU） | D2D 硬件加速渲染 |
| Windows 10/11（无 GPU / 远程桌面） | 自动回退到 D2D 软件渲染（WARP）或 GDI |
| Windows 7 + KB971644 | D2D 硬件加速渲染 |
| Windows 7 无平台更新 | D2D 不可用 → 自动走 GDI 回退 |
| 任意平台 GPU 驱动崩溃 | `D2DERR_RECREATE_TARGET` 自动重建资源 |

D2D 回退到 GDI 后，所有功能完全正常，仅渲染方式从 GPU 加速切换为 CPU 软渲染。

## 许可证

本项目基于 MIT License 开源。

字体文件 `Font/SourceHanSans-Regular-Subset.otf` 基于思源黑体（Source Han Sans）裁剪生成，遵循 SIL Open Font License v1.1。



> 此文部分内容可能由AI生成