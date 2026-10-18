; HEM Studio Suite - Inno Setup 6 script (same kind of installer as Hybrid Studio and Rework Playground)
;
; Build:  iscc /DBuildDir="<folder with Hybrid Elite Mafia - Studio Suite.exe>" /DAppVersion=0.11.6 Installer\HEMStudioSuite.iss
;
; - AppId is fixed forever, so every new version installs over the old one in place.
; - Sessions, media and settings are never listed here: upgrades and uninstall leave them alone.
; - Static MSVC runtime and all artwork are inside the exe: no VC++ redistributable needed.
; - Separate from ChatGPT's earlier per-version installer (%LOCALAPPDATA%\Programs\HEMStudioSuite\Versions);
;   that one has its own "Remove this version" shortcut.

#ifndef AppVersion
  #define AppVersion "0.11.6"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\HEMStudioSuite_artefacts\Release"
#endif

#define AppName      "HEM Studio Suite"
#define AppPublisher "Hybrid Elite Mafia"
#define AppExe       "Hybrid Elite Mafia - Studio Suite.exe"

[Setup]
AppId={{8D201860-8E31-46B3-BC7D-6076648E9FDB}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
UsePreviousAppDir=yes
UsePreviousGroup=yes
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=HEM-Studio-Suite-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no
SetupIconFile=HEM-Studio-Suite.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoCopyright=Copyright (c) 2026 Steven Reliford / Hybrid Elite Mafia

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}\Notices"; Flags: ignoreversion
Source: "NOTICE-*.txt"; DestDir: "{app}\Notices"; Flags: ignoreversion
Source: "SPECTRAL-ATTRIBUTION.txt"; DestDir: "{app}\Notices"; Flags: ignoreversion
Source: "SPECTRAL-LICENSE-AGPL-3.0.txt"; DestDir: "{app}\Notices"; Flags: ignoreversion
Source: "INSTRUMENTAL-ATTRIBUTION.txt"; DestDir: "{app}\Notices"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\{#AppName} - Read me"; Filename: "{app}\README.txt"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
