#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef PackageDir
  #define PackageDir "..\..\dist\windows"
#endif

[Setup]
AppId={{B2B55096-996D-480E-A1BE-89849B7D6088}
AppName=Simpl
AppVersion={#AppVersion}
AppPublisher=ZolVo-o
DefaultDirName={autopf}\Simpl
DefaultGroupName=Simpl
OutputDir=..\..\dist
OutputBaseFilename=simpl-{#AppVersion}-windows-setup
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayName=Simpl
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Simpl"; Filename: "{app}\simpl.cmd"; WorkingDir: "{app}"
Name: "{group}\Uninstall Simpl"; Filename: "{uninstallexe}"; WorkingDir: "{app}"

[Code]
function InitializeSetup(): Boolean;
var
  PythonStdlib: String;
begin
  PythonStdlib := 'C:\msys64\ucrt64\lib\python3.14\encodings\__init__.py';
  if not FileExists(PythonStdlib) then
  begin
    MsgBox('Для Simpl требуется MSYS2 UCRT64 с Python 3.14.' + #13#10 + #13#10 +
      'Установите MSYS2 в C:\msys64, откройте терминал UCRT64 и выполните:' + #13#10 +
      'pacman -S mingw-w64-ucrt-x86_64-python' + #13#10 + #13#10 +
      'После установки запустите этот установщик снова.', mbError, MB_OK);
    Result := False;
  end
  else
    Result := True;
end;
