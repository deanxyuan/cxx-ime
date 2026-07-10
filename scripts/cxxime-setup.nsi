Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

!define PRODUCT "CxxIME"
!define VERSION "0.1.0"
!define PUBLISHER "CxxIME Contributors"
!define CLSID "{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"

Name "${PRODUCT} ${VERSION}"
OutFile "cxxime-v${VERSION}-setup.exe"
InstallDir "$PROGRAMFILES\CxxIME"
RequestExecutionLevel admin
SetCompressor lzma

Var LaunchSettings

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
Page custom FinishPage FinishPageLeave
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
    StrCpy $LaunchSettings 0
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "CxxIME requires 64-bit Windows."
        Abort
    ${EndIf}
    ${If} ${RunningX64}
        StrCpy $INSTDIR "$PROGRAMFILES64\CxxIME"
    ${EndIf}
FunctionEnd

Function .onInstSuccess
    Exec '"$INSTDIR\cxxime-server.exe"'
    ${If} $LaunchSettings == ${BST_CHECKED}
        Exec '"$INSTDIR\cxxime-settings.exe"'
    ${EndIf}
FunctionEnd

Function FinishPage
    nsDialogs::Create 1018
    Pop $1
    ${NSD_CreateCheckbox} 20u 50u 100% 20u "Launch CxxIME Settings"
    Pop $1
    nsDialogs::Show
FunctionEnd

Function FinishPageLeave
    ${NSD_GetState} $1 $LaunchSettings
FunctionEnd

Section "Install"
    ${If} ${RunningX64}
        SetRegView 64
    ${EndIf}

    nsExec::Exec 'taskkill /im cxxime-server.exe'
    Sleep 1500
    nsExec::Exec 'taskkill /f /im cxxime-server.exe'

    ; Unregister and clean existing TSF DLLs before overwriting them.
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 install_unregister_tsf_x86_done
        nsExec::Exec '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\cxxime_tsf_x86.dll"'
    install_unregister_tsf_x86_done:

    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 install_unregister_tsf_x64_done
        nsExec::Exec '"$WINDIR\Sysnative\regsvr32.exe" /u /s "$INSTDIR\cxxime_tsf_x64.dll"'
    install_unregister_tsf_x64_done:

    Delete "$INSTDIR\cxxime_tsf_x64.dll"
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 install_tsf_x64_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_tsf_x64.dll"
    install_tsf_x64_deleted:

    Delete "$INSTDIR\cxxime_tsf_x86.dll"
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 install_tsf_x86_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_tsf_x86.dll"
    install_tsf_x86_deleted:

    Delete "$INSTDIR\cxxime-resources.dll"
    IfFileExists "$INSTDIR\cxxime-resources.dll" 0 install_resources_dll_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime-resources.dll"
    install_resources_dll_deleted:

    Delete "$INSTDIR\cxxime_tsf_x64.dll.old"
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll.old" 0 install_tsf_x64_old_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_tsf_x64.dll.old"
    install_tsf_x64_old_deleted:

    Delete "$INSTDIR\cxxime_tsf_x86.dll.old"
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll.old" 0 install_tsf_x86_old_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_tsf_x86.dll.old"
    install_tsf_x86_old_deleted:

    SetOutPath "$INSTDIR"
    File "cxxime_tsf_x64.dll"
    File "cxxime_tsf_x86.dll"
    File "cxxime-resources.dll"
    File "cxxime-server.exe"
    File "cxxime-settings.exe"
    File "collect_diagnostics.ps1"

    !ifdef FAST
        SetCompress off
    !endif

    SetOutPath "$INSTDIR\data"
    File "data\default.json"
    File "data\settings_presets.json"
    File "data\themes.json"
    File "data\punctuation.json"
    File "data\dictionary_manifest.json"
    File "data\pinyin.dict.bin"
    File "data\pinyin.dict.idx"
    File "data\pinyin.spellings.bin"
    File "data\pinyin.topn.bin"
    File /nonfatal "data\wubi86.dict.bin"
    File /nonfatal "data\wubi86.dict.idx"

    SetCompress auto

    CreateDirectory "$PROFILE\cxxime"
    SetOutPath "$PROFILE\cxxime"
    IfFileExists "$PROFILE\cxxime\default.json" user_default_exists 0
        File "data\default.json"
    user_default_exists:
    IfFileExists "$PROFILE\cxxime\themes.json" user_themes_exists 0
        File "data\themes.json"
    user_themes_exists:
    IfFileExists "$PROFILE\cxxime\punctuation.json" user_punctuation_exists 0
        File "data\punctuation.json"
    user_punctuation_exists:

    nsExec::Exec '"$WINDIR\Sysnative\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x64.dll"'
    nsExec::Exec '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x86.dll"'

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "CxxIMEServer" '"$INSTDIR\cxxime-server.exe"'

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "DisplayName" "CxxIME"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "DisplayIcon" "$INSTDIR\cxxime-settings.exe"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "NoModify" 1
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "NoRepair" 1
    WriteUninstaller "$INSTDIR\uninstall.exe"

    SetShellVarContext all
    CreateDirectory "$SMPROGRAMS\CxxIME"
    CreateShortCut "$SMPROGRAMS\CxxIME\CxxIME Settings.lnk" "$INSTDIR\cxxime-settings.exe"
    CreateShortCut "$SMPROGRAMS\CxxIME\Collect Diagnostics.lnk" "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" '-NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\collect_diagnostics.ps1"'
    CreateShortCut "$SMPROGRAMS\CxxIME\Uninstall CxxIME.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
    ${If} ${RunningX64}
        SetRegView 64
    ${EndIf}
    SetShellVarContext all

    ; Stop server
    nsExec::Exec 'taskkill /im cxxime-server.exe'
    Sleep 1500
    nsExec::Exec 'taskkill /f /im cxxime-server.exe'

    ; Switch system keyboard to English to trigger TSF to unload CxxIME from all processes
    DeleteRegValue HKCU "Keyboard Layout\Preload" "1"
    WriteRegStr HKCU "Keyboard Layout\Preload" "1" "00000409"
    System::Call 'user32::SendMessageTimeout(i 0xFFFF, i 0x0050, i 0, i 0, i 0, i 2000, *i .r0)'
    Sleep 1000

    ; Unregister TSF DLLs and wait for TSF to notify processes
    nsExec::Exec '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\cxxime_tsf_x86.dll"'
    nsExec::Exec '"$WINDIR\Sysnative\regsvr32.exe" /u /s "$INSTDIR\cxxime_tsf_x64.dll"'
    Sleep 3000

    ; Remove registry entries
    DeleteRegValue HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "CxxIMEServer"
    nsExec::Exec '"$WINDIR\Sysnative\reg.exe" delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v CxxIMEServer /f'
    nsExec::Exec '"$SYSDIR\reg.exe" delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v CxxIMEServer /f'

    nsExec::Exec '"$WINDIR\Sysnative\reg.exe" delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" /f'
    nsExec::Exec '"$SYSDIR\reg.exe" delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" /f'

    nsExec::Exec '"$WINDIR\Sysnative\reg.exe" delete "HKLM\SOFTWARE\Classes\CLSID\${CLSID}" /f'
    nsExec::Exec '"$SYSDIR\reg.exe" delete "HKLM\SOFTWARE\Classes\CLSID\${CLSID}" /f'

    nsExec::Exec '"$WINDIR\Sysnative\reg.exe" delete "HKLM\SOFTWARE\Microsoft\CTF\TIP\${CLSID}" /f'
    nsExec::Exec '"$SYSDIR\reg.exe" delete "HKLM\SOFTWARE\Microsoft\CTF\TIP\${CLSID}" /f'

    RMDir /r "$SMPROGRAMS\CxxIME"

    ; Delete TSF DLLs. If a DLL is still loaded, schedule deletion on reboot.
    Delete "$INSTDIR\cxxime_tsf_x64.dll"
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 tsf_x64_dll_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_tsf_x64.dll"
    tsf_x64_dll_deleted:

    Delete "$INSTDIR\cxxime_tsf_x86.dll"
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 tsf_x86_dll_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_tsf_x86.dll"
    tsf_x86_dll_deleted:

    Delete "$INSTDIR\cxxime_tsf_x64.dll.old"
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll.old" 0 tsf_x64_old_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_tsf_x64.dll.old"
    tsf_x64_old_deleted:

    Delete "$INSTDIR\cxxime_tsf_x86.dll.old"
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll.old" 0 tsf_x86_old_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_tsf_x86.dll.old"
    tsf_x86_old_deleted:

    Delete "$INSTDIR\cxxime-resources.dll"
    IfFileExists "$INSTDIR\cxxime-resources.dll" 0 resources_dll_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime-resources.dll"
    resources_dll_deleted:
    Delete "$INSTDIR\collect_diagnostics.ps1"
    Delete "$INSTDIR\cxxime-server.exe"
    Delete "$INSTDIR\cxxime-settings.exe"
    Delete "$INSTDIR\data\default.json"
    Delete "$INSTDIR\data\settings_presets.json"
    Delete "$INSTDIR\data\themes.json"
    Delete "$INSTDIR\data\punctuation.json"
    Delete "$INSTDIR\data\dictionary_manifest.json"
    Delete "$INSTDIR\data\pinyin.dict.bin"
    Delete "$INSTDIR\data\pinyin.dict.idx"
    Delete "$INSTDIR\data\pinyin.spellings.bin"
    Delete "$INSTDIR\data\pinyin.topn.bin"
    Delete "$INSTDIR\data\wubi86.dict.bin"
    Delete "$INSTDIR\data\wubi86.dict.idx"
    Delete "$INSTDIR\uninstall.exe"
    RMDir /r "$INSTDIR\data"
    RMDir /REBOOTOK "$INSTDIR"

    ; Preserve user configuration and dictionaries in $PROFILE\cxxime by default.
SectionEnd
