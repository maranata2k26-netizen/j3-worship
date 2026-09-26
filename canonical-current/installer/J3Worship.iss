#define MyAppName "J3 Worship"
#define MyAppVersion "1.7.2"
#define MyAppPublisher "J3 Worship"
#define MyAppExeName "J3Worship.exe"

[Setup]
AppId={{C3B653D6-AB0E-4F8D-90C2-6F48C93D1A77}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\J3 Worship
DefaultGroupName=J3 Worship
OutputDir=..\dist-installer
OutputBaseFilename=J3Worship-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile=..\resources\J3Worship.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion=1.7.2.0
VersionInfoCompany=J3 Worship
VersionInfoDescription=J3 Worship Installer
VersionInfoProductName=J3 Worship

[Files]
Source: "..\dist\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\J3 Worship"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\J3 Worship"; Filename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Ejecutar J3 Worship"; Flags: nowait postinstall skipifsilent
