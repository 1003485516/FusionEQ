; Fusion EQ — Windows 安装包脚本（Inno Setup 6/7）
; 组件直接在列表里勾选：VST3 / AAX（Pro Tools）
; VST3 → Common Files\VST3 ；AAX → Common Files\Avid\Audio\Plug-Ins
; 不打包独立运行版（Standalone 仅供自测，直接运行构建目录里的 exe 即可）
;
; 推荐用 installer\build_installer.bat 编译，它会把构建目录的绝对路径通过
; /DBuildDir= 传进来；直接双击本脚本编译时则使用下面的默认路径。

#define MyAppName "Fusion EQ"
#define MyAppVersion "1.0.3"
#define MyAppPublisher "OH Audio"

; 构建产物目录（绝对路径）。可由命令行 /DBuildDir=... 覆盖
#ifndef BuildDir
  #define BuildDir "E:\FusionEQ\FusionEQ\build\FusionEQ_artefacts\Release"
#endif

[Setup]
AppId={{7E4C1A92-5B3D-46F8-9E21-A6C0D8F4B7E5}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=E:\FusionEQ\安装包
OutputBaseFilename=FusionEQ-{#MyAppVersion}-Setup
SetupIconFile=..\installer\fusioneq.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\fusioneq.ico

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

; 只有一个自定义类型：不搞「安装类型」下拉选择，直接在组件列表里勾选
[Types]
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "vst3"; Description: "{cm:CompVST3}"; Types: custom
#if FileExists(BuildDir + "\AAX\Fusion EQ.aaxplugin\Contents\x64\Fusion EQ.aaxplugin")
Name: "aax"; Description: "{cm:CompAAX}"; Types: custom
#endif

[Files]
; VST3 插件（bundle 整体安装到 Common Files\VST3，DAW 自动扫描）
Source: "{#BuildDir}\VST3\Fusion EQ.vst3\*"; DestDir: "{commoncf}\VST3\Fusion EQ.vst3"; \
    Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion uninsrestartdelete
; AAX 插件（Pro Tools）：仅当构建时产出 AAX 才会出现在列表里（需要 Avid AAX SDK）
#if FileExists(BuildDir + "\AAX\Fusion EQ.aaxplugin\Contents\x64\Fusion EQ.aaxplugin")
Source: "{#BuildDir}\AAX\Fusion EQ.aaxplugin\*"; \
    DestDir: "{commoncf}\Avid\Audio\Plug-Ins\Fusion EQ.aaxplugin"; \
    Components: aax; Flags: recursesubdirs createallsubdirs ignoreversion uninsrestartdelete
#endif
; 图标（卸载程序显示用）
Source: "..\installer\fusioneq.ico"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; 老版本安装过的独立版 exe，升级安装时清理
Type: files; Name: "{app}\Fusion EQ.exe"

[Icons]
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[CustomMessages]
TypeCustom=选择安装组件
CompVST3=VST3 插件（Cubase / FL Studio / Ableton Live / Studio One 等）
CompAAX=AAX 插件（Pro Tools）
chinesesimplified.TypeCustom=选择安装组件
chinesesimplified.CompVST3=VST3 插件（Cubase / FL Studio / Ableton Live / Studio One 等）
chinesesimplified.CompAAX=AAX 插件（Pro Tools）
english.TypeCustom=Choose components to install
english.CompVST3=VST3 plug-in (Cubase / FL Studio / Ableton Live / Studio One, etc.)
english.CompAAX=AAX plug-in (Pro Tools)
