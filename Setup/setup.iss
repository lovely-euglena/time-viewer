; 时间屏幕简易安装脚本
#define MyAppName "时间屏幕"
#define MyAppExeName "TimeDisplay.exe"
#define SourcePath "E:\code\TimeViwer\Release"

[Setup]
AppName={#MyAppName}
AppVersion=1.1
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=E:\code\TimeViwer\Setup
OutputBaseFilename=时间屏幕安装包
Compression=lzma2
SolidCompression=yes

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式:"

[Files]
Source: "{#SourcePath}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动时间屏幕"; Flags: nowait postinstall skipifsilent