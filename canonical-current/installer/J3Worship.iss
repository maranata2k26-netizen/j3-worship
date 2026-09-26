#define MyAppName "J3 Worship"
#define MyAppVersion "1.10.3"
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
VersionInfoVersion=1.10.3.0
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
function TryGetLegacyUpdaterDir(var LegacyDir: string): Boolean;
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
  LegacyDir := '';
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
        LegacyDir := ExtractFileDir(Candidate);
        Result := True;
        Exit;
      end;
    end;
  end;
end;

function GetInstallDir(Param: string): string;
var
  LegacyDir: string;
begin
  if TryGetLegacyUpdaterDir(LegacyDir) then
    Result := LegacyDir
  else
    Result := ExpandConstant('{autopf}\J3 Worship');
end;
