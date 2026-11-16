; PTHK Karaoke (based on KaraMixer Edition / Handy Karaoke, GPLv3) - Inno Setup script
; Build with:  iscc _iss_setup\pthk-karaoke-x64.iss
; Expects the deployed program in D:\REA\PTHK-Build\deploy (see D:\REA\build-pthk.ps1).

#ifndef AppVersion
  #define AppVersion "1.2.0"
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
; everything in one folder that can be written to (songs, settings, database, SoundFonts, backgrounds):
; C:\PTHK Karaoke by default, no admin rights needed. Program Files can still be chosen.
DefaultDirName={sd}\PTHK Karaoke
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
; only me (default, C:\PTHK Karaoke / USB drive, no admin rights) or all users (Program Files)
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "portable"; Description: "รวมทุกอย่างไว้ในโฟลเดอร์ที่ติดตั้ง: เพลง ค่าตั้ง ฐานข้อมูล ซาวด์ฟอนต์ พื้นหลัง (แนะนำ - ยกทั้งโฟลเดอร์ไปเครื่องอื่น / Flash Drive / External HD ได้)"; GroupDescription: "ที่เก็บข้อมูล:"

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
Name: "{app}\Songs\SIB"; Flags: uninsneveruninstall
Name: "{app}\Backgrounds"; Flags: uninsneveruninstall
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
Source: "{#DeployDir}\*"; DestDir: "{app}"; Excludes: "Config,Data,Tools,models,vc_redist.x64.exe,KaraMixer.exe,*.pdb,*.lib,*.log,*.old,*.bak,debug-logs.txt,debug-offline.txt,\VST\*,\SoundFonts\*"; Flags: ignoreversion recursesubdirs createallsubdirs
; On the development PC only ("external": read at install time, not packed into Setup.exe; skipped
; where the build folder does not exist): start with the settings and the song database already
; made there, instead of choosing the folders and building the database again. Never overwritten.
Source: "{#DeployDir}\Config\*.conf"; DestDir: "{%USERPROFILE}\.PTHKKaraoke\Config"; Tasks: not portable; Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{#DeployDir}\Data\Database.db3"; DestDir: "{%USERPROFILE}\.PTHKKaraoke\Data"; Tasks: not portable; Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
; portable install: the same, but into the program folder itself (travels with the drive)
Source: "{#DeployDir}\Config\*.conf"; DestDir: "{app}\Config"; Tasks: portable; Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{#DeployDir}\Data\Database.db3"; DestDir: "{app}\Data"; Tasks: portable; Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
; a first one-folder install on a computer that already used PTHK Karaoke (settings in the user
; profile): bring those settings and medley sets into the folder ("external": read on that computer at
; install time, nothing is packed). Not when the folder already has its own. The song database comes
; on the program's first start (Config.cpp importProfileData): a running PTHK Karaoke keeps it open,
; and Setup could not read it (it stopped with "cancelled").
Source: "{%USERPROFILE}\.PTHKKaraoke\Config\*"; DestDir: "{app}\Config"; Tasks: portable; Check: FirstOneFolderInstall; Flags: external skipifsourcedoesntexist onlyifdoesntexist recursesubdirs createallsubdirs uninsneveruninstall
; the default sound for MIDI songs: GeneralUser GS by S. Christian Collins (free to redistribute,
; licence shipped next to it). The deploy folder's own SoundFonts / VST are never packed (third-party).
Source: "D:\REA\downloads\GeneralUser-GS\GeneralUser-GS.sf2"; DestDir: "{app}\SoundFonts"; Flags: ignoreversion
Source: "D:\REA\downloads\GeneralUser-GS\LICENSE.txt"; DestDir: "{app}\SoundFonts"; DestName: "GeneralUser-GS-LICENSE.txt"; Flags: ignoreversion
Source: "..\NOTICE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\gpl-3.0.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: ignoreversion deleteafterinstall

[Code]
var
  HadAppConfig: Boolean;

function IsAutoUpdate: Boolean;
begin
  Result := ExpandConstant('{param:update|0}') = '1';
end;

function FirstOneFolderInstall: Boolean;
begin
  Result := not HadAppConfig;
end;

function InProgramFiles(const Dir: String): Boolean;
begin
  Result := (Pos(Uppercase(ExpandConstant('{commonpf64}')), Uppercase(Dir)) = 1)
         or (Pos(Uppercase(ExpandConstant('{commonpf32}')), Uppercase(Dir)) = 1);
end;

// Program Files cannot be written to by the program: no songs / settings can be kept there
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectTasks) and WizardIsTaskSelected('portable') and InProgramFiles(WizardDirValue) then
  begin
    MsgBox('โฟลเดอร์ Program Files เก็บเพลงและค่าตั้งไม่ได้' + #13#10 +
           'กด Back แล้วเลือกโฟลเดอร์อื่น เช่น C:\PTHK Karaoke' + #13#10 +
           'หรือเอาติ๊ก "รวมทุกอย่างไว้ในโฟลเดอร์ที่ติดตั้ง" ออก', mbInformation, MB_OK);
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Lines: TArrayOfString;
begin
  if CurStep = ssInstall then
    HadAppConfig := FileExists(ExpandConstant('{app}\Config\PTHKKaraoke.conf'));
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('portable') then
  begin
    ForceDirectories(ExpandConstant('{app}\Config'));
    ForceDirectories(ExpandConstant('{app}\Data'));
    SetArrayLength(Lines, 2);
    Lines[0] := 'PTHK Karaoke: ทุกอย่างอยู่ในโฟลเดอร์นี้ (Config, Data, Songs, SoundFonts, VST, Backgrounds)';
    Lines[1] := 'ยกทั้งโฟลเดอร์ไปเครื่องอื่นหรือไดรฟ์พกพาได้ เปิด PTHKKaraoke.exe แล้วใช้ได้เลย';
    SaveStringsToUTF8File(ExpandConstant('{app}\Portable.txt'), Lines, False);
    // settings brought from the user profile: the program brings the song database too
    if (not HadAppConfig) and FileExists(ExpandConstant('{%USERPROFILE}\.PTHKKaraoke\Data\Database.db3')) then
      SetIniString('AutoSetup', 'ImportProfileData', 'true', ExpandConstant('{app}\Config\PTHKKaraoke.conf'));
  end;
end;
