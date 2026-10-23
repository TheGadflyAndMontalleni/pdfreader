; Build with tools/make_installer.ps1. Test mode changes only installation
; identity and registry/shortcut destinations, preserving the install logic.
#ifndef ProjectRoot
  #error ProjectRoot is required
#endif
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef AppVersion
  #define AppVersion "0.4.0"
#endif
#ifdef InstallerTest
  #define ProductId "TheGadflyAndMontalleni.PDFReader.InstallerTest"
  #define ProductName "PDF Reader Installer Test"
  #define ClassesRoot "Software\PDFReaderInstallerTest\Classes"
  #define CapabilityRoot "Software\PDFReaderInstallerTest\Capabilities"
  #define RegisteredRoot "Software\PDFReaderInstallerTest\RegisteredApplications"
  #define StartShortcut "{app}\test-shortcuts\PDF Reader"
  #define DesktopShortcut "{app}\test-shortcuts\PDF Reader Desktop"
  #define OutputName "PDFReader-Setup-Test"
#else
  #define ProductId "TheGadflyAndMontalleni.PDFReader"
  #define ProductName "PDF Reader"
  #define ClassesRoot "Software\Classes"
  #define CapabilityRoot "Software\PDFReader\Capabilities"
  #define RegisteredRoot "Software\RegisteredApplications"
  #define StartShortcut "{userprograms}\PDF Reader"
  #define DesktopShortcut "{userdesktop}\PDF Reader"
  #define OutputName "PDFReader-Setup"
#endif

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVersion={#AppVersion}
AppVerName={#ProductName} {#AppVersion}
AppPublisher=TheGadflyAndMontalleni
AppPublisherURL=https://github.com/TheGadflyAndMontalleni/pdfreader
AppSupportURL=https://github.com/TheGadflyAndMontalleni/pdfreader/issues
AppUpdatesURL=https://github.com/TheGadflyAndMontalleni/pdfreader/releases
DefaultDirName={localappdata}\Programs\PDFReader
DefaultGroupName=PDF Reader
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#ProjectRoot}\dist
OutputBaseFilename={#OutputName}
SetupIconFile={#ProjectRoot}\src\app\app.ico
UninstallDisplayIcon={app}\pdfreader.exe
UninstallFilesDir={app}\uninstall
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
DisableStartupPrompt=yes
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableFinishedPage=yes
ShowLanguageDialog=no
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0

[Languages]
Name: "chinesesimp"; MessagesFile: "{#ProjectRoot}\tools\installer\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
chinesesimp.ButtonInstall=一键安装(&I)
chinesesimp.ReadyLabel1=点击“一键安装”，即可在当前账户安装 PDF Reader。安装完成后会自动打开阅读器。
chinesesimp.ReadyLabel2a=将创建快捷方式并添加 PDF / EPUB“打开方式”。缺少 EPUB 运行组件时将联网安装 Microsoft WebView2。

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#WebViewSetup}"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: NeedsWebView2

[Icons]
Name: "{#StartShortcut}"; Filename: "{app}\pdfreader.exe"; WorkingDir: "{app}"; Comment: "PDF Reader"
Name: "{#DesktopShortcut}"; Filename: "{app}\pdfreader.exe"; WorkingDir: "{app}"; Comment: "PDF Reader"

[Registry]
Root: HKCU; Subkey: "{#ClassesRoot}\PDFReader.pdf"; ValueType: string; ValueName: ""; ValueData: "PDF 文档"; Flags: uninsdeletekey
Root: HKCU; Subkey: "{#ClassesRoot}\PDFReader.pdf\DefaultIcon"; ValueType: string; ValueData: """{app}\pdfreader.exe"",0"
Root: HKCU; Subkey: "{#ClassesRoot}\PDFReader.pdf\shell\open\command"; ValueType: string; ValueData: """{app}\pdfreader.exe"" ""%1"""
Root: HKCU; Subkey: "{#ClassesRoot}\.pdf\OpenWithProgids"; ValueType: string; ValueName: "PDFReader.pdf"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "{#ClassesRoot}\PDFReader.epub"; ValueType: string; ValueName: ""; ValueData: "EPUB 电子书"; Flags: uninsdeletekey
Root: HKCU; Subkey: "{#ClassesRoot}\PDFReader.epub\DefaultIcon"; ValueType: string; ValueData: """{app}\pdfreader.exe"",0"
Root: HKCU; Subkey: "{#ClassesRoot}\PDFReader.epub\shell\open\command"; ValueType: string; ValueData: """{app}\pdfreader.exe"" ""%1"""
Root: HKCU; Subkey: "{#ClassesRoot}\.epub\OpenWithProgids"; ValueType: string; ValueName: "PDFReader.epub"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "{#ClassesRoot}\Applications\pdfreader.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "PDF Reader"; Flags: uninsdeletekey
Root: HKCU; Subkey: "{#ClassesRoot}\Applications\pdfreader.exe\DefaultIcon"; ValueType: string; ValueData: """{app}\pdfreader.exe"",0"
Root: HKCU; Subkey: "{#ClassesRoot}\Applications\pdfreader.exe\shell\open\command"; ValueType: string; ValueData: """{app}\pdfreader.exe"" ""%1"""
Root: HKCU; Subkey: "{#ClassesRoot}\Applications\pdfreader.exe\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""
Root: HKCU; Subkey: "{#ClassesRoot}\Applications\pdfreader.exe\SupportedTypes"; ValueType: string; ValueName: ".epub"; ValueData: ""
Root: HKCU; Subkey: "{#CapabilityRoot}"; ValueType: string; ValueName: "ApplicationName"; ValueData: "PDF Reader"; Flags: uninsdeletekey
Root: HKCU; Subkey: "{#CapabilityRoot}"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "支持高清 PDF、EPUB 重排、目录、书签和 PDF 批注的阅读器"
Root: HKCU; Subkey: "{#CapabilityRoot}\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "PDFReader.pdf"
Root: HKCU; Subkey: "{#CapabilityRoot}\FileAssociations"; ValueType: string; ValueName: ".epub"; ValueData: "PDFReader.epub"
Root: HKCU; Subkey: "{#RegisteredRoot}"; ValueType: string; ValueName: "PDFReader"; ValueData: "{#CapabilityRoot}"; Flags: uninsdeletevalue

[Run]
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; Check: NeedsWebView2; Flags: runhidden waituntilterminated
Filename: "{app}\pdfreader.exe"; WorkingDir: "{app}"; Flags: nowait skipifsilent

[Code]
function NeedsWebView2: Boolean;
var
  Version: String;
  RuntimeKey: String;
begin
#ifdef InstallerTest
  { Isolated packaging tests never install a system browser component. }
  Result := False;
#else
  RuntimeKey := 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  Result := not ((RegQueryStringValue(HKLM32, RuntimeKey, 'pv', Version) or
    RegQueryStringValue(HKCU, RuntimeKey, 'pv', Version)) and
    (StrToIntDef(Copy(Version, 1, Pos('.', Version) - 1), 0) >= 120));
#endif
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  PreviousDirectory: String;
begin
#ifndef InstallerTest
  { Migrate the older PowerShell install only when it owns this exact path. }
  if CurStep = ssPostInstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\PDFReader',
      'InstallLocation', PreviousDirectory) then
      if CompareText(RemoveBackslashUnlessRoot(PreviousDirectory),
        RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) = 0 then
        RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\PDFReader');
#endif
end;
