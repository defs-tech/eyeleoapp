!include "MUI2.nsh"
ShowInstDetails show

!define APPNAME "EyeLeo"
!define VERSION "1.4.0"

; The name of the installer
Name "${APPNAME} Installer"

; The file to write
OutFile "${APPNAME}_Installer_${VERSION}_ru.exe"

; The default installation directory
InstallDir $PROGRAMFILES\${APPNAME}

; Registry key to check for directory (so if you install again, it will 
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\${APPNAME}" "Install_Dir"

; Request application privileges
RequestExecutionLevel admin

;--------------------------------

; Pages

!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_CHECKED
!define MUI_FINISHPAGE_RUN_TEXT "Launch EyeLeo immediately"
!define MUI_FINISHPAGE_RUN_FUNCTION "LaunchApp"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\readme.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
;!insertmacro MUI_UNPAGE_FINISH


!insertmacro MUI_LANGUAGE "English"

;--------------------------------

; The stuff to install
Section "Install EyeLeo (required)"

  SectionIn RO
  
  ; Install for all users
  SetShellVarContext all
  ; SetDetailsPrint textonly
  DetailPrint "Installing ${APPNAME} files..."
  
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR\Langpacks
  File "Langpacks\langpack.ru.xml"
  
  SetOutPath $INSTDIR\Personages\leopard
  File "Personages\leopard\leopard_blink.png"
  File "Personages\leopard\leopard_close_tightly.png"
  File "Personages\leopard\leopard_default.png"
  File "Personages\leopard\leopard_look_down.png"
  File "Personages\leopard\leopard_look_left.png"
  File "Personages\leopard\leopard_look_right.png"
  File "Personages\leopard\leopard_look_up.png"
  File "Personages\leopard\leopard_eyes_closed.png"
  File "Personages\leopard\leopard_eyes_open.png"
  File "Personages\leopard\leopard_shoulders_down.png"
  File "Personages\leopard\leopard_shoulders_up.png"
  File "Personages\leopard\leopard_stretch_left.png"
  File "Personages\leopard\leopard_stretch_neutral.png"
  File "Personages\leopard\leopard_stretch_right.png"
  File "Personages\leopard\leopard_stretch_up.png"
  File "Personages\leopard\leopard_stretch_up_neutral.png"
  File "Personages\leopard\leopard_stretch_upper.png"
  
  SetOutPath $INSTDIR\Resources
  File "Resources\icon.ico"
  File "Resources\icongray.ico"
  File "Resources\alarm-clock.png"
  File "Resources\alarm-clock-blue.png"
  File "Resources\balloon.png"
  File "Resources\bell.png"
  File "Resources\can-close-notifs.png"
  File "Resources\control.png"
  File "Resources\control-pause.png"
  File "Resources\cross-button.png"
  File "Resources\eyeleo_title.png"
  File "Resources\icon_settings.png"
  File "Resources\info_16.png"
  File "Resources\info_32.png"
  File "Resources\minipause_window.png"
  File "Resources\notification_leopard.png"
  File "Resources\skin2.png"
  File "Resources\skin3.png"
  File "Resources\skin4.png"
  File "Resources\tea_black.png"
  File "Resources\tea_green.png"
  File "Resources\tea_herbal.png"
  File "Resources\tea_oolong.png"
  File "Resources\tea_puer.png"
  File "Resources\tea_water.png"
  File "Resources\tea_white.png"
  File "Resources\users.png"
  File "Resources\window.png"
  File "Resources\wrench-screwdriver.png"
  File "Resources\information.ico"
  File "Resources\long_break.ico"
  File "Resources\notification.ico"
  File "Resources\pause.ico"
  File "Resources\resume.ico"
  File "Resources\settings.ico"
  File "Resources\short_break.ico"
  File "Resources\sound.ico"
  File "Resources\strict_mode.ico"
  
  SetOutPath $INSTDIR
  File "activity-monitor.dll"
  File "msvcp120.dll"
  File "msvcr120.dll"
  File "EyeLeo.exe"
  File "tea.conf"
  File "readme.txt"
  File "license.txt"
  File "/oname=config.xml" "config.ru.xml"
  
  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\${APPNAME} "Install_Dir" "$INSTDIR"
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
  WriteUninstaller "uninstall.exe"
  
SectionEnd

Section "Start Menu Shortcuts"
  SetShellVarContext all ;for all users
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\EyeLeo.exe" "$INSTDIR\icon.ico" "$INSTDIR\EyeLeo.exe" 0
SectionEnd

Section "Add EyeLeo to Startup"
  SetShellVarContext all ;for all users
  CreateShortCut "$SMSTARTUP\${APPNAME}.lnk" "$INSTDIR\EyeLeo.exe" "$INSTDIR\icon.ico" "$INSTDIR\EyeLeo.exe" 2 SW_SHOWNORMAL ALT|CTRL|SHIFT|F5 "Launch EyeLeo and start reducing your eye strain."
SectionEnd


Function LaunchApp
  ExecShell "" "$INSTDIR\EyeLeo.exe"
FunctionEnd
;--------------------------------

; Uninstaller

Section "Uninstall"
  ; Close active instance.
  ; This used to be FindProcDLL::FindProc, and that plugin no longer exists in NSIS 3.10: neither
  ; x86-ansi nor x86-unicode on the build image contains it, so makensis aborts the whole script
  ; with "Plugin not found". nsExec is shipped with NSIS, so nothing has to be vendored in.
  ;
  ; No LogicLib and no StrFunc either. tasklist with /NH prints one line per match and nothing at all
  ; when there is none, so an empty capture is the whole test. The earlier version nested ${AndIf}
  ; around ${WordFind}, which does not work: ${WordFind} expands to a macro call with six parameters and
  ; _And wants four, so makensis stopped with "requires 4 parameter(s), passed 6".
  nsExec::ExecToStack '"$SYSDIR\tasklist.exe" /NH /FI "IMAGENAME eq EyeLeo.exe"'
  Pop $0
  Pop $1
  StrCmp $1 "" notRunning
      MessageBox MB_OK|MB_ICONEXCLAMATION "EyeLeo is running. Please close the application before uninstalling." /SD IDOK
      Abort
  notRunning:
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
  DeleteRegKey HKLM SOFTWARE\${APPNAME}
  
  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\${APPNAME}\*.*"
  Delete "$SMSTARTUP\${APPNAME}.lnk"

  ; Remove directories used
  RMDir /r "$SMPROGRAMS\${APPNAME}"
  
  Delete "$INSTDIR\*.*"
  Delete "$INSTDIR\Resources\*.*"
  Delete "$INSTDIR\Langpacks\*.*"
  Delete "$INSTDIR\Personages\leopard\*.*"
  RMDir "$INSTDIR\Langpacks"
  RMDir "$INSTDIR\Resources"
  RMDir "$INSTDIR\Personages\leopard\"
  RMDir "$INSTDIR\Personages\"
  RMDir "$INSTDIR"
SectionEnd

