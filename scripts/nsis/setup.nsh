Function AcquireInstallerMutex
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Global\CxxIME.Installation") p .r1 ?e'
    Pop $0
    ${If} $1 == 0
        StrCpy $FailureMessage "无法初始化 CxxIME 安装程序。"
        IfSilent installer_mutex_failed_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        installer_mutex_failed_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} $0 == ${ERROR_ALREADY_EXISTS}
        StrCpy $FailureMessage "另一个 CxxIME 安装程序或卸载程序正在运行。"
        IfSilent installer_already_running_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        installer_already_running_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}
FunctionEnd

Function .onInit
    StrCpy $LaunchSettings 0
    ${IfNot} ${RunningX64}
        StrCpy $FailureMessage "CxxIME 需要 64 位 Windows。"
        IfSilent installer_requires_x64_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        installer_requires_x64_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}

    Call AcquireInstallerMutex
    SetShellVarContext all
    SetRegView 64
    StrCpy $INSTDIR "$PROGRAMFILES64\CxxIME"
    StrCpy $RegisteredInstallDir ""
    ClearErrors
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
    ${IfNot} ${Errors}
    ${AndIf} $0 != ""
        StrCpy $RegisteredInstallDir $0
        StrCpy $INSTDIR $0
    ${EndIf}
FunctionEnd

Function un.AcquireInstallerMutex
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Global\CxxIME.Installation") p .r1 ?e'
    Pop $0
    ${If} $1 == 0
        StrCpy $FailureMessage "无法初始化 CxxIME 卸载程序。"
        IfSilent un_mutex_failed_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        un_mutex_failed_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} $0 == ${ERROR_ALREADY_EXISTS}
        StrCpy $FailureMessage "另一个 CxxIME 安装程序或卸载程序正在运行。"
        IfSilent un_already_running_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        un_already_running_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}
FunctionEnd

Function un.onInit
    Call un.AcquireInstallerMutex
    SetShellVarContext all
    SetRegView 64
    StrCpy $UninstallServerWasRunning 0
    StrCpy $UninstallDeferred 0
    StrCpy $UninstallDeferredResume 0
    StrCpy $UninstallRemoveUserData 0
    StrCpy $UninstallUserDataDir "$PROFILE\cxxime"
    StrCpy $UninstallRollbackDir "$INSTDIR\${UNINSTALL_ROLLBACK_DIR}"
    IfFileExists "$INSTDIR\${UNINSTALL_DEFERRED_MARKER}" 0 un_init_ready
        ClearErrors
        ReadINIStr $0 "$INSTDIR\${UNINSTALL_DEFERRED_MARKER}" "uninstall" "state"
        IfErrors un_init_deferred_invalid
        StrCmp $0 "removing" un_init_deferred_resume
        StrCmp $0 "pending_restart" 0 un_init_deferred_invalid
            MessageBox MB_ICONEXCLAMATION \
                "CxxIME 正在等待 Windows 重新启动以完成卸载。"
            SetErrorLevel 3
            Abort
        un_init_deferred_resume:
            StrCpy $UninstallDeferred 1
            StrCpy $UninstallDeferredResume 1
            Goto un_init_ready
        un_init_deferred_invalid:
            MessageBox MB_ICONSTOP \
                "CxxIME 延期卸载状态无效。请重新启动 Windows 后再次运行安装程序。"
            SetErrorLevel 3
            Abort
    un_init_ready:
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
    ${NSD_CreateCheckbox} 20u 50u 100% 20u "启动 CxxIME 设置"
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

    ${NSD_CreateLabel} 20u 16u 100% 24u \
        "选择是否删除 CxxIME 用户数据。默认保留用户数据。"
    Pop $0
    ${NSD_CreateLabel} 20u 46u 100% 12u "用户数据目录："
    Pop $0
    ${NSD_CreateText} 28u 60u 100% 12u "$UninstallUserDataDir"
    Pop $0
    SendMessage $0 0x00CF 1 0
    ${NSD_CreateCheckbox} 20u 88u 100% 20u "删除用户配置和词库数据"
    Pop $UninstallRemoveUserDataCheckbox
    ${NSD_SetState} $UninstallRemoveUserDataCheckbox $UninstallRemoveUserData
    nsDialogs::Show
FunctionEnd

Function un.UserDataPageLeave
    ${NSD_GetState} $UninstallRemoveUserDataCheckbox $UninstallRemoveUserData
FunctionEnd

Function CheckInstallDirectory
    StrCpy $ExistingInstall 0
    IfFileExists "$INSTDIR\${UNINSTALL_DEFERRED_MARKER}" 0 install_directory_check_marker
        StrCpy $FailureMessage \
            "CxxIME 正在等待 Windows 重新启动以完成卸载。请重启后再安装。"
        Push 0
        Return
    install_directory_check_marker:
    IfFileExists "$INSTDIR\${INSTALL_MARKER}" install_directory_owned
    StrCmp $RegisteredInstallDir "$INSTDIR" 0 install_directory_scan_start
    IfFileExists "$INSTDIR\cxxime-server.exe" 0 install_directory_scan_start
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 install_directory_scan_start
    IfFileExists "$INSTDIR\uninstall.exe" install_directory_owned

    install_directory_scan_start:
    IfFileExists "$INSTDIR\cxxime-resources.dll" 0 install_directory_scan_contents
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" install_directory_owned
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" install_directory_owned

    install_directory_scan_contents:
    FindFirst $0 $1 "$INSTDIR\*"
    IfErrors install_directory_ready
    install_directory_scan:
        StrCmp $1 "." install_directory_next
        StrCmp $1 ".." install_directory_next
        FindClose $0
        StrCpy $FailureMessage \
            "所选目录不为空，并且不是 CxxIME 安装目录。请选择一个空目录。"
        Push 0
        Return
    install_directory_next:
        ClearErrors
        FindNext $0 $1
        IfErrors install_directory_empty
        Goto install_directory_scan
    install_directory_empty:
        FindClose $0
        Goto install_directory_ready

    install_directory_owned:
        StrCpy $ExistingInstall 1
    install_directory_ready:
    Push 1
FunctionEnd

Function ValidateInstallDirectory
    Call CheckInstallDirectory
    Pop $0
    StrCmp $0 "1" install_directory_valid
        MessageBox MB_ICONSTOP "$FailureMessage"
        Abort
    install_directory_valid:
FunctionEnd

Function SetTransactionPaths
    StrCpy $StageDir "$INSTDIR.cxxime-stage"
    StrCpy $BackupDir "$INSTDIR.cxxime-backup"
    StrCpy $LockReportPath "$PLUGINSDIR\cxxime-locks.txt"
FunctionEnd
