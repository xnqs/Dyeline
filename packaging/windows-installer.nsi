; The Windows installer, built by package.py with makensis. It puts Dyeline.ofx.bundle in
; the OFX plugin folder, replacing an earlier version, and registers an uninstaller.
;
; Defined by package.py: VERSION, STAGING (the staged files), LICENSE, OUTFILE.

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

Name "Dyeline ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$COMMONFILES64\OFX\Plugins"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Dyeline"
!define APP_DIR "$PROGRAMFILES64\Dyeline"

!insertmacro MUI_PAGE_LICENSE "${LICENSE}"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "Dyeline needs 64-bit Windows."
        Abort
    ${EndIf}
    SetRegView 64
FunctionEnd

Section "Dyeline"
    ; A clean copy: files from an older version never linger in the bundle.
    RMDir /r "$INSTDIR\Dyeline.ofx.bundle"
    SetOutPath "$INSTDIR"
    File /r "${STAGING}\Dyeline.ofx.bundle"

    SetOutPath "${APP_DIR}"
    File "${STAGING}\LICENSE"
    File "${STAGING}\NOTICE"
    File "${STAGING}\INSTALL.md"
    WriteUninstaller "${APP_DIR}\Uninstall.exe"

    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "Dyeline OFX plugin"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Dyeline contributors"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR\Dyeline.ofx.bundle"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"${APP_DIR}\Uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Function un.onInit
    SetRegView 64
FunctionEnd

Section "Uninstall"
    ; Only Dyeline's own files: the plugin folder holds other plugins too. Looks the user
    ; saved, in %APPDATA%\Dyeline, are kept.
    RMDir /r "$COMMONFILES64\OFX\Plugins\Dyeline.ofx.bundle"
    Delete "${APP_DIR}\LICENSE"
    Delete "${APP_DIR}\NOTICE"
    Delete "${APP_DIR}\INSTALL.md"
    Delete "${APP_DIR}\Uninstall.exe"
    RMDir "${APP_DIR}"
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
