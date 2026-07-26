Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

!define PRODUCT "CxxIME"
!define PUBLISHER "CxxIME Contributors"
!define CLSID "{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"

!ifndef VERSION
    !error "VERSION must be provided by package.py"
!endif
!ifndef VERSION_NUMERIC
    !error "VERSION_NUMERIC must be provided by package.py"
!endif

Name "${PRODUCT} ${VERSION}"
!ifdef HOST_DIAGNOSTICS
    OutFile "cxxime-v${VERSION}-host-diag-setup.exe"
!else
    OutFile "cxxime-v${VERSION}-setup.exe"
!endif
InstallDir "$PROGRAMFILES\CxxIME"
RequestExecutionLevel admin
SetCompressor lzma

VIProductVersion "${VERSION_NUMERIC}"
VIAddVersionKey /LANG=2052 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=2052 "FileDescription" "CxxIME Installer"
VIAddVersionKey /LANG=2052 "FileVersion" "${VERSION_NUMERIC}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Copyright (c) 2026 CxxIME Contributors"
VIAddVersionKey /LANG=2052 "ProductName" "${PRODUCT}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${VERSION}"

Var LaunchSettings
Var UninstallRemoveUserData
Var UninstallRemoveUserDataCheckbox
Var UninstallUserDataDir
Var UninstallUserDataDirSuffix

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
Page custom FinishPage FinishPageLeave
!insertmacro MUI_UNPAGE_CONFIRM
UninstPage custom un.UserDataPage un.UserDataPageLeave
!insertmacro MUI_UNPAGE_INSTFILES
!define MUI_FINISHPAGE_REBOOTLATER_DEFAULT
!insertmacro MUI_UNPAGE_FINISH
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

Function un.onInit
    StrCpy $UninstallRemoveUserData 0
    StrCpy $UninstallUserDataDir "$PROFILE\cxxime"
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

Function un.UserDataPage
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 20u 16u 100% 24u "Choose whether to remove CxxIME user data. By default it is kept."
    Pop $0
    ${NSD_CreateLabel} 20u 46u 100% 12u "User data directory:"
    Pop $0
    ${NSD_CreateText} 28u 60u 100% 12u "$UninstallUserDataDir"
    Pop $0
    SendMessage $0 0x00CF 1 0
    ${NSD_CreateCheckbox} 20u 88u 100% 20u "Remove user configuration and dictionary data"
    Pop $UninstallRemoveUserDataCheckbox
    ${NSD_SetState} $UninstallRemoveUserDataCheckbox $UninstallRemoveUserData
    nsDialogs::Show
FunctionEnd

Function un.UserDataPageLeave
    ${NSD_GetState} $UninstallRemoveUserDataCheckbox $UninstallRemoveUserData
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

    Delete "$INSTDIR\cxxime_ime_x64.ime"
    IfFileExists "$INSTDIR\cxxime_ime_x64.ime" 0 install_ime_x64_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_ime_x64.ime"
    install_ime_x64_deleted:

    Delete "$INSTDIR\cxxime_ime_x86.ime"
    IfFileExists "$INSTDIR\cxxime_ime_x86.ime" 0 install_ime_x86_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_ime_x86.ime"
    install_ime_x86_deleted:

    Delete "$WINDIR\Sysnative\cxxime.ime.new"
    Delete "$SYSDIR\cxxime.ime.new"

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
    File "cxxime_ime_x64.ime"
    File "cxxime_ime_x86.ime"
    File "cxxime-resources.dll"
    File "cxxime-server.exe"
    File "cxxime-settings.exe"
    File "collect_diagnostics.ps1"
    !ifdef HOST_DIAGNOSTICS
        File "cxxime-ime-host-probe-x64.exe"
        File "cxxime-ime-host-probe-x86.exe"
        File "export_stage_trace.ps1"
    !else
        Delete "$INSTDIR\cxxime-ime-host-probe-x64.exe"
        Delete "$INSTDIR\cxxime-ime-host-probe-x86.exe"
        Delete "$INSTDIR\export_stage_trace.ps1"
    !endif

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
    File "data\wubi86.dict.bin"
    File "data\wubi86.dict.idx"

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

    SetOutPath "$WINDIR\Sysnative"
    ClearErrors
    File /oname=cxxime.ime "cxxime_ime_x64.ime"
    IfErrors 0 install_system_ime_x64_copied
        Delete /REBOOTOK "$WINDIR\Sysnative\cxxime.ime"
        ClearErrors
        File /oname=cxxime.ime.new "cxxime_ime_x64.ime"
        IfErrors 0 install_system_ime_x64_schedule
            MessageBox MB_ICONSTOP "Failed to install 64-bit legacy IME module."
            Abort
        install_system_ime_x64_schedule:
        ClearErrors
        Rename /REBOOTOK "$WINDIR\Sysnative\cxxime.ime.new" "$WINDIR\Sysnative\cxxime.ime"
        IfErrors 0 install_system_ime_x64_copied
            MessageBox MB_ICONSTOP "Failed to schedule 64-bit legacy IME module replacement."
            Abort
    install_system_ime_x64_copied:

    SetOutPath "$SYSDIR"
    ClearErrors
    File /oname=cxxime.ime "cxxime_ime_x86.ime"
    IfErrors 0 install_system_ime_x86_copied
        Delete /REBOOTOK "$SYSDIR\cxxime.ime"
        ClearErrors
        File /oname=cxxime.ime.new "cxxime_ime_x86.ime"
        IfErrors 0 install_system_ime_x86_schedule
            MessageBox MB_ICONSTOP "Failed to install 32-bit legacy IME module."
            Abort
        install_system_ime_x86_schedule:
        ClearErrors
        Rename /REBOOTOK "$SYSDIR\cxxime.ime.new" "$SYSDIR\cxxime.ime"
        IfErrors 0 install_system_ime_x86_copied
            MessageBox MB_ICONSTOP "Failed to schedule 32-bit legacy IME module replacement."
            Abort
    install_system_ime_x86_copied:

    SetOutPath "$INSTDIR"

    nsExec::Exec '"$WINDIR\Sysnative\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x64.dll"'
    nsExec::Exec '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x86.dll"'

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "CxxIMEServer" '"$INSTDIR\cxxime-server.exe"'

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "DisplayName" "CxxIME"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "DisplayIcon" '"$INSTDIR\cxxime-resources.dll",-100'
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "NoModify" 1
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "NoRepair" 1
    WriteUninstaller "$INSTDIR\uninstall.exe"

    SetShellVarContext all
    CreateDirectory "$SMPROGRAMS\CxxIME"
    CreateShortCut "$SMPROGRAMS\CxxIME\CxxIME Settings.lnk" "$INSTDIR\cxxime-settings.exe"
    Delete "$SMPROGRAMS\CxxIME\Host Candidate Probe x64.lnk"
    Delete "$SMPROGRAMS\CxxIME\Host Candidate Probe x86.lnk"
    Delete "$SMPROGRAMS\CxxIME\Export Stage 1 Trace.lnk"
    !ifdef HOST_DIAGNOSTICS
        CreateShortCut "$SMPROGRAMS\CxxIME\Host Candidate Probe x64.lnk" "$INSTDIR\cxxime-ime-host-probe-x64.exe"
        CreateShortCut "$SMPROGRAMS\CxxIME\Host Candidate Probe x86.lnk" "$INSTDIR\cxxime-ime-host-probe-x86.exe"
        CreateShortCut "$SMPROGRAMS\CxxIME\Export Stage 1 Trace.lnk" "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" '-NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\export_stage_trace.ps1"'
    !endif
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
    System::Call 'user32::LoadKeyboardLayoutW(w "00000409", i 0x00000001) p .r0'
    ${If} $0 != 0
        System::Call 'user32::SendMessageTimeoutW(p 0xFFFF, i 0x0050, p 0, p r0, i 0x0002, i 2000, *p .r1)'
    ${EndIf}
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

    Delete "$WINDIR\Sysnative\cxxime.ime"
    IfFileExists "$WINDIR\Sysnative\cxxime.ime" 0 system_ime_x64_deleted
        Delete /REBOOTOK "$WINDIR\Sysnative\cxxime.ime"
    system_ime_x64_deleted:

    Delete "$SYSDIR\cxxime.ime"
    IfFileExists "$SYSDIR\cxxime.ime" 0 system_ime_x86_deleted
        Delete /REBOOTOK "$SYSDIR\cxxime.ime"
    system_ime_x86_deleted:

    Delete "$WINDIR\Sysnative\cxxime.ime.new"
    Delete "$SYSDIR\cxxime.ime.new"

    Delete "$INSTDIR\cxxime_ime_x64.ime"
    IfFileExists "$INSTDIR\cxxime_ime_x64.ime" 0 ime_x64_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_ime_x64.ime"
    ime_x64_deleted:

    Delete "$INSTDIR\cxxime_ime_x86.ime"
    IfFileExists "$INSTDIR\cxxime_ime_x86.ime" 0 ime_x86_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime_ime_x86.ime"
    ime_x86_deleted:

    Delete "$INSTDIR\cxxime-resources.dll"
    IfFileExists "$INSTDIR\cxxime-resources.dll" 0 resources_dll_deleted
        Delete /REBOOTOK "$INSTDIR\cxxime-resources.dll"
    resources_dll_deleted:
    Delete "$INSTDIR\collect_diagnostics.ps1"
    Delete "$INSTDIR\export_stage_trace.ps1"
    Delete "$INSTDIR\cxxime-ime-host-probe-x64.exe"
    Delete "$INSTDIR\cxxime-ime-host-probe-x86.exe"
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

    ${If} $UninstallRemoveUserData == ${BST_CHECKED}
        StrCpy $UninstallUserDataDirSuffix $UninstallUserDataDir 7 -7
        ${If} $UninstallUserDataDir != ""
        ${AndIf} $UninstallUserDataDirSuffix == "\cxxime"
            RMDir /r "$UninstallUserDataDir"
        ${EndIf}
    ${EndIf}
SectionEnd
