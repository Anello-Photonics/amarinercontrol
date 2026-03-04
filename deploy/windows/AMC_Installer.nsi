;--------------------------------
; General Installer Settings
;--------------------------------

!define MUI_ICON "ANELLO.ico"
!define MUI_UNICON "ANELLO.ico"
!include "MUI2.nsh"

!include "MUI2.nsh"
!include "x64.nsh"

Name "AMarinerControl"
OutFile "AMarinerControl-Setup.exe"


; Use 64-bit Program Files since your build is MSVC2022_64bit
InstallDir "$PROGRAMFILES64\AMarinerControl"
InstallDirRegKey HKLM "Software\AMarinerControl" "InstallDir"

RequestExecutionLevel admin


;--------------------------------
; Modern UI Pages
;--------------------------------
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Variables
;--------------------------------
Var StartMenuFolder

;--------------------------------
; Installation Section
;--------------------------------
Section "Install" SEC01

    SetOutPath "$INSTDIR"

    ; Copy EVERYTHING from your deployed Release folder
    ; (Make sure you've already run windeployqt here)
    File /r "C:\Users\K Ryan\AMC\build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release\Release\*"

    ; Copy icon into installation directory
    File "ANELLO.ico"

    ; Shortcuts
    CreateShortcut "$DESKTOP\AMarinerControl.lnk" "$INSTDIR\AMarinerControl.exe" "" "$INSTDIR\ANELLO.ico"

    CreateDirectory "$SMPROGRAMS\AMarinerControl"
    CreateShortcut "$SMPROGRAMS\AMarinerControl\AMarinerControl.lnk" "$INSTDIR\AMarinerControl.exe" "" "$INSTDIR\ANELLO.ico"
    CreateShortcut "$SMPROGRAMS\AMarinerControl\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\ANELLO.ico"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Add/Remove Programs registration
    WriteRegStr HKLM "Software\AMarinerControl" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AMarinerControl" "DisplayName" "AMarinerControl"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AMarinerControl" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AMarinerControl" "DisplayIcon" "$\"$INSTDIR\ANELLO.ico$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AMarinerControl" "InstallLocation" "$\"$INSTDIR$\""
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AMarinerControl" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AMarinerControl" "NoRepair" 1

SectionEnd

;--------------------------------
; Uninstaller Section
;--------------------------------
Section "Uninstall"

    ; Remove shortcuts
    Delete "$DESKTOP\AMarinerControl.lnk"

    Delete "$SMPROGRAMS\AMarinerControl\AMarinerControl.lnk"
    Delete "$SMPROGRAMS\AMarinerControl\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\AMarinerControl"

    ; Remove installed files
    RMDir /r "$INSTDIR"

    ; Remove registry entries
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AMarinerControl"
    DeleteRegKey HKLM "Software\AMarinerControl"

SectionEnd
