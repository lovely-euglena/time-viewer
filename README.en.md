# TimeDisplay

A feature-rich Windows desktop time display application supporting NTP network time synchronization, full-screen mode, and more.

## Features

- **Time Display**: Real-time display of current time and date
- **NTP Time Synchronization**: Automatically synchronizes system time via the Network Time Protocol, with built-in support for multiple NTP servers
- **Full-Screen Mode**: Supports full-screen display, ideal for clock displays or large-screen presentations
- **Touch Support**: Supports touch operations, suitable for touchscreen devices
- **DPI Awareness**: Supports high-DPI displays to ensure clear rendering at various resolutions
- **Dynamic Font Sizing**: Automatically adjusts font size based on window dimensions

## System Requirements

- Windows 7 or later
- Visual Studio 2015 or later
- Windows SDK

## Build Instructions

1. Open the `TimeDisplay.sln` file using Visual Studio
2. Select the appropriate configuration (Debug/Release) and platform (x86/x64)
3. Press Ctrl+Shift+B or select "Build Solution"

## Usage Instructions

- **Full-Screen Button**: Click to toggle full-screen mode
- **Sync Button**: Manually synchronize NTP time
- **ESC Key**: Exit full-screen mode
- **Double-Click Window Title Bar**: Enter full-screen mode

## Project Structure

- `main.cpp` - Main program logic, including window creation and time display
- `ntp_client.cpp/h` - NTP client implementation for network time synchronization
- `fullscreen.cpp/h` - Full-screen mode management
- `resource.h` - Resource definitions

## Technology Stack

- Win32 API
- C++11 (multithreading, atomic operations)
- NTP Protocol
- Windows Sockets

## License

MIT License