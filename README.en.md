# TimeDisplay

A feature-rich Windows desktop time display application supporting NTP network time synchronization, full-screen mode, custom fonts, and more.

## Features

- **Time Display**: Real-time display of current time and date, with network/local time switching support
- **NTP Time Synchronization**: Automatically synchronizes system time via the Network Time Protocol, with built-in support for multiple NTP servers
- **Full-Screen Mode**: Supports full-screen display, ideal for clock displays or large-screen presentations
- **Window Topmost**: Supports always-on-top mode to keep the window in front
- **Transparency Control**: Set window transparency via command line (0–255)
- **Embedded Font**: Ships with a subset of Source Han Sans, no system font dependency required for consistent appearance across devices
- **Custom Font**: Specify a system font via `--font-name` or load an external font file via `--font-path`
- **Touch Support**: Supports touch operations, suitable for touchscreen devices
- **DPI Aware**: Supports high-DPI displays (Per-Monitor V2), ensuring clear rendering at various resolutions
- **Dynamic Font Sizing**: Automatically adjusts font size based on window dimensions; fonts follow window size in real-time during resize
- **D2D Accelerated Rendering**: Uses Direct2D/DirectWrite GPU hardware acceleration for better performance than traditional GDI
- **Graceful Degradation**: Automatically falls back to D2D software rendering or GDI when GPU is unavailable; compatible with Windows 7~11
- **Command Line Options**: Rich set of startup parameters

## System Requirements

- Windows 7 or later (Direct2D requires KB2670838 Platform Update; falls back to GDI rendering when it is missing)
- Visual Studio 2019 / 2022 / 2026 (recommended, using v142/v143/v145 toolset)
- Windows SDK 10.0+

## Build Instructions

### Method 1: Visual Studio (Recommended)

1. Open `TimeDisplay.sln` with Visual Studio
2. Select the target configuration (Debug/Release) and platform (x86/x64) in Solution Explorer
3. Press Ctrl+Shift+B or select **Build → Build Solution**

> Note: The project has been adapted for the VS 2026 toolset (v145). If prompted to upgrade the toolset when opening the .sln file, confirm to proceed.

### Method 2: Command Line

```cmd
msbuild TimeDisplay.sln /p:Configuration=Release /p:Platform=x64
```

## Usage

### Basic Operations

| Action | Description |
|--------|-------------|
| Fullscreen Button / F11 | Toggle full-screen / windowed mode |
| Exit Button / Esc | Exit full-screen mode or close the program |
| Sync Button / F5 | Manually synchronize NTP time |
| Topmost Button / F6 | Toggle window always-on-top state |
| Ctrl+Shift+Q | Exit the program |
| Double-click title bar | Toggle full-screen mode (also supports touch double-tap) |

### Command Line Options

| Option | Description |
|--------|-------------|
| `-b`, `--no-button` | Hide all buttons |
| `-f`, `--full-screen` | Start in full-screen mode automatically |
| `-k`, `--nok-key` | Disable global hotkeys |
| `-p`, `--topmost` | Start with window always on top |
| `-t`, `--transparency <0-255>` | Set window transparency (0=fully transparent, 255=opaque) |
| `--font-name <name>` | Specify display font (name of an installed system font family) |
| `--font-path <path>` | Load font from an external file (supports .ttf/.otf) |
| `--debug [path]` | Output debug log (defaults to `TimeDisplay.log` next to the exe; file or directory) |
| `-h`, `--help` | Show help information |
| `-v`, `--version` | Show version information |

### Examples

```cmd
TimeDisplay.exe                                   # Normal startup
TimeDisplay.exe -f -p                             # Fullscreen + always on top
TimeDisplay.exe --no-button --transparency 200    # No buttons, semi-transparent
TimeDisplay.exe -k -b                             # Disable hotkeys, hide buttons
TimeDisplay.exe --font-name "Microsoft YaHei"     # Use Microsoft YaHei font
TimeDisplay.exe --font-path "myfont.ttf" --font-name "MyFont"  # Load external font
TimeDisplay.exe --debug                           # Output debug log
```

## Project Structure

```
├── main.cpp                 # Main program logic — window creation, time display, event handling
├── d2d_renderer.cpp/h       # Direct2D/DirectWrite renderer (hardware accelerated + software fallback)
├── fullscreen.cpp/h         # Full-screen mode management
├── debug_log.cpp/h          # Debug log module (--debug)
├── ntp_client.cpp/h         # NTP client implementation for network time synchronization
├── app.rc / resource.h      # Resources (icons, embedded fonts, etc.)
├── app.manifest             # Application manifest (DPI awareness, etc.)
├── Font/
│   ├── SourceHanSans-Regular-Subset.otf   # Subsetted embedded Source Han Sans font
│   └── OFL.txt                            # SIL Open Font License
└── FontNeed.txt             # Font character coverage verification checklist
```

## Technology Stack

- Win32 API
- C++17 (multithreading, atomic operations)
- Direct2D / DirectWrite (GPU hardware accelerated rendering + software fallback)
- NTP Protocol (RFC 4330)
- Windows Socket (winsock2)
- DWM (window rounding)

## Compatibility Notes

The renderer uses a **hardware accelerated + automatic fallback** strategy:

| Scenario | Behavior |
|----------|----------|
| Windows 10/11 (with GPU) | D2D hardware accelerated rendering |
| Windows 10/11 (no GPU / Remote Desktop) | Auto fallback to D2D software rendering (WARP) or GDI |
| Windows 7 + KB2670838 | D2D hardware accelerated rendering |
| Windows 7 without Platform Update | D2D unavailable → automatic GDI fallback |
| Any platform GPU driver crash | `D2DERR_RECREATE_TARGET` auto-rebuilds resources |

When D2D falls back to GDI, all features continue to work normally — only the rendering path switches from GPU acceleration to CPU software rendering.

## License

This project is open-sourced under the MIT License.

The font file `Font/SourceHanSans-Regular-Subset.otf` is derived from Source Han Sans and is subject to the SIL Open Font License v1.1.

> **Some content in this article may have been generated by artificial intelligence.**
