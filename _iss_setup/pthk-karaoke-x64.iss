; PTHK Karaoke (based on KaraMixer Edition / Handy Karaoke, GPLv3) - Inno Setup script
; Build with:  iscc _iss_setup\pthk-karaoke-x64.iss
; Expects the deployed program in D:\REA\PTHK-Build\deploy (see D:\REA\build-pthk.ps1).

#ifndef AppVersion
  #define AppVersion "1.1.0"
#endif
#define MyAppName "PTHK Karaoke"
#define MyAppPublisher "PTHK"
#define MyAppExeName "PTHKKaraoke.exe"
#define DeployDir "D:\REA\PTHK-Build\deploy"

[Setup]
AppId={{8E3B6F2A-7C14-4B9D-9A2E-5D61C0F4B7A3}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppVerName={#MyAppName} {#AppVersion}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#MyAppPublisher}
AppPublisher={#MyAppPublisher}
AppCopyright=Copyright (C) 2026 PTHK - based on KaraMixer Edition (Buai Music) and Handy Karaoke (pie62), GPLv3
DefaultDirName={autopf}\PTHK Karaoke
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=..\gpl-3.0.rtf
OutputDir=..\dist
OutputBaseFilename=PTHK-Karaoke-{#AppVersion}-x64-setup
SetupIconFile=..\Icons\App\pthk.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma
SolidCompression=yes
DisableDirPage=no
UsePreviousAppDir=no
AlwaysShowDirOnReadyPage=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; all users (Program Files, default) or only me / USB drive (no admin rights needed)
PrivilegesRequiredOverridesAllowed=dialog commandline

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "portable"; Description: "โหมดพกพาสำหรับ Flash Drive / External HD (เก็บตั้งค่าและฐานข้อมูลเพลงไว้ในโฟลเดอร์ที่ติดตั้ง)"; GroupDescription: "โหมดพกพา (Portable Mode):"; Flags: unchecked

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated; Check: IsAdminInstallMode
Filename: "{app}\{#MyAppExeName}"; Flags: nowait postinstall skipifsilent; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"
; automatic update (started by the program with /update=1): open the program again when done
Filename: "{app}\{#MyAppExeName}"; Flags: nowait; Check: IsAutoUpdate

[Dirs]
Name: "{app}\SoundFonts"; Flags: uninsneveruninstall
Name: "{app}\VST"; Flags: uninsneveruninstall
Name: "{app}\Songs"; Flags: uninsneveruninstall
Name: "{app}\Songs\NCN"; Flags: uninsneveruninstall
Name: "{app}\Songs\NCN\Song"; Flags: uninsneveruninstall
Name: "{app}\Songs\NCN\Lyrics"; Flags: uninsneveruninstall
Name: "{app}\Songs\NCN\Cursor"; Flags: uninsneveruninstall
Name: "{app}\Songs\HNK"; Flags: uninsneveruninstall
Name: "{app}\Songs\KAR"; Flags: uninsneveruninstall
Name: "{app}\Songs\VIDEO"; Flags: uninsneveruninstall
Name: "{app}\Songs\MP3"; Flags: uninsneveruninstall
Name: "{app}\Config"; Tasks: portable; Flags: uninsneveruninstall
Name: "{app}\Data"; Tasks: portable; Flags: uninsneveruninstall
Name: "{app}\Tools"; Tasks: portable; Flags: uninsneveruninstall

[Files]
; the portable Config / Data folders of the build are not packed: the installed program keeps
; its settings and song database in %USERPROFILE%\.PTHKKaraoke (or {app}\Config and {app}\Data in portable mode)
; (developer logs / leftovers are never packed; Tools and models are downloaded on first use)
Source: "{#DeployDir}\*"; DestDir: "{app}"; Excludes: "Config,Data,Tools,models,vc_redist.x64.exe,KaraMixer.exe,*.pdb,*.log,*.old,*.bak,debug-logs.txt"; Flags: ignoreversion recursesubdirs createallsubdirs
; On the development PC only ("external": read at install time, not packed into Setup.exe; skipped
; where the build folder does not exist): start with the settings and the song database already
; made there, instead of choosing the folders and building the database again. Never overwritten.
Source: "{#DeployDir}\Config\*.conf"; DestDir: "{%USERPROFILE}\.PTHKKaraoke\Config"; Tasks: not portable; Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{#DeployDir}\Data\Database.db3"; DestDir: "{%USERPROFILE}\.PTHKKaraoke\Data"; Tasks: not portable; Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
; portable install: the same, but into the program folder itself (travels with the drive)
Source: "{#DeployDir}\Config\*.conf"; DestDir: "{app}\Config"; Tasks: portable; Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{#DeployDir}\Data\Database.db3"; DestDir: "{app}\Data"; Tasks: portable; Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "..\NOTICE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\gpl-3.0.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: ignoreversion deleteafterinstall

[Code]
function IsAutoUpdate: Boolean;
begin
  Result := ExpandConstant('{param:update|0}') = '1';
end;
