#define MyAppName "Nova Host"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "NovaHost Developers"
#define MyAppURL "https://github.com/NovaHost/NovaHost"
#define MyAppExeName "Nova Host.exe"

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
AppId={{0C3D84A1-6E5F-48C2-9CE3-FF8A8E45781D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputBaseFilename=NovaHostSetup
SetupIconFile=..\Resources\icon.png
Compression=lzma
SolidCompression=yes
PrivilegesRequired=lowest
WizardStyle=modern
; Compatible with Windows 10 and 11 primarily
MinVersion=10.0.17763

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startupicon"; Description: "Start {#MyAppName} when Windows starts"; GroupDescription: "Startup options:"; Flags: unchecked

[Files]
; Application executable and necessary DLLs
Source: "..\Builds\VisualStudio2022\x64\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Builds\VisualStudio2022\x64\Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\license"; DestDir: "{app}"; DestName: "license.txt"; Flags: ignoreversion
Source: "..\gpl.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\NovaHost\README.md"; DestDir: "{app}"; DestName: "readme.txt"; Flags: ignoreversion isreadme

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{commonstartup}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startupicon; Check: not IsAdminInstallMode
Name: "{userstartup}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startupicon; Check: IsAdminInstallMode

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Registry]
; Store app settings in HKCU
Root: HKCU; Subkey: "Software\{#MyAppPublisher}\{#MyAppName}"; Flags: uninsdeletekey
; Register application to run at startup if selected
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: startupicon
; Associate with audio plugin files for easy loading
Root: HKCR; Subkey: ".vst3\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.vst3"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.vst3"; ValueType: string; ValueData: "VST3 Plugin"; Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.vst3\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.vst3\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletevalue
; Add VST2 association
Root: HKCR; Subkey: ".dll\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppName}.vst"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.vst"; ValueType: string; ValueData: "VST Plugin"; Flags: uninsdeletekey
Root: HKCR; Subkey: "{#MyAppName}.vst\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "{#MyAppName}.vst\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletevalue

[Code]
// Add a page to select default plugin directories with improved UI
var
  PluginDirsPage: TInputDirWizardPage;

procedure InitializeWizard;
begin
  // Create a page with VST directory selection
  PluginDirsPage := CreateInputDirPage(wpSelectDir,
    'Plugin Directories', 'Where are your audio plugins located?',
    'Select folders where your audio plugins are located. NovaHost will scan these directories ' +
    'when searching for plugins.' + #13#10 + #13#10 +
    'If you don''t know where your plugins are stored, you can leave these fields empty and configure later.',
    False, '');
    
  // Add entries for different plugin formats
  PluginDirsPage.Add('VST2 Plugins Directory');
  PluginDirsPage.Add('VST3 Plugins Directory');
  PluginDirsPage.Add('Additional Plugins Directory (optional)');
  
  // Set default values based on standard locations
  PluginDirsPage.Values[0] := ExpandConstant('{commonpf}\VSTPlugins');
  PluginDirsPage.Values[1] := ExpandConstant('{commonpf}\Common Files\VST3');
  PluginDirsPage.Values[2] := '';
end;

// Store plugin paths in registry for the app to use
procedure RegisterPaths;
var
  Paths: String;
begin
  Paths := '';
  
  // Build pipe-separated list of valid paths
  if PluginDirsPage.Values[0] <> '' then
    Paths := PluginDirsPage.Values[0];
  
  if PluginDirsPage.Values[1] <> '' then
  begin
    if Paths <> '' then
      Paths := Paths + '|' + PluginDirsPage.Values[1]
    else
      Paths := PluginDirsPage.Values[1];
  end;
  
  if PluginDirsPage.Values[2] <> '' then
  begin
    if Paths <> '' then
      Paths := Paths + '|' + PluginDirsPage.Values[2]
    else
      Paths := PluginDirsPage.Values[2];
  end;
  
  if Paths <> '' then
    RegWriteStringValue(HKCU, 'Software\{#MyAppPublisher}\{#MyAppName}', 'pluginSearchPaths', Paths);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    RegisterPaths;
end;