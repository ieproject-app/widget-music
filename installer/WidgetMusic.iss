#define MyAppName "SnipTune 10"
#define MyAppVersion "1.0.5"
#define MyAppPublisher "SnipGeek"
#define MyAppExeName "WidgetMusicHost.exe"
#define MyDistDir "..\out\dist\SnipTune10"

[Setup]
AppId={{8A94D033-9199-4E50-BE8B-B2A196332975}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://snipgeek.com
AppSupportURL=https://snipgeek.com
AppUpdatesURL=https://snipgeek.com
DefaultDirName={localappdata}\SnipGeek\SnipTune 10
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\out\dist
OutputBaseFilename=SnipTune10Setup-{#MyAppVersion}-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
MinVersion=10.0
ArchitecturesAllowed=x64os
SetupIconFile=..\assets\icons\snipgeek.ico
InfoAfterFile=AfterInstall.txt
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion=1.0.5.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Windows 10 DeskBand Installer
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#MyDistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{app}\Enable-WidgetMusicTaskbar.ps1"
Type: files; Name: "{app}\Invoke-WidgetMusicTaskbarEnable.ps1"

[Run]
Filename: "{app}\Register-WidgetMusic.cmd"; Parameters: "restart"; WorkingDir: "{app}"; StatusMsg: "Registering SnipTune 10 and restarting Explorer..."; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "{app}\Unregister-WidgetMusic.cmd"; Parameters: "restart"; WorkingDir: "{app}"; RunOnceId: "UnregisterWidgetMusic"; Flags: runhidden waituntilterminated

[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  UnregisterScript: String;
begin
  Result := '';
  UnregisterScript := ExpandConstant('{app}\Unregister-WidgetMusic.cmd');
  if FileExists(UnregisterScript) then
  begin
    if not Exec(UnregisterScript, 'restart', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    begin
      Result := 'Could not unregister the existing SnipTune 10 installation before updating.';
      Exit;
    end;
    if ResultCode <> 0 then
    begin
      Result := 'The existing SnipTune 10 installation could not be unregistered before updating.';
      Exit;
    end;
  end;
end;
