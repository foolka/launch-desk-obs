#ifndef AppVersion
  #error AppVersion is required
#endif
[Setup]
AppId={{53EA8D21-730D-4A2A-A710-BBF7ECA2A0D6}
AppName=Launch Desk OBS
AppVersion={#AppVersion}
AppPublisher=FermionaPlay
AppPublisherURL=https://fermionaplay.win
AppSupportURL=https://github.com/foolka/launch-desk-obs/issues
AppUpdatesURL=https://github.com/foolka/launch-desk-obs/releases
#ifdef TestRoot
DefaultDirName={#TestRoot}\plugin
PrivilegesRequired=lowest
CreateUninstallRegKey=no
#else
DefaultDirName={commonappdata}\obs-studio\plugins\launch-desk-obs
PrivilegesRequired=admin
#endif
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
WizardStyle=modern
LicenseFile={#PackageDir}\LICENSE
UninstallFilesDir={app}\uninstall
OutputDir={#OutputDir}
OutputBaseFilename=launch-desk-obs-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
SetupMutex=LaunchDeskOBS.Setup
VersionInfoVersion={#AppVersion}.0

[Languages]
Name: en; MessagesFile: compiler:Default.isl
Name: ru; MessagesFile: compiler:Languages\Russian.isl
Name: uk; MessagesFile: compiler:Languages\Ukrainian.isl

[Files]
Source: "{#PackageDir}\launch-desk-obs\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\README*.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#PackageDir}\LICENSE"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#PackageDir}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[CustomMessages]
en.ObsPath=Select the OBS Studio installation folder (not portable OBS).
ru.ObsPath=Выберите папку установки OBS Studio (не portable).
uk.ObsPath=Виберіть папку встановлення OBS Studio (не portable).
en.ObsInvalid=OBS Studio 32+ with Qt 6.8+ is required. For portable OBS use the ZIP package.
ru.ObsInvalid=Нужен OBS Studio 32+ с Qt 6.8+. Для portable OBS используйте ZIP.
uk.ObsInvalid=Потрібен OBS Studio 32+ з Qt 6.8+. Для portable OBS використовуйте ZIP.
en.CloseObs=Close OBS first. Setup will not stop streaming or close any applications.
ru.CloseObs=Сначала закройте OBS. Установщик не останавливает эфир и не закрывает приложения.
uk.CloseObs=Спочатку закрийте OBS. Інсталятор не зупиняє ефір і не закриває програми.
en.CheckFailed=Cannot check whether OBS is running. Installation has not started.
ru.CheckFailed=Не удалось проверить, запущен ли OBS. Установка не началась.
uk.CheckFailed=Не вдалося перевірити, чи працює OBS. Встановлення не почалося.
en.TargetInvalid=The standard plugin installation folder is required.
ru.TargetInvalid=Необходимо использовать стандартную папку установки плагина.
uk.TargetInvalid=Потрібно використовувати стандартну папку встановлення плагіна.

[Messages]
en.FinishedLabel=Launch Desk OBS is installed.%n%nOpen OBS > Docks > Launch Desk. Check the paths and enable Start with OBS for each app.%n%nReward Desk and Shoutout Desk profiles have not been changed.
ru.FinishedLabel=Launch Desk OBS установлен.%n%nОткройте OBS > Док-панели > Launch Desk. Проверьте пути и включите «Запускать вместе с OBS» для нужных приложений.%n%nПрофили Reward Desk и Shoutout Desk не изменены.
uk.FinishedLabel=Launch Desk OBS установлено.%n%nВідкрийте OBS > Док-панелі > Launch Desk. Перевірте шляхи й увімкніть «Запускати разом з OBS» для потрібних програм.%n%nПрофілі Reward Desk і Shoutout Desk не змінено.

[Code]
var
  ObsPage: TInputDirWizardPage;

function RunningProblem: String;
var
  Locator, Services, Processes: Variant;
begin
  Result := '';
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Services := Locator.ConnectServer('', 'root\CIMV2');
    Processes := Services.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
    if Processes.Count > 0 then Result := CustomMessage('CloseObs');
  except
    Result := CustomMessage('CheckFailed');
  end;
end;

function ObsProblem: String;
var
  Root: String;
  MS, LS: Cardinal;
begin
  Result := CustomMessage('ObsInvalid');
  Root := AddBackslash(ObsPage.Values[0]);
  if not GetVersionNumbers(Root + 'bin\64bit\obs64.exe', MS, LS) then Exit;
  if (MS shr 16) < 32 then Exit;
  if not GetVersionNumbers(Root + 'bin\64bit\Qt6Core.dll', MS, LS) then Exit;
  if ((MS shr 16) <> 6) or ((MS and $FFFF) < 8) then Exit;
  if FileExists(Root + 'portable_mode') or FileExists(Root + 'portable_mode.txt') or
    FileExists(Root + 'bin\64bit\portable_mode.txt') then Exit;
  Result := '';
end;

procedure InitializeWizard;
var
  Root: String;
begin
  ObsPage := CreateInputDirPage(wpLicense, 'OBS Studio', CustomMessage('ObsPath'), '', False, '');
  ObsPage.Add('OBS Studio:');
  Root := ExpandConstant('{param:OBSROOT|}');
  if Root = '' then RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Root);
  if Root = '' then Root := ExpandConstant('{autopf}\obs-studio');
  ObsPage.Values[0] := Root;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Target: String;
begin
#ifdef TestRoot
  Target := ExpandFileName('{#TestRoot}\plugin');
#else
  Target := ExpandConstant('{commonappdata}\obs-studio\plugins\launch-desk-obs');
#endif
  Result := CustomMessage('TargetInvalid');
  if CompareText(RemoveBackslashUnlessRoot(ExpandFileName(WizardDirValue)), RemoveBackslashUnlessRoot(Target)) <> 0 then Exit;
  Result := ObsProblem;
  if Result = '' then Result := RunningProblem;
end;

function InitializeUninstall: Boolean;
var
  Problem: String;
begin
  Problem := RunningProblem;
  Result := Problem = '';
  if not Result then SuppressibleMsgBox(Problem, mbInformation, MB_OK, IDOK);
end;
