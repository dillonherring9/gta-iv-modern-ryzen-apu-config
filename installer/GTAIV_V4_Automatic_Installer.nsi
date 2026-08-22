Unicode True
!include "FileFunc.nsh"
!include "nsDialogs.nsh"

RequestExecutionLevel admin
SetCompressor /SOLID lzma

!ifndef PRODUCT_VERSION
!define PRODUCT_VERSION "4"
!endif
!define PRODUCT_NAME "GTA IV After Dark Modern APU Configuration"
!define PRODUCT_PUBLISHER "LOST_MAN93"
!define UNINSTALLER_NAME "GTAIV_After_Dark_Configuration_V4_Uninstall.exe"
!define BACKUP_ROOT "GTAIV_After_Dark_Configuration_V4_Backups"
!define REFERENCE_ROOT "GTAIV_After_Dark_Complete_Package_V4"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\GTAIV_After_Dark_Configuration_V4"
!define APP_COMPAT_KEY "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "build\GTAIV_After_Dark_Complete_Configuration_v${PRODUCT_VERSION}.exe"
InstallDir ""
ShowInstDetails show
ShowUnInstDetails show

Page directory "" "" VerifyGameDirectory
Page custom ProfilePage ProfilePageLeave
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Var BackupDir
Var BackupRoot
Var ReferenceRoot
Var Year
Var Month
Var Day
Var Weekday
Var Hour
Var Minute
Var Second
Var Handle
Var LastBackup
Var ProcessOutput
Var PriorCompatFlags
Var V4Profile
Var V4ProfileGeneralRadio
Var V4ProfileRadeonRadio
Var V4ProfilePageHandle

Function .onInit
  SetShellVarContext current
  StrCpy $V4Profile "general"
FunctionEnd

Function ProfilePage
  nsDialogs::Create 1018
  Pop $V4ProfilePageHandle
  ${If} $V4ProfilePageHandle == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 24u "Choose a Version 4 renderer configuration. Both choices install the same seven Drag-and-Drop Archive configuration files, backup/rollback behavior, and GTAIV.exe administrator compatibility. This choice changes only the supplied dxvk.conf."
  Pop $0
  ${NSD_CreateRadioButton} 0 34u 100% 12u "General modern APU (recommended) — upstream-compatible baseline for unknown, stock DXVK, DXVK-Sarek, or another non-GPLAsync renderer."
  Pop $V4ProfileGeneralRadio
  ${NSD_OnClick} $V4ProfileGeneralRadio SelectGeneralProfile
  ${NSD_CreateRadioButton} 0 60u 100% 12u "Radeon 660M / GPLAsync v3.0-1 — choose only if GTAIV_d3d9.log already confirms that exact separate GPLAsync renderer."
  Pop $V4ProfileRadeonRadio
  ${NSD_OnClick} $V4ProfileRadeonRadio SelectRadeonProfile
  ${NSD_CreateLabel} 0 86u 100% 30u "Version 4 never replaces vulkan.dll. The Radeon option enables only dxvk.enableAsync and deliberately excludes the removed dxvk.gplAsyncCache key and older undocumented async-thread setting."
  Pop $0
  ${NSD_Check} $V4ProfileGeneralRadio
  nsDialogs::Show
FunctionEnd

Function SelectGeneralProfile
  StrCpy $V4Profile "general"
FunctionEnd

Function SelectRadeonProfile
  StrCpy $V4Profile "radeon660m"
FunctionEnd

Function ProfilePageLeave
  StrCmp $V4Profile "radeon660m" 0 profileAccepted
  MessageBox MB_ICONEXCLAMATION|MB_YESNO "Choose Radeon 660M / GPLAsync only if you already checked GTAIV_d3d9.log and confirmed the separate GPLAsync v3.0-1 renderer. This installer will not install or update vulkan.dll. Continue with this advanced profile?" IDYES profileAccepted
  Abort
profileAccepted:
FunctionEnd

Function VerifyGameDirectory
  IfFileExists "$INSTDIR\GTAIV.exe" gameRoot
  IfFileExists "$INSTDIR\GTA IV\GTAIV.exe" useSpacedGtaSubfolder
  IfFileExists "$INSTDIR\Grand Theft Auto IV\GTAIV.exe" useGrandTheftAutoSubfolder
  IfFileExists "$INSTDIR\GTAIV\GTAIV.exe" useCompactGtaSubfolder
  IfFileExists "$INSTDIR\GTA-IV\GTAIV.exe" useDashedGtaSubfolder
  MessageBox MB_ICONSTOP "The selected folder does not contain GTAIV.exe or a supported nested GTA IV game folder.$\r$\n$\r$\nSelected folder:$\r$\n$INSTDIR$\r$\n$\r$\nChoose the actual GTA IV Complete Edition game root. No files were changed."
  Abort
useSpacedGtaSubfolder:
  StrCpy $INSTDIR "$INSTDIR\GTA IV"
  Goto gameRoot
useGrandTheftAutoSubfolder:
  StrCpy $INSTDIR "$INSTDIR\Grand Theft Auto IV"
  Goto gameRoot
useCompactGtaSubfolder:
  StrCpy $INSTDIR "$INSTDIR\GTAIV"
  Goto gameRoot
useDashedGtaSubfolder:
  StrCpy $INSTDIR "$INSTDIR\GTA-IV"
gameRoot:
  ReadRegStr $PriorCompatFlags HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe"
FunctionEnd

; The full Drag-and-Drop Archive supplies FusionFix and its surrounding mod layout. Do not infer
; archive health from one ASI filename: valid loaders/layouts differ. The Vulkan route remains the
; required renderer boundary for the supplied FusionFix profile.
Function VerifyRendererRoute
  IfFileExists "$INSTDIR\vulkan.dll" rendererFound
  MessageBox MB_ICONSTOP "vulkan.dll was not found in the game root.$\r$\n$\r$\nInstall the complete Drag-and-Drop Archive and its Vulkan route first. This package supplies configuration only; it never redistributes renderer binaries. No files were changed."
  Abort
rendererFound:
FunctionEnd

Function VerifyGameClosed
  nsExec::ExecToStack '"$SYSDIR\tasklist.exe" /FI "$\"IMAGENAME eq GTAIV.exe$\"" /FO CSV /NH'
  Pop $0
  Pop $ProcessOutput
  StrCpy $0 $ProcessOutput 9 1
  StrCmp $0 "GTAIV.exe" gameIsRunning gameIsClosed
gameIsRunning:
  MessageBox MB_ICONSTOP "GTAIV.exe is running. Close GTA IV, the Rockstar Games Launcher, mod managers, and tools that may hold these files open; then run this installer again. No files were changed."
  Abort
gameIsClosed:
FunctionEnd

Function CreateBackup
  ${GetTime} "" "L" $Year $Month $Day $Weekday $Hour $Minute $Second
  StrCpy $BackupRoot "$INSTDIR\${BACKUP_ROOT}"
  StrCpy $BackupDir "$BackupRoot\$Year-$Month-$Day_$Hour-$Minute-$Second"
  CreateDirectory "$BackupDir"
  CreateDirectory "$BackupDir\pc"
  CreateDirectory "$BackupDir\plugins"

  ; Preserve every configuration file this full installer manages. Missing backup files mean
  ; this installer created the corresponding target and uninstall will remove it on restore.
  IfFileExists "$INSTDIR\dxvk.conf" 0 +2
    CopyFiles /SILENT "$INSTDIR\dxvk.conf" "$BackupDir"
  IfFileExists "$INSTDIR\pc\stream.ini" 0 +2
    CopyFiles /SILENT "$INSTDIR\pc\stream.ini" "$BackupDir\pc"
  IfFileExists "$INSTDIR\plugins\ConsoleSelectMenuIV.ini" 0 +2
    CopyFiles /SILENT "$INSTDIR\plugins\ConsoleSelectMenuIV.ini" "$BackupDir\plugins"
  IfFileExists "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.cfg" 0 +2
    CopyFiles /SILENT "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.cfg" "$BackupDir\plugins"
  IfFileExists "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.ini" 0 +2
    CopyFiles /SILENT "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.ini" "$BackupDir\plugins"
  IfFileExists "$INSTDIR\plugins\GTAIV.XboxRainDroplets.ini" 0 +2
    CopyFiles /SILENT "$INSTDIR\plugins\GTAIV.XboxRainDroplets.ini" "$BackupDir\plugins"
  IfFileExists "$INSTDIR\plugins\LibertyCityPlates.txt" 0 +2
    CopyFiles /SILENT "$INSTDIR\plugins\LibertyCityPlates.txt" "$BackupDir\plugins"

  FileOpen $Handle "$BackupDir\backup_manifest.txt" w
  FileWrite $Handle "GTA IV After Dark Complete Configuration backup$\r$\n"
  FileWrite $Handle "Installation directory: $INSTDIR$\r$\n"
  FileWrite $Handle "Created: $Year-$Month-$Day $Hour:$Minute:$Second$\r$\n"
  FileWrite $Handle "Managed files: dxvk.conf, pc\stream.ini, plugins\ConsoleSelectMenuIV.ini, plugins\GTAIV.EFLC.FusionFix.cfg, plugins\GTAIV.EFLC.FusionFix.ini, plugins\GTAIV.XboxRainDroplets.ini, plugins\LibertyCityPlates.txt$\r$\n"
  FileWrite $Handle "Selected Version 4 renderer profile: $V4Profile$\r$\n"
  FileWrite $Handle "Previous GTAIV.exe compatibility flags: $PriorCompatFlags$\r$\n"
  FileClose $Handle
  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Installer" "BackupPath" "$BackupDir"
  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Installer" "PreviousCompatFlags" "$PriorCompatFlags"
  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Installer" "SelectedProfile" "$V4Profile"
FunctionEnd

Function InstallReferencePackage
  StrCpy $ReferenceRoot "$INSTDIR\${REFERENCE_ROOT}"
  SetOutPath "$ReferenceRoot"
  File /r "payload\reference\*.*"
FunctionEnd

Section "Install complete Version ${PRODUCT_VERSION} Drag-and-Drop Archive configuration" SEC_MAIN
  Call VerifyRendererRoute
  Call VerifyGameClosed
  Call CreateBackup

  SetOutPath "$INSTDIR"
  StrCmp $V4Profile "radeon660m" installRadeonDxvk installGeneralDxvk
installGeneralDxvk:
  File /oname=dxvk.conf "payload\root\general\dxvk.conf"
  Goto dxvkInstalled
installRadeonDxvk:
  File /oname=dxvk.conf "payload\root\radeon660m\dxvk.conf"
dxvkInstalled:
  SetOutPath "$INSTDIR\pc"
  File /oname=stream.ini "payload\pc\stream.ini"
  SetOutPath "$INSTDIR\plugins"
  File /oname=ConsoleSelectMenuIV.ini "payload\optional\components\ConsoleSelectMenuIV.ini"
  File /oname=GTAIV.EFLC.FusionFix.cfg "payload\plugins\GTAIV.EFLC.FusionFix.cfg"
  File /oname=GTAIV.EFLC.FusionFix.ini "payload\plugins\GTAIV.EFLC.FusionFix.ini"
  File /oname=GTAIV.XboxRainDroplets.ini "payload\optional\components\GTAIV.XboxRainDroplets.ini"
  File /oname=LibertyCityPlates.txt "payload\optional\components\LibertyCityPlates.txt"

  ; GTA IV must create configuration files in this documented layout. Preserve any existing
  ; per-user flags and add RUNASADMIN for GTAIV.exe on every complete-package installation.
  StrCmp $PriorCompatFlags "" setAdminOnly setAdminPreservingPriorValue
setAdminOnly:
  WriteRegStr HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe" "RUNASADMIN"
  Goto compatibilityWritten
setAdminPreservingPriorValue:
  WriteRegStr HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe" "$PriorCompatFlags RUNASADMIN"
compatibilityWritten:

  Call InstallReferencePackage
  WriteUninstaller "$INSTDIR\${UNINSTALLER_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME} ${PRODUCT_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${UNINSTALLER_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\${UNINSTALLER_NAME}$\""
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" "$\"$INSTDIR\${UNINSTALLER_NAME}$\" /S"
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1

  IfSilent +2
    MessageBox MB_ICONINFORMATION "Version ${PRODUCT_VERSION} installed the complete Drag-and-Drop Archive configuration: dxvk.conf, stream.ini, Console Select Menu, FusionFix, Xbox Rain Droplets, and LibertyCityPlates settings.$\r$\n$\r$\nSelected renderer configuration: $V4Profile. Version 4 did not replace vulkan.dll.$\r$\n$\r$\nGTAIV.exe was set to run as administrator so it can create required configuration files. The prior files and compatibility value were backed up in:$\r$\n$BackupDir$\r$\n$\r$\nThe complete documentation, sources, credits, renderer profiles, audit adoption record, and history are available at:$\r$\n$ReferenceRoot"
SectionEnd

Section "Uninstall"
  StrCpy $BackupRoot "$INSTDIR\${BACKUP_ROOT}"
  ClearErrors
  ReadINIStr $LastBackup "$BackupRoot\LAST_BACKUP.ini" "Installer" "BackupPath"
  ReadINIStr $PriorCompatFlags "$BackupRoot\LAST_BACKUP.ini" "Installer" "PreviousCompatFlags"
  StrCmp $LastBackup "" noBackup

  IfSilent restoreFiles
  MessageBox MB_ICONEXCLAMATION|MB_YESNO "Restore the latest complete-configuration backup from:$\r$\n$LastBackup$\r$\n$\r$\nChoose Yes only if you want to replace the current managed configuration with that backup." IDYES restoreFiles IDNO finishUninstall

restoreFiles:
  IfFileExists "$LastBackup\dxvk.conf" 0 +3
    CopyFiles /SILENT "$LastBackup\dxvk.conf" "$INSTDIR"
    Goto streamRestore
  Delete "$INSTDIR\dxvk.conf"
streamRestore:
  IfFileExists "$LastBackup\pc\stream.ini" 0 +3
    CopyFiles /SILENT "$LastBackup\pc\stream.ini" "$INSTDIR\pc"
    Goto consoleRestore
  Delete "$INSTDIR\pc\stream.ini"
consoleRestore:
  IfFileExists "$LastBackup\plugins\ConsoleSelectMenuIV.ini" 0 +3
    CopyFiles /SILENT "$LastBackup\plugins\ConsoleSelectMenuIV.ini" "$INSTDIR\plugins"
    Goto cfgRestore
  Delete "$INSTDIR\plugins\ConsoleSelectMenuIV.ini"
cfgRestore:
  IfFileExists "$LastBackup\plugins\GTAIV.EFLC.FusionFix.cfg" 0 +3
    CopyFiles /SILENT "$LastBackup\plugins\GTAIV.EFLC.FusionFix.cfg" "$INSTDIR\plugins"
    Goto iniRestore
  Delete "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.cfg"
iniRestore:
  IfFileExists "$LastBackup\plugins\GTAIV.EFLC.FusionFix.ini" 0 +3
    CopyFiles /SILENT "$LastBackup\plugins\GTAIV.EFLC.FusionFix.ini" "$INSTDIR\plugins"
    Goto rainRestore
  Delete "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.ini"
rainRestore:
  IfFileExists "$LastBackup\plugins\GTAIV.XboxRainDroplets.ini" 0 +3
    CopyFiles /SILENT "$LastBackup\plugins\GTAIV.XboxRainDroplets.ini" "$INSTDIR\plugins"
    Goto platesRestore
  Delete "$INSTDIR\plugins\GTAIV.XboxRainDroplets.ini"
platesRestore:
  IfFileExists "$LastBackup\plugins\LibertyCityPlates.txt" 0 +3
    CopyFiles /SILENT "$LastBackup\plugins\LibertyCityPlates.txt" "$INSTDIR\plugins"
    Goto finishUninstall
  Delete "$INSTDIR\plugins\LibertyCityPlates.txt"

noBackup:
  IfSilent finishUninstall
  MessageBox MB_ICONEXCLAMATION "No complete-configuration backup record was found. The managed configuration files were left in place."

finishUninstall:
  StrCmp $PriorCompatFlags "" removeManagedCompatibility
  WriteRegStr HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe" "$PriorCompatFlags"
  Goto compatibilityRestored
removeManagedCompatibility:
  DeleteRegValue HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe"
compatibilityRestored:
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  Delete "$INSTDIR\${UNINSTALLER_NAME}"
  ; The complete reference package is intentionally retained for auditability, credits, and recovery.
SectionEnd
