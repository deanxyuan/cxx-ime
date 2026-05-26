!include "MUI2.nsh"
!include "FileFunc.nsh"

!define PRODUCT "CxxIME"
!define VERSION "0.1.0"
!define PUBLISHER "CxxIME Contributors"
!define CLSID "{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"

Name "${PRODUCT} ${VERSION}"
OutFile "cxxime-v${VERSION}-setup.exe"
InstallDir "$PROFILE\cxxime"
RequestExecutionLevel admin
SetCompressor lzma

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
Page custom InstallModePage InstallModeLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Var InstallMode
Var DataDir

Function InstallModePage
    nsDialogs::Create 1018
    Pop $0
    ${NSD_CreateLabel} 0 0 100% 40u "选择安装模式："
    Pop $0
    ${NSD_CreateRadioButton} 20u 50u 100% 20u "用户目录模式（安装到 %USERPROFILE%\cxxime\）"
    Pop $1
    ${NSD_CreateRadioButton} 20u 75u 100% 20u "程序目录模式（数据放在安装目录下的 data\ 子目录）"
    Pop $2
    ${NSD_Check} $1
    nsDialogs::Show
FunctionEnd

Function InstallModeLeave
    ${NSD_GetState} $1 $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $InstallMode "user"
        StrCpy $InstallDir "$PROFILE\cxxime"
    ${Else}
        StrCpy $InstallMode "program"
    ${EndIf}
FunctionEnd

Function .onInit
    ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "CxxIMEServer"
    ${If} $0 != ""
        MessageBox MB_YESNO "检测到已有安装。是否覆盖升级？" IDYES +2
        Abort
        nsExec::Exec 'taskkill /f /im cxxime-server.exe'
        Sleep 500
    ${EndIf}
FunctionEnd

Section "Install"
    SetOutPath "$INSTDIR"

    File "dist\cxxime_tsf.dll"
    File "dist\cxxime-server.exe"
    File "dist\cxxime-settings.exe"

    ${If} $InstallMode == "user"
        SetOutPath "$INSTDIR"
        File "dist\data\default.json"
        File "dist\data\themes.json"
        File "dist\data\pinyin.dict.bin"
        File "dist\data\pinyin.dict.idx"
        File "dist\data\pinyin.spellings.bin"
        File /nonfatal "dist\data\wubi86.dict.bin"
        File /nonfatal "dist\data\wubi86.dict.idx"
        File /nonfatal "dist\data\wubi86.spellings.bin"
        StrCpy $DataDir "$INSTDIR"
    ${Else}
        SetOutPath "$INSTDIR\data"
        File "dist\data\default.json"
        File "dist\data\themes.json"
        File "dist\data\pinyin.dict.bin"
        File "dist\data\pinyin.dict.idx"
        File "dist\data\pinyin.spellings.bin"
        File /nonfatal "dist\data\wubi86.dict.bin"
        File /nonfatal "dist\data\wubi86.dict.idx"
        File /nonfatal "dist\data\wubi86.spellings.bin"
        StrCpy $DataDir "$INSTDIR\data"
    ${EndIf}

    ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\cxxime_tsf.dll"'

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "CxxIMEServer" '"$INSTDIR\cxxime-server.exe"'

    Exec '"$INSTDIR\cxxime-server.exe"'

    CreateDirectory "$SMPROGRAMS\CxxIME"
    CreateShortCut "$SMPROGRAMS\CxxIME\CxxIME 设置.lnk" "$INSTDIR\cxxime-settings.exe"
    CreateShortCut "$SMPROGRAMS\CxxIME\卸载 CxxIME.lnk" "$INSTDIR\uninstall.exe"

    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" \
                     "DisplayName" "CxxIME"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" \
                     "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" \
                     "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" \
                     "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" \
                       "NoModify" 1
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME" \
                       "NoRepair" 1

    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
    nsExec::Exec 'taskkill /f /im cxxime-server.exe'
    Sleep 500

    ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\cxxime_tsf.dll"'

    DeleteRegValue HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" "CxxIMEServer"
    DeleteRegKey HKCR "CLSID\${CLSID}"
    DeleteRegKey HKLM "SOFTWARE\Microsoft\CTF\TIP\${CLSID}"
    DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME"

    RMDir /r "$SMPROGRAMS\CxxIME"

    Delete "$INSTDIR\cxxime_tsf.dll"
    Delete "$INSTDIR\cxxime-server.exe"
    Delete "$INSTDIR\cxxime-settings.exe"
    Delete "$INSTDIR\default.json"
    Delete "$INSTDIR\themes.json"
    Delete "$INSTDIR\pinyin.dict.bin"
    Delete "$INSTDIR\pinyin.dict.idx"
    Delete "$INSTDIR\pinyin.spellings.bin"
    Delete "$INSTDIR\wubi86.dict.bin"
    Delete "$INSTDIR\wubi86.dict.idx"
    Delete "$INSTDIR\wubi86.spellings.bin"
    Delete "$INSTDIR\user.tsv"
    Delete "$INSTDIR\uninstall.exe"
    RMDir /r "$INSTDIR\data"
    RMDir "$INSTDIR"

    MessageBox MB_OK "卸载完成。请注销重新登录以使更改生效。"
SectionEnd
