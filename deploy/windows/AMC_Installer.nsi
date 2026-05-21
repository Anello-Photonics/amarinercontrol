;--------------------------------
; AMarinerControl NSIS Installer
;--------------------------------

Unicode True

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APP_NAME "AMarinerControl"
!define APP_PUBLISHER "ANELLO Photonics"
!define APP_EXE "AMarinerControl.exe"
!define APP_ICON "ANELLO.ico"
!define APP_VERSION "1.0.0"

!ifndef APPDIR
  !error "APPDIR not defined. Run makensis with /DAPPDIR=<path-to-release-folder>"
!endif

!ifndef OUTDIR
  !define OUTDIR "."
!endif

Name "${APP_NAME}"
OutFile "${OUTDIR}\AMarinerControl-Setup.exe"

InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "InstallDir"

RequestExecutionLevel admin

!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"

;--------------------------------
; Modern UI Pages
;--------------------------------

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Variables
;--------------------------------

Var EstimatedSize

;--------------------------------
; Helper Function
;--------------------------------

Function .onInit
  SetShellVarContext all
FunctionEnd

Function un.onInit
  SetShellVarContext all
  
  ; Automatically remove any existing installation before continuing.
  ; This keeps upgrades from leaving stale files behind.
  ReadRegStr $0 HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${APP_NAME}" "UninstallString"
  StrCmp $0 "" done

  DetailPrint "Existing ${APP_NAME} installation found; uninstalling old version..."
  ExecWait '$0 /S' $1
  IntCmp $1 0 done
  MessageBox MB_ICONSTOP|MB_OK "Failed to uninstall existing ${APP_NAME} version (exit code: $1). Setup will now exit."
  Abort

done:
FunctionEnd

;--------------------------------
; Installation Section
;--------------------------------

Section "Install" SEC01

    SetOutPath "$INSTDIR"

    ; Remove old shortcuts first in case of upgrade/reinstall
    Delete "$DESKTOP\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"

    ; Copy deployed application contents from APPDIR
    File /r "${APPDIR}\*"

    ; Ensure installer icon is also present in install directory
    File "${APP_ICON}"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Shortcuts
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_ICON}"
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_ICON}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\${APP_ICON}"

    ; Main app registry key
    WriteRegStr HKLM "Software\${APP_NAME}" "InstallDir" "$INSTDIR"

    ; Add/Remove Programs registration
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayIcon" "$INSTDIR\${APP_ICON}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "NoRepair" 1

    ; Estimated installed size in KB for Add/Remove Programs
    ${GetSize} "$INSTDIR" "/S=0K" $EstimatedSize $0 $1
    IntFmt $EstimatedSize "0x%08X" $EstimatedSize
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "EstimatedSize" "$EstimatedSize"

SectionEnd

;--------------------------------
; Uninstaller Section
;--------------------------------

Section "Uninstall"

    ; Remove shortcuts
    Delete "$DESKTOP\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"

    ; Remove installed files
    RMDir /r "$INSTDIR"

    ; Remove registry entries
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
    DeleteRegKey HKLM "Software\${APP_NAME}"

SectionEnd