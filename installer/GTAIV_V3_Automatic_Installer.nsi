Unicode True
!include "FileFunc.nsh"

RequestExecutionLevel admin
SetCompressor /SOLID lzma

!ifndef PRODUCT_VERSION
!define PRODUCT_VERSION "3"
!endif
!define PRODUCT_NAME "GTA IV After Dark Complete Configuration"
!define PRODUCT_PUBLISHER "LOST_MAN93"
!define UNINSTALLER_NAME "GTAIV_After_Dark_Configuration_Uninstall.exe"
!define BACKUP_ROOT "GTAIV_After_Dark_Configuration_Backups"
!define REFERENCE_ROOT "GTAIV_After_Dark_Complete_Package"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\GTAIV_After_Dark_Configuration"
!define APP_COMPAT_KEY "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "build\GTAIV_After_Dark_Complete_Configuration_v${PRODUCT_VERSION}.exe"
InstallDir ""
ShowInstDetails show
ShowUnInstDetails show

Page directory "" "" VerifyGameDirectory
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

Function .onInit
  SetShellVarContext current
FunctionEnd

; Resolve only a confirmed GTA IV Complete Edition game root. This installer never guesses a path.
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

; FusionFix and its Vulkan route are required because the supplied core profile selects that route.
Function VerifyPrerequisites
  IfFileExists "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.asi" fusionFixFound
  MessageBox MB_ICONSTOP "FusionFix was not found at:$\r$\n$INSTDIR\plugins\GTAIV.EFLC.FusionFix.asi$\r$\n$\r$\nInstall and launch a compatible FusionFix Complete Edition setup first. This installer does not ship, update, or repair third-party binaries. No files were changed."
  Abort
fusionFixFound:
  IfFileExists "$INSTDIR\vulkan.dll" rendererFound
  MessageBox MB_ICONSTOP "FusionFix is present, but vulkan.dll was not found in the game root.$\r$\n$\r$\nInstall the intended compatible Vulkan renderer first. This complete package includes stock-DXVK and GPLAsync configuration choices as reference files, but it never redistributes renderer binaries. No files were changed."
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

  ; Back up each managed core file only when it already exists. Optional files are carried in
  ; the embedded package but are never overwritten automatically because their dependencies vary.
  IfFileExists "$INSTDIR\pc\stream.ini" 0 +2
    CopyFiles /SILENT "$INSTDIR\pc\stream.ini" "$BackupDir\pc"
  IfFileExists "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.cfg" 0 +2
    CopyFiles /SILENT "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.cfg" "$BackupDir\plugins"
  IfFileExists "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.ini" 0 +2
    CopyFiles /SILENT "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.ini" "$BackupDir\plugins"

  FileOpen $Handle "$BackupDir\backup_manifest.txt" w
  FileWrite $Handle "GTA IV After Dark Complete Configuration backup$\r$\n"
  FileWrite $Handle "Installation directory: $INSTDIR$\r$\n"
  FileWrite $Handle "Created: $Year-$Month-$Day $Hour:$Minute:$Second$\r$\n"
  FileWrite $Handle "Managed core files: pc\stream.ini, plugins\GTAIV.EFLC.FusionFix.cfg, plugins\GTAIV.EFLC.FusionFix.ini$\r$\n"
  FileWrite $Handle "Optional renderer, plugin, and shader-layout files were embedded in the package but not auto-deployed.$\r$\n"
  FileWrite $Handle "Previous GTAIV.exe compatibility flags: $PriorCompatFlags$\r$\n"
  FileClose $Handle
  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Installer" "BackupPath" "$BackupDir"
  WriteINIStr "$BackupRoot\LAST_BACKUP.ini" "Installer" "PreviousCompatFlags" "$PriorCompatFlags"
FunctionEnd

Function InstallReferencePackage
  StrCpy $ReferenceRoot "$INSTDIR\${REFERENCE_ROOT}"
  SetOutPath "$ReferenceRoot"
  File /r "payload\reference\*.*"
FunctionEnd

Section "Install complete Version ${PRODUCT_VERSION} configuration package" SEC_MAIN
  Call VerifyPrerequisites
  Call VerifyGameClosed
  Call CreateBackup

  SetOutPath "$INSTDIR\pc"
  File /oname=stream.ini "payload\pc\stream.ini"

  SetOutPath "$INSTDIR\plugins"
  File /oname=GTAIV.EFLC.FusionFix.cfg "payload\plugins\GTAIV.EFLC.FusionFix.cfg"
  File /oname=GTAIV.EFLC.FusionFix.ini "payload\plugins\GTAIV.EFLC.FusionFix.ini"

  ; GTA IV must create configuration files in this documented layout. Preserve any existing
  ; per-user flags and add RUNASADMIN for GTAIV.exe on every complete-package installation.
  StrCmp $PriorCompatFlags "" setAdminOnly setAdminPreservingPriorValue
setAdminOnly:
  WriteRegStr HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe" "RUNASADMIN"
  Goto compatibilityWritten
setAdminPreservingPriorValue:
  WriteRegStr HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe" "$PriorCompatFlags RUNASADMIN"
compatibilityWritten:

  ; The complete package includes every retained config, document, source, credit, and
  ; historical release note. It is stored alongside the game for offline reference; it does
  ; not overwrite dependency-bound optional content.
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
    MessageBox MB_ICONINFORMATION "Version ${PRODUCT_VERSION} installed the complete cumulative package.$\r$\n$\r$\nApplied safely: the three core configuration files and required GTAIV.exe administrator compatibility.$\r$\n$\r$\nIncluded for your reference: both renderer profiles, all retained optional component files, shader-preload reference, full documentation, sources, credits, and historical reasoning at:$\r$\n$ReferenceRoot$\r$\n$\r$\nThe optional files are present in this installer but are not auto-deployed because their external plugins, renderer builds, and shader layouts must be verified first."
SectionEnd

Section "Uninstall"
  StrCpy $BackupRoot "$INSTDIR\${BACKUP_ROOT}"
  ClearErrors
  ReadINIStr $LastBackup "$BackupRoot\LAST_BACKUP.ini" "Installer" "BackupPath"
  ReadINIStr $PriorCompatFlags "$BackupRoot\LAST_BACKUP.ini" "Installer" "PreviousCompatFlags"
  StrCmp $LastBackup "" noBackup

  IfSilent restoreFiles
  MessageBox MB_ICONEXCLAMATION|MB_YESNO "Restore the latest core-configuration backup from:$\r$\n$LastBackup$\r$\n$\r$\nChoose Yes only if you want to replace the current core configuration with that backup." IDYES restoreFiles IDNO finishUninstall

restoreFiles:
  IfFileExists "$LastBackup\pc\stream.ini" 0 +3
    CopyFiles /SILENT "$LastBackup\pc\stream.ini" "$INSTDIR\pc"
    Goto cfgRestore
  Delete "$INSTDIR\pc\stream.ini"
cfgRestore:
  IfFileExists "$LastBackup\plugins\GTAIV.EFLC.FusionFix.cfg" 0 +3
    CopyFiles /SILENT "$LastBackup\plugins\GTAIV.EFLC.FusionFix.cfg" "$INSTDIR\plugins"
    Goto iniRestore
  Delete "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.cfg"
iniRestore:
  IfFileExists "$LastBackup\plugins\GTAIV.EFLC.FusionFix.ini" 0 +3
    CopyFiles /SILENT "$LastBackup\plugins\GTAIV.EFLC.FusionFix.ini" "$INSTDIR\plugins"
    Goto finishUninstall
  Delete "$INSTDIR\plugins\GTAIV.EFLC.FusionFix.ini"

noBackup:
  IfSilent finishUninstall
  MessageBox MB_ICONEXCLAMATION "No core-configuration backup record was found. The managed core files were left in place."

finishUninstall:
  ; Restore exactly the per-user GTAIV.exe compatibility value that existed before installation.
  StrCmp $PriorCompatFlags "" removeManagedCompatibility
  WriteRegStr HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe" "$PriorCompatFlags"
  Goto compatibilityRestored
removeManagedCompatibility:
  DeleteRegValue HKCU "${APP_COMPAT_KEY}" "$INSTDIR\GTAIV.exe"
compatibilityRestored:
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  Delete "$INSTDIR\${UNINSTALLER_NAME}"
  ; The complete reference package is intentionally retained for auditability, credits, and future recovery.
SectionEnd
