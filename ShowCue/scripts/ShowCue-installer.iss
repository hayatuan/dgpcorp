; ShowCue v1.0.0 — Inno Setup (Windows 10/11 x64)
;
; Cách dùng:
;   1. Build Release: cmake --build build-win --config Release --target ShowCue
;   2. Mở file .iss trong Inno Setup Compiler → Compile
;   3. Output: D:\APP\RC\ShowCue-Setup-1.0.0.exe
;
; ffmpeg: copy ffmpeg.exe vào cùng thư mục ShowCue.exe ({app}\ffmpeg.exe).

#define MyAppName "ShowCue"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "DGP Corp"
#define MyAppURL "https://github.com/hayatuan/dgpcorp"
#define MyAppExeName "ShowCue.exe"

; Đường dẫn tuyệt đối tương đối thư mục chứa file .iss (SourcePath)
#define BuildDir AddBackslash(SourcePath) + "..\..\build-win\ShowCue\ShowCue_artefacts\Release"
#define AppExePath BuildDir + "\ShowCue.exe"
#define FfmpegBuildPath BuildDir + "\ffmpeg.exe"
#define FfmpegFallback AddBackslash(SourcePath) + "..\ThirdParty\ffmpeg\win\ffmpeg.exe"
#define ReleaseDir "D:\APP\RC"
#define IconPath AddBackslash(SourcePath) + "..\Resources\AppIcon.ico"

; Kiểm tra lúc COMPILE — dùng #ifexist/#ifnexist (không dùng #ifnot FileExists)
#ifnexist AppExePath
  #error "Khong tim thay ShowCue.exe. Build truoc: cmake --build build-win --config Release --target ShowCue"
#endif

#ifexist FfmpegBuildPath
  #define FfmpegSource FfmpegBuildPath
#else
  #ifexist FfmpegFallback
    #define FfmpegSource FfmpegFallback
  #else
    #error "Thieu ffmpeg.exe - dat vao ThirdParty\ffmpeg\win hoac build lai ShowCue"
  #endif
#endif

[Setup]
AppId={{A7B3C9D1-4E2F-5A6B-8C0D-1E2F3A4B5C6D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#ReleaseDir}
OutputBaseFilename=ShowCue-Setup-{#MyAppVersion}
SetupIconFile={#IconPath}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion=1.0.0.0
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Cai dat day du (khuyen nghi)"

[Components]
Name: "main"; Description: "Ung dung ShowCue"; Types: full; Flags: fixed
Name: "ffmpeg"; Description: "FFmpeg — can cho keo tha / trich audio tu video"; Types: full; Flags: fixed

[Tasks]
Name: "desktopicon"; Description: "Tao shortcut tren Desktop"; GroupDescription: "Shortcut:"; Flags: unchecked

[Files]
Source: "{#AppExePath}"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "{#FfmpegSource}"; DestDir: "{app}"; DestName: "ffmpeg.exe"; Components: ffmpeg; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Go cai {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Chay {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectComponents then
  begin
    if MsgBox(
      'ShowCue can FFmpeg de xu ly file video (keo tha MP4, MOV, ...).' + #13#10#13#10 +
      'Bo cai se dat ffmpeg.exe canh ShowCue.exe — ban khong can tai FFmpeg rieng.' + #13#10#13#10 +
      'Tiep tuc cai dat?',
      mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not FileExists(ExpandConstant('{app}\ffmpeg.exe')) then
      MsgBox('Canh bao: ffmpeg.exe khong co trong thu muc cai dat. Tinh nang video se khong hoat dong.',
             mbError, MB_OK);
  end;
end;
