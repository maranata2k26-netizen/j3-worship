#define MyAppName "J3 Worship"
#define MyAppVersion "1.10.6"
#define MyAppPublisher "J3 Worship"
#define MyAppExeName "J3Worship.exe"

[Setup]
AppId={{C3B653D6-AB0E-4F8D-90C2-6F48C93D1A77}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={code:GetInstallDir}
DefaultGroupName=J3 Worship
OutputDir=..\dist-installer
OutputBaseFilename=J3Worship-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UsePreviousAppDir=no
CloseApplications=yes
RestartApplications=no
SetupIconFile=..\resources\J3Worship.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion=1.10.6.0
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

[Code]
var
  LegacyExePath: string;

function DetectLegacyUpdaterTarget(var TargetExe: string): Boolean;
var
  ScriptPath: string;
  Lines: TArrayOfString;
  Line: string;
  Marker: string;
  Candidate: string;
  I: Integer;
  P: Integer;
  Q: Integer;
begin
  Result := False;
  TargetExe := '';
  ScriptPath := AddBackslash(GetEnv('TEMP')) + 'J3WorshipUpdater\apply-update.cmd';
  if not FileExists(ScriptPath) then
    Exit;

  if not LoadStringsFromFile(ScriptPath, Lines) then
    Exit;

  Marker := 'start "" "';
  for I := GetArrayLength(Lines) - 1 downto 0 do
  begin
    Line := Trim(Lines[I]);
    P := Pos(Marker, Lowercase(Line));
    if P > 0 then
    begin
      Candidate := Copy(Line, P + Length(Marker), Length(Line));
      Q := Pos('"', Candidate);
      if Q > 0 then
        Candidate := Copy(Candidate, 1, Q - 1);

      if (CompareText(ExtractFileName(Candidate), '{#MyAppExeName}') = 0)
        and FileExists(Candidate) then
      begin
        TargetExe := Candidate;
        Result := True;
        Exit;
      end;
    end;
  end;
end;

function GetInstallDir(Param: string): string;
begin
  if LegacyExePath <> '' then
    Result := ExpandConstant('{localappdata}\\J3 Worship\\UpdaterStage\\{#MyAppVersion}')
  else
    Result := ExpandConstant('{autopf}\\J3 Worship');
end;

function InitializeSetup(): Boolean;
begin
  LegacyExePath := '';
  DetectLegacyUpdaterTarget(LegacyExePath);
  if LegacyExePath <> '' then
    Log('Legacy J3 updater target detected: ' + LegacyExePath);
  Result := True;
end;

procedure ForceReplaceLegacyExecutable;
var
  SourceExe: string;
  TargetDir: string;
  ResultCode: Integer;
  Attempt: Integer;
  Copied: Boolean;
  MigrationLog: string;
begin
  if LegacyExePath = '' then
    Exit;

  SourceExe := ExpandConstant('{app}\{#MyAppExeName}');
  if CompareText(SourceExe, LegacyExePath) = 0 then
  begin
    Log('Legacy target is the active install path; normal installer replacement already applies.');
    Exit;
  end;

  TargetDir := ExtractFileDir(LegacyExePath);
  MigrationLog := AddBackslash(GetEnv('TEMP')) + 'J3WorshipUpdater\migration-result.txt';

  Log('Forcing legacy executable migration from ' + SourceExe + ' to ' + LegacyExePath);

  Exec(
    ExpandConstant('{cmd}'),
    '/C taskkill /F /T /IM {#MyAppExeName} >nul 2>&1',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode);
  Sleep(700);

  ForceDirectories(TargetDir);
  Copied := False;
  for Attempt := 1 to 40 do
  begin
    if FileExists(LegacyExePath) then
      DeleteFile(LegacyExePath);

    if (not FileExists(LegacyExePath)) and FileCopy(SourceExe, LegacyExePath, False) then
    begin
      Copied := True;
      Break;
    end;

    Sleep(150);
  end;

  if Copied then
  begin
    SaveStringToFile(
      MigrationLog,
      'OK' + #13#10 +
      'source=' + SourceExe + #13#10 +
      'target=' + LegacyExePath + #13#10 +
      'version={#MyAppVersion}' + #13#10,
      False);
    Log('Legacy executable replaced successfully.');
  end
  else
  begin
    SaveStringToFile(
      MigrationLog,
      'FAILED' + #13#10 +
      'source=' + SourceExe + #13#10 +
      'target=' + LegacyExePath + #13#10 +
      'version={#MyAppVersion}' + #13#10,
      False);
    RaiseException('J3 Worship could not replace the legacy executable: ' + LegacyExePath);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    ForceReplaceLegacyExecutable;
end;
