;
; Copyright (c) 2014 D. Richard Hipp
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the Simplified BSD License (also
; known as the "2-Clause License" or "FreeBSD License".)
;
; This program is distributed in the hope that it will be useful,
; but without any warranty; without even the implied warranty of
; merchantability or fitness for a particular purpose.
;
; Author contact information:
;   drh@hwaci.com
;   http://www.hwaci.com/drh/
;

[Setup]
ArchitecturesAllowed=x86 x64
AlwaysShowComponentsList=false
AppCopyright=Copyright (c) D. Richard Hipp.  All rights reserved.
AppID={{f1c25a1f-3954-4e1a-ac36-4314c52f057c}
AppName=Fossil
AppPublisher=Fossil Development Team
AppPublisherURL=http://www.fossil-scm.org/
AppSupportURL=http://www.fossil-scm.org/
AppUpdatesURL=http://www.fossil-scm.org/
AppVerName=Fossil v{#AppVersion}
AppVersion={#AppVersion}
AppComments=Simple, high-reliability, distributed software configuration management system.
AppReadmeFile=http://www.fossil-scm.org/index.html/doc/tip/www/quickstart.wiki
DefaultDirName={pf}\Fossil
DefaultGroupName=Fossil
OutputBaseFilename=fossil-win32-{#AppVersion}
OutputManifestFile=fossil-win32-{#AppVersion}-manifest.txt
SetupLogging=true
UninstallFilesDir={app}\uninstall
VersionInfoVersion={#AppVersion}

[Components]
Name: Application; Description: Core application.; Types: custom compact full; Flags: fixed

[Dirs]
Name: {app}\bin

[Files]
Components: Application; Source: ..\fossil.exe; DestDir: {app}\bin; Flags: restartreplace uninsrestartdelete

[Registry]
Components: Application; Root: HKLM32; SubKey: Software\Fossil; ValueType: string; ValueName: Install_Dir; ValueData: {app}; Flags: uninsdeletekeyifempty uninsdeletevalue
