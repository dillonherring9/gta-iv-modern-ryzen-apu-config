Unicode True
!include "FileFunc.nsh"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define PRODUCT_NAME "GTA IV Version 3 Automatic Installer"
!define PRODUCT_VERSION "3.0.0"
!define PRODUCT_PUBLISHER "LOST_MAN93"
!define UNINSTALLER_NAME "GTAIV_V3_Automatic_Installer_Uninstall.exe"
!define BACKUP_ROOT "GTAIV_V3_Installer_Backups"
!define LAYERS_KEY "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\GTAIV_V3_Automatic_Installer"
!define V2_UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\GTAIV_V2_Automatic_Installer"
!define V2_UNINSTALLER_NAME "GTAIV_V2_Automatic_Installer_Uninstall.exe"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "build\GTAIV_V3_Automatic_Installer_v3.0.0.exe"
; Do not import Rockstar's InstallFolder registry value: it can be stale or contain duplicated folders.
; The tester selects the actual game folder once through Browse.
InstallDir ""
ShowInstDetails show
ShowUnInstDetails show

; The directory page is intentionally non-blocking. Verification happens only after Install is clicked.
Page directory "" "" VerifyGameDirectory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Var BackupDir
Var BackupRoot
Var Year
Var Month
Var Day
Var Weekday
Var Hour
Var Minute
Var Second
Var PreviousCompat
Var Handle
Var LastBackup

Function .onInit
  SetShellVarContext current
FunctionEnd

Function VerifyGameDirectory
  ; Accept the usual root layout first.
  IfFileExists "$INSTDIR\GTAIV.exe" validGame
  IfFileExists "$INSTDIR\PlayGTAIV.exe" validGame

  ; Also accept a parent folder that contains a common GTA IV game-folder name.
IfFileExists "$INSTDIR\GTA IV\GTAIV.exe" useSpacedGtaSubfolder
IfFileExists "$INSTDIR\Grand Theft Auto IV\GTAIV.exe" useGrandTheftAutoSubfolder
IfFileExists "$INSTDIR\GTAIV\GTAIV.exe" useCompactGtaSubfolder
IfFileExists "$INSTDIR\GTA-IV\GTAIV.exe" useDashedGtaSubfolder

  ; Do not trap the user with a disabled Install button. They can deliberately continue
  ; after checking the path if their legitimate installation uses a different layout.
  MessageBox MB_ICONEXCLAMATION|MB_YESNO "The installer could not automatically find GTAIV.exe in the selected folder or its common GTA IV subfolders.$\r$\n$\r$\nSelected folder:$\r$\n$INSTDIR$\r$\n$\r$\nChoose Yes only if this is the GTA IV game folder. Choose No to go back and select a different folder." IDYES validGame IDNO invalidGame

useSpacedGtaSubfolder:
  StrCpy $INSTDIR "$INSTDIR\GTA IV"
  Goto validGame
useGrandTheftAutoSubfolder:
  StrCpy $INSTDIR "$INSTDIR\Grand Theft Auto IV"
  Goto validGame
useCompactGtaSubfolder:
  StrCpy $INSTDIR "$INSTDIR\GTAIV"
  Goto validGame
useDashedGtaSubfolder:
  StrCpy $INSTDIR "$INSTDIR\GTA-IV"
  Goto validGame
invalidGame:
  Abort
validGame:
FunctionEnd

Function CreateBackup
  ${GetTime} "" "L" $Year $Month $Day $Weekday $Hour $Minute $Second
  StrCpy $BackupRoot "$INSTDIR\${BACKUP_ROOT}"
  StrCpy $BackupDir "$BackupRoot\$Year-$Month-$Day_$Hour-$Minute-$Second"
  CreateDirectory "$BackupDir"
  CreateDirectory "$BackupDir\pc"
  CreateDirectory "$BackupDir\plugins"
  CreateDirectory "$BackupDir\common\shaders"

  ; Preserve the prior per-user compatibility state before enabling RUNASADMIN.
  ; INI entries avoid text-line parsing ambiguity during uninstall restoration.
  ClearErrors
  ReadRegStr $PreviousCompat HKCU "${LAYERS_KEY}" "$INSTDIR\GTAIV.exe"
  IfErrors compatMissing
  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Compatibility" "Existed" "1"
  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Compatibility" "Value" "$PreviousCompat"
  Goto compatRecorded
compatMissing:
  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Compatibility" "Existed" "0"
compatRecorded:

  ; Back up only files that already exist. Missing backup files mean the installer created that target file.
  IfFileExists "$INSTDIR\dxvk.conf" 0 +2
    CopyFiles /SILENT "$INSTDIR\dxvk.conf" "$BackupDir"
  IfFileExists "$INSTDIR\pc\stream.ini" 0 +2
    CopyFiles /SILENT "$INSTDIR\pc\stream.ini" "$BackupDir\pc"
  IfFileExists "$INSTDIR\plugins\AudioMap.ini" 0 +2
    CopyFiles /SILENT "$INSTDIR\plugins\AudioMap.ini" "$BackupDir\plugins"
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
  IfFileExists "$INSTDIR\common\shaders\preload.list" 0 +2
    CopyFiles /SILENT "$INSTDIR\common\shaders\preload.list" "$BackupDir\common\shaders"

  FileOpen $Handle "$BackupDir\backup_manifest.txt" w
  FileWrite $Handle "GTA IV Version 3 Automatic Installer backup$\r$\n"
  FileWrite $Handle "Installation directory: $INSTDIR$\r$\n"
  FileWrite $Handle "Created: $Year-$Month-$Day $Hour:$Minute:$Second$\r$\n"
  FileWrite $Handle "The backup contains only pre-existing configuration files.$\r$\n"
  FileClose $Handle

  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Installer" "BackupPath" "$BackupDir"
FunctionEnd

Function SetRunAsAdmin
  ; Store the prior value in CreateBackup, then set the exact flag requested for this installer run.
  ; Uninstall restores the recorded prior value when one existed. Do not create a flag
  ; for a non-existent executable if the tester deliberately continued with an unusual layout.
  IfFileExists "$INSTDIR\GTAIV.exe" 0 +2
    WriteRegStr HKCU "${LAYERS_KEY}" "$INSTDIR\GTAIV.exe" "RUNASADMIN"
FunctionEnd

Section "Install GTA IV Version 3 configuration" SEC_MAIN
  Call CreateBackup

  SetOutPath "$INSTDIR"
  File /oname=dxvk.conf "payload\root\dxvk.conf"

  SetOutPath "$INSTDIR\pc"
  File /oname=stream.ini "payload\pc\stream.ini"

  SetOutPath "$INSTDIR\plugins"
  File /oname=AudioMap.ini "payload\plugins\AudioMap.ini"
  File /oname=ConsoleSelectMenuIV.ini "payload\plugins\ConsoleSelectMenuIV.ini"
  File /oname=GTAIV.EFLC.FusionFix.cfg "payload\plugins\GTAIV.EFLC.FusionFix.cfg"
  File /oname=GTAIV.EFLC.FusionFix.ini "payload\plugins\GTAIV.EFLC.FusionFix.ini"
  File /oname=GTAIV.XboxRainDroplets.ini "payload\plugins\GTAIV.XboxRainDroplets.ini"
  File /oname=LibertyCityPlates.txt "payload\plugins\LibertyCityPlates.txt"

  SetOutPath "$INSTDIR\common\shaders"
  File /oname=preload.list "payload\common\shaders\preload.list"

  Call SetRunAsAdmin
  WriteUninstaller "$INSTDIR\${UNINSTALLER_NAME}"

  ; This upgrade supersedes Version 2 only after the Version 3 uninstaller exists.
  DeleteRegKey HKLM "${V2_UNINSTALL_KEY}"
  Delete "$INSTDIR\${V2_UNINSTALLER_NAME}"

  ; Make the documented "Installed apps" route real and keep the uninstaller discoverable.
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
    MessageBox MB_ICONINFORMATION "Version 3 files were installed. The prior files, if any, were backed up in:$\r$\n$BackupDir$\r$\n$\r$\nThe log-validated DXVK GPLAsync profile is now in place. If GTAIV.exe is present in the selected game folder, it was configured to run as administrator for the current Windows user."
SectionEnd

Section "Uninstall"
  StrCpy $BackupRoot "$INSTDIR\${BACKUP_ROOT}"
  ClearErrors
  ReadINIStr $LastBackup "$BackupRoot\LAST_BACKUP.ini" "Installer" "BackupPath"
  StrCmp $LastBackup "" noBackup

  IfSilent restoreFiles
  MessageBox MB_ICONEXCLAMATION|MB_YESNO "Restore the latest backup from:$\r$\n$LastBackup$\r$\n$\r$\nChoose Yes only if you want to replace the current installed configuration with that backup." IDYES restoreFiles IDNO finishUninstall

restoreFiles:
  IfFileExists "$LastBackup\dxvk.conf" 0 +3
    CopyFiles /SILENT "$LastBackup\dxvk.conf" "$INSTDIR"
    Goto streamRestore
  Delete "$INSTDIR\dxvk.conf"
streamRestore:
  IfFileExists "$LastBackup\pc\stream.ini" 0 +3
    CopyFiles /SILENT "$LastBackup\pc\stream.ini" "$INSTDIR\pc"
    Goto audioMapRestore
  Delete "$INSTDIR\pc\stream.ini"
audioMapRestore:
  IfFileExists "$LastBackup\plugins\AudioMap.ini" 0 +3
    CopyFiles /SILENT "$LastBackup\plugins\AudioMap.ini" "$INSTDIR\plugins"
    Goto consoleRestore
  Delete "$INSTDIR\plugins\AudioMap.ini"
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
    Goto preloadRestore
  Delete "$INSTDIR\plugins\LibertyCityPlates.txt"
preloadRestore:
  IfFileExists "$LastBackup\common\shaders\preload.list" 0 +3
    CopyFiles /SILENT "$LastBackup\common\shaders\preload.list" "$INSTDIR\common\shaders"
    Goto compatibilityRestore
  Delete "$INSTDIR\common\shaders\preload.list"
compatibilityRestore:
  ClearErrors
  ReadINIStr $PreviousCompat "$BackupRoot\LAST_BACKUP.ini" "Compatibility" "Existed"
  StrCmp $PreviousCompat "1" restorePriorCompat clearCompat
restorePriorCompat:
  ReadINIStr $PreviousCompat "$BackupRoot\LAST_BACKUP.ini" "Compatibility" "Value"
  StrCmp $PreviousCompat "" clearCompat
  WriteRegStr HKCU "${LAYERS_KEY}" "$INSTDIR\GTAIV.exe" "$PreviousCompat"
  Goto finishUninstall
clearCompat:
  DeleteRegValue HKCU "${LAYERS_KEY}" "$INSTDIR\GTAIV.exe"
  Goto finishUninstall

noBackup:
  MessageBox MB_ICONEXCLAMATION "No installer backup record was found. The configuration files were left in place."
  Goto finishUninstall

finishUninstall:
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  Delete "$INSTDIR\${UNINSTALLER_NAME}"
SectionEnd
