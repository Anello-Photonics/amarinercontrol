;--------------------------------
; General Installer Settings
;--------------------------------
Name "AMarinerControl"
OutFile "AMarinerControl-Setup.exe"
InstallDir "$PROGRAMFILES\AMarinerControl"
RequestExecutionLevel admin

; Installer & Uninstaller Icons
Icon "ANELLO.ico"
UninstallIcon "ANELLO.ico"

;--------------------------------
; Pages
;--------------------------------
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------
; Installation Section
;--------------------------------
Section "Install"

    ; Set installation path
    SetOutPath "$INSTDIR"

    ; Copy EVERYTHING from Release folder
    File /r "C:\Users\K Ryan\AMC\build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release\Release\*.*"

    ; Ensure QML folder exists
    CreateDirectory "$INSTDIR\qml"

    ; Rename executable after copying
    Rename "$INSTDIR\QGroundControl.exe" "$INSTDIR\AMarinerControl.exe"

    ; Copy icon into installation directory
    SetOutPath "$INSTDIR"
    File "ANELLO.ico"

    ; Desktop shortcut with custom icon
    CreateShortcut "$DESKTOP\AMarinerControl.lnk" "$INSTDIR\AMarinerControl.exe" "" "$INSTDIR\ANELLO.ico"

    ; Start Menu folder & shortcut
    CreateDirectory "$SMPROGRAMS\AMarinerControl"
    CreateShortcut "$SMPROGRAMS\AMarinerControl\AMarinerControl.lnk" "$INSTDIR\AMarinerControl.exe" "" "$INSTDIR\ANELLO.ico"

SectionEnd

;--------------------------------
; Uninstaller Section
;--------------------------------
Section "Uninstall"

    ; Remove desktop shortcut
    Delete "$DESKTOP\AMarinerControl.lnk"

    ; Remove Start Menu shortcut and folder
    Delete "$SMPROGRAMS\AMarinerControl\AMarinerControl.lnk"
    RMDir "$SMPROGRAMS\AMarinerControl"

    ; Remove all installed files and directories
    RMDir /r "$INSTDIR"

SectionEnd
