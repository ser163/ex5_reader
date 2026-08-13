; EX5 阅读器 Inno Setup 打包脚本
; 编译: "E:\Program Files\Inno Setup 7\ISCC.exe" installer\ex5_setup.iss

#define MyAppName "EX5 阅读器"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "EX5 Reader"

[Setup]
AppId={{7C9A2E41-5B3D-4E6F-9A1C-2D8E0F4B6A73}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion=1.0.0.0
VersionInfoDescription=EX5 阅读器安装程序
DefaultDirName={autopf}\EX5Reader
DefaultGroupName={#MyAppName}
OutputDir=output
OutputBaseFilename=ex5reader_setup_v1.0.0
SetupIconFile=..\images\ex5_reader.ico
WizardImageFile=wizard.bmp
WizardSmallImageFile=wizard_small.bmp
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
DisableProgramGroupPage=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"

[Registry]
; .ex5 文件关联:双击 .ex5 直接用 GUI 阅读器打开
Root: HKCR; Subkey: ".ex5"; ValueType: string; ValueName: ""; ValueData: "EX5.Book"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "EX5.Book"; ValueType: string; ValueName: ""; ValueData: "EX5 电子书"; Flags: uninsdeletekey
Root: HKCR; Subkey: "EX5.Book\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\ex5_reader.ico"
Root: HKCR; Subkey: "EX5.Book\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\ex5reader_gui.exe"" ""%1"""
Root: HKCR; Subkey: "EX5.Book\shell\open"; ValueType: string; ValueName: ""; ValueData: "用 EX5 阅读器打开"

[Files]
Source: "..\bin\ex5reader_gui.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\ex5reader.exe";     DestDir: "{app}"; Flags: ignoreversion
Source: "..\images\ex5_reader.ico"; DestDir: "{app}"
Source: "..\bin\plugins\*.dll";    DestDir: "{app}\plugins"; Flags: ignoreversion
Source: "..\samples\sample_book.ex5"; DestDir: "{app}\samples"
Source: "..\docs\产品文档.md";        DestDir: "{app}\docs"
Source: "..\docs\插件规范.md";        DestDir: "{app}\docs"

[Icons]
Name: "{group}\{#MyAppName}";            Filename: "{app}\ex5reader_gui.exe"; IconFilename: "{app}\ex5_reader.ico"
Name: "{group}\{#MyAppName}(命令行版)";   Filename: "{app}\ex5reader.exe"
Name: "{group}\阅读示例书《海与灯》";      Filename: "{app}\ex5reader_gui.exe"; Parameters: """{app}\samples\sample_book.ex5"""
Name: "{group}\插件开发规范";               Filename: "{app}\docs\插件规范.md"
Name: "{autodesktop}\{#MyAppName}";      Filename: "{app}\ex5reader_gui.exe"; IconFilename: "{app}\ex5_reader.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\ex5reader_gui.exe"; Parameters: """{app}\samples\sample_book.ex5"""; Description: "安装完成后打开示例书《海与灯》"; Flags: postinstall nowait skipifsilent unchecked
