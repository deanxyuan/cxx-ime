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
InstallDir "$PROGRAMFILES\cxxime"
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
    ${If} ${RunningX64}
        StrCpy $INSTDIR "$PROGRAMFILES64\cxxime"
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
    ${NSD_Check} $1
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

    SetOutPath "$INSTDIR"
    File "cxxime_tsf.dll"
    File "cxxime-server.exe"
    File "cxxime-settings.exe"

    SetOutPath "$INSTDIR\data"
    File "data\default.json"
    File "data\themes.json"
    File "data\pinyin.dict.bin"
    File "data\pinyin.dict.idx"
    File "data\pinyin.spellings.bin"
    File /nonfatal "data\wubi86.dict.bin"

    CreateDirectory "$APPDATA\CxxIME"

    nsExec::Exec '"$WINDIR\Sysnative\regsvr32.exe" /s "$INSTDIR\cxxime_tsf.dll"'

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "CxxIMEServer" '"$INSTDIR\cxxime-server.exe"'

    CreateDirectory "$SMPROGRAMS\CxxIME"
    CreateShortCut "$SMPROGRAMS\CxxIME\CxxIME Settings.lnk" "$INSTDIR\cxxime-settings.exe"
    CreateShortCut "$SMPROGRAMS\CxxIME\Uninstall CxxIME.lnk" "$INSTDIR\uninstall.exe"

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "DisplayName" "CxxIME"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "NoModify" 1
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" "NoRepair" 1
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
    ${If} ${RunningX64}
        SetRegView 64
    ${EndIf}

    ; Stop server
    nsExec::Exec 'taskkill /im cxxime-server.exe'
    Sleep 1500
    nsExec::Exec 'taskkill /f /im cxxime-server.exe'

    ; Switch system keyboard to English to trigger TSF to unload CxxIME from all processes
    DeleteRegValue HKCU "Keyboard Layout\Preload" "1"
    WriteRegStr HKCU "Keyboard Layout\Preload" "1" "00000409"
    System::Call 'user32::SendMessageTimeout(i 0xFFFF, i 0x0050, i 0, i 0, i 0, i 2000, *i .r0)'
    Sleep 1000

    ; Unregister TSF DLL and wait for TSF to notify processes
    nsExec::Exec '"$WINDIR\Sysnative\regsvr32.exe" /u /s "$INSTDIR\cxxime_tsf.dll"'
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

    ; Delete TSF DLL - try immediate delete, rename-and-schedule if locked
    Delete "$INSTDIR\cxxime_tsf.dll"
    IfFileExists "$INSTDIR\cxxime_tsf.dll" 0 dll_deleted
        Rename "$INSTDIR\cxxime_tsf.dll" "$INSTDIR\cxxime_tsf.dll.old"
        System::Call 'kernel32::MoveFileEx(t "$INSTDIR\cxxime_tsf.dll.old", t "", i 0x8) i.r0'
    dll_deleted:

    Delete "$INSTDIR\cxxime-server.exe"
    Delete "$INSTDIR\cxxime-settings.exe"
    Delete "$INSTDIR\data\default.json"
    Delete "$INSTDIR\data\themes.json"
    Delete "$INSTDIR\data\pinyin.dict.bin"
    Delete "$INSTDIR\data\pinyin.dict.idx"
    Delete "$INSTDIR\data\pinyin.spellings.bin"
    Delete "$INSTDIR\data\wubi86.dict.bin"
    Delete "$INSTDIR\uninstall.exe"
    RMDir /r "$INSTDIR\data"
    RMDir /REBOOTOK "$INSTDIR"

    Delete "$APPDATA\CxxIME\default.json"
    Delete "$APPDATA\CxxIME\themes.json"
    Delete "$APPDATA\CxxIME\pinyin.dict.bin"
    Delete "$APPDATA\CxxIME\pinyin.dict.idx"
    Delete "$APPDATA\CxxIME\pinyin.spellings.bin"
    Delete "$APPDATA\CxxIME\wubi86.dict.bin"
    Delete "$APPDATA\CxxIME\user.tsv"
    RMDir "$APPDATA\CxxIME"

    SetRebootFlag true
SectionEnd
