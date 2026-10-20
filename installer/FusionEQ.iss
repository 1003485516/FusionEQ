; Fusion EQ — Windows 安装包脚本（Inno Setup 6/7）
; 安装内容：VST3 插件 → Common Files\VST3 ；独立版 → Program Files\Fusion EQ

#define MyAppName "Fusion EQ"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Fusion Audio"
#define BuildDir "C:\Users\Administrator\WorkBuddy\2026-10-18-20-24-15\FusionEQ\build\FusionEQ_artefacts\Release"

[Setup]
AppId={{7E4C1A92-5B3D-46F8-9E21-A6C0D8F4B7E5}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=C:\Users\Administrator\Desktop
OutputBaseFilename=FusionEQ-{#MyAppVersion}-Setup
SetupIconFile=C:\Users\Administrator\WorkBuddy\2026-10-18-20-24-15\FusionEQ\installer\fusioneq.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\Fusion EQ.exe

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; VST3 插件（bundle 整体安装到 Common Files\VST3，DAW 自动扫描）
Source: "{#BuildDir}\VST3\Fusion EQ.vst3\*"; DestDir: "{commoncf}\VST3\Fusion EQ.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion uninsrestartdelete
; 独立版可执行文件
Source: "{#BuildDir}\Standalone\Fusion EQ.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\Fusion EQ.exe"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\Fusion EQ.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Fusion EQ.exe"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent
