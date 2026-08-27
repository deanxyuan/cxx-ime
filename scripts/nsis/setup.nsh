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
    StrCpy $ServerRestartResult 0
    StrCpy $InstallStateVerified 1
    StrCpy $InstallBaseHandle 0
    StrCpy $InitialServerWasRunning 0
    StrCpy $TransactionServerWasRunning ""
    StrCpy $ServerStopResult 0
    StrCpy $ServerProcessId 0
    StrCpy $InstallBaseDir "$PROGRAMFILES64\CxxIME"
    StrCpy $PreviousInstallDir ""
    StrCpy $OldPreviousInstallDir ""
    StrCpy $PreviousVersionDir ""
    StrCpy $MultiVersionInstall 0
    StrCpy $ActiveServerDir "$PROGRAMFILES64\CxxIME"
    StrCpy $StateInstallDir "$PROGRAMFILES64\CxxIME"
    StrCpy $InstallTargetDir "$PROGRAMFILES64\CxxIME\${VERSION}"
    StrCpy $InstallTargetPrepared 0
    StrCpy $PreviousInstallFlat 0
    StrCpy $OldTipX64Present 0
    StrCpy $OldTipX86Present 0
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
        StrCpy $PreviousInstallDir $0
        StrCpy $INSTDIR $0
        ClearErrors
        ReadRegStr $1 HKLM "${UNINSTALL_KEY}" "InstallBaseLocation"
        ${IfNot} ${Errors}
        ${AndIf} $1 != ""
            StrCpy $InstallBaseDir $1
        ${Else}
            StrCpy $InstallBaseDir $0
        ${EndIf}
        ClearErrors
        ReadRegStr $PreviousVersionDir HKLM "${UNINSTALL_KEY}" "PreviousInstallLocation"
        ${If} ${Errors}
            StrCpy $PreviousVersionDir ""
        ${EndIf}
        StrCpy $INSTDIR $InstallBaseDir
        StrCpy $InstallTargetDir "$InstallBaseDir\${VERSION}"
        StrCmp $InstallTargetDir $RegisteredInstallDir setup_existing_same_version 0
            StrCpy $MultiVersionInstall 1
            Goto setup_install_layout_ready
        setup_existing_same_version:
            StrCpy $InstallTargetDir "$InstallBaseDir\${VERSION}.next"
            StrCpy $MultiVersionInstall 1
    ${EndIf}
    setup_install_layout_ready:
FunctionEnd

Function RefreshInstallLayoutAfterRecovery
    StrCpy $RegisteredInstallDir ""
    StrCpy $PreviousInstallDir ""
    StrCpy $PreviousVersionDir ""
    StrCpy $MultiVersionInstall 0
    ClearErrors
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
    IfErrors refresh_install_layout_fresh
    StrCmp $0 "" refresh_install_layout_fresh

    StrCpy $RegisteredInstallDir $0
    StrCpy $PreviousInstallDir $0
    StrCpy $ActiveServerDir $0
    StrCpy $StateInstallDir $0
    StrCpy $MultiVersionInstall 1
    ClearErrors
    ReadRegStr $1 HKLM "${UNINSTALL_KEY}" "InstallBaseLocation"
    ${IfNot} ${Errors}
    ${AndIf} $1 != ""
        StrCpy $InstallBaseDir $1
    ${Else}
        StrCpy $InstallBaseDir $0
    ${EndIf}
    ClearErrors
    ReadRegStr $PreviousVersionDir HKLM "${UNINSTALL_KEY}" "PreviousInstallLocation"
    ${If} ${Errors}
        StrCpy $PreviousVersionDir ""
    ${EndIf}
    StrCpy $InstallTargetDir "$InstallBaseDir\${VERSION}"
    StrCmp $InstallTargetDir $RegisteredInstallDir refresh_install_layout_same_version
        StrCpy $INSTDIR $InstallTargetDir
        Return
    refresh_install_layout_same_version:
        StrCpy $InstallTargetDir "$InstallBaseDir\${VERSION}.next"
        StrCpy $INSTDIR $InstallTargetDir
        Return

    refresh_install_layout_fresh:
    StrCpy $ActiveServerDir $InstallBaseDir
    StrCpy $StateInstallDir $InstallBaseDir
    StrCpy $InstallTargetDir "$InstallBaseDir\${VERSION}"
    StrCpy $INSTDIR $InstallTargetDir
FunctionEnd

Function RecoverPendingSystemIme
    IfFileExists "$InstallBaseDir\${SYSTEM_IME_UPDATE_MARKER}" recover_pending_system_ime
    IfFileExists "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}" recover_pending_system_ime
    IfFileExists "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}" recover_pending_system_ime
    Push 1
    Return

    recover_pending_system_ime:
    ClearErrors
    ReadRegStr $1 HKLM "${UNINSTALL_KEY}" "InstallLocation"
    IfErrors recover_pending_system_ime_failed
    StrCmp $1 "" recover_pending_system_ime_failed
    StrCpy $RegisteredInstallDir $1
    Push $INSTDIR
    StrCpy $INSTDIR $RegisteredInstallDir
    Call PrepareSystemImeUpdate
    Pop $0
    StrCmp $0 "1" 0 recover_pending_system_ime_restore_dir
    Call CopyNewSystemIme
    Pop $0
    recover_pending_system_ime_restore_dir:
    Pop $INSTDIR
    StrCmp $0 "1" 0 recover_pending_system_ime_failed
    IfFileExists "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}" \
        recover_pending_system_ime_restart_required
    IfFileExists "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}" \
        recover_pending_system_ime_restart_required
    Push 1
    Return

    recover_pending_system_ime_restart_required:
    StrCpy $FailureMessage \
        "上一次 CxxIME 系统 IME 更新需要重新启动 Windows 后才能完成。"
    Push 0
    Return

    recover_pending_system_ime_failed:
    StrCpy $FailureMessage "无法恢复上一次 CxxIME 系统 IME 更新。"
    Push 0
FunctionEnd

Function CheckPreviousVersionLimit
    ; Product versions identify directories only. Upgrades and downgrades use the same flow.
    StrCmp $PreviousVersionDir "" previous_version_limit_done
    StrCmp $PreviousVersionDir $RegisteredInstallDir previous_version_limit_done
    IfFileExists "$PreviousVersionDir\cxxime_tsf_x64.dll" previous_version_limit_block
    IfFileExists "$PreviousVersionDir\cxxime_tsf_x86.dll" previous_version_limit_block
    IfFileExists "$PreviousVersionDir\cxxime_ime_x64.ime" previous_version_limit_block
    IfFileExists "$PreviousVersionDir\cxxime_ime_x86.ime" previous_version_limit_block
    IfFileExists "$PreviousVersionDir\cxxime-resources.dll" previous_version_limit_block
    IfFileExists "$PreviousVersionDir\cxxime-server.exe" previous_version_limit_block
    IfFileExists "$PreviousVersionDir\cxxime-settings.exe" previous_version_limit_block
    IfFileExists "$PreviousVersionDir\uninstall.exe" previous_version_limit_block
    Goto previous_version_limit_done

    previous_version_limit_block:
    StrCpy $FailureMessage \
        "上一版本仍在被应用程序使用。请重新启动 Windows 完成旧文件清理后再升级。"
    Push 0
    Return
    previous_version_limit_done:
    Push 1
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
    StrCpy $UninstallServerStopResult 0
    StrCpy $UninstallDeferred 0
    StrCpy $UninstallDeferredResume 0
    StrCpy $UninstallRemoveUserData 0
    StrCpy $UninstallUserDataDir "$PROFILE\cxxime"
    StrCpy $PreviousVersionDir ""
    StrCpy $InstallBaseDir "$INSTDIR"
    ClearErrors
    ReadRegStr $InstallBaseDir HKLM "${UNINSTALL_KEY}" "InstallBaseLocation"
    ${If} ${Errors}
    ${OrIf} $InstallBaseDir == ""
        StrCpy $InstallBaseDir "$INSTDIR"
    ${EndIf}
    ClearErrors
    ReadRegStr $PreviousVersionDir HKLM "${UNINSTALL_KEY}" "PreviousInstallLocation"
    StrCpy $UninstallRollbackDir "$INSTDIR\${UNINSTALL_ROLLBACK_DIR}"
    IfFileExists "$INSTDIR\${UNINSTALL_DEFERRED_MARKER}" 0 un_init_ready
        ClearErrors
        ReadINIStr $0 "$INSTDIR\${UNINSTALL_DEFERRED_MARKER}" "uninstall" "state"
        IfErrors un_init_deferred_invalid
        StrCmp $0 "removing" un_init_deferred_resume
        StrCmp $0 "pending_restart" 0 un_init_deferred_invalid
            IfSilent un_init_deferred_silent
            MessageBox MB_ICONEXCLAMATION \
                "CxxIME 正在等待 Windows 重新启动以完成卸载。"
        un_init_deferred_silent:
            DetailPrint "CxxIME 正在等待 Windows 重新启动以完成卸载。"
            SetErrorLevel 3
            Abort
        un_init_deferred_resume:
            StrCpy $UninstallDeferred 1
            StrCpy $UninstallDeferredResume 1
            Goto un_init_ready
        un_init_deferred_invalid:
            IfSilent un_init_deferred_invalid_silent
            MessageBox MB_ICONSTOP \
                "CxxIME 延期卸载状态无效。请重新启动 Windows 后再次运行安装程序。"
        un_init_deferred_invalid_silent:
            DetailPrint "CxxIME 延期卸载状态无效。请重新启动 Windows 后再次运行安装程序。"
            SetErrorLevel 3
            Abort
    un_init_ready:
FunctionEnd

Function .onInstSuccess
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
        StrCmp $1 "${RUNTIME_MARKER}" install_directory_next
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
    ${If} $RegisteredInstallDir != ""
        StrCpy $MultiVersionInstall 1
        StrCpy $PreviousInstallDir $RegisteredInstallDir
        IfFileExists "$InstallTargetDir\*" install_target_conflict
        StrCpy $INSTDIR $InstallTargetDir
        StrCpy $InstallTargetPrepared 1
        Push 1
        Return
    ${EndIf}
    StrCpy $InstallBaseDir "$INSTDIR"
    Call CheckInstallDirectory
    Pop $0
    StrCmp $0 "1" install_directory_valid
        MessageBox MB_ICONSTOP "$FailureMessage"
        Abort
    install_directory_valid:
    StrCpy $InstallTargetDir "$InstallBaseDir\${VERSION}"
    StrCpy $INSTDIR $InstallTargetDir
    StrCpy $InstallTargetPrepared 1
    Return
    install_target_conflict:
    StrCpy $FailureMessage "目标版本目录已存在。请先完成或清理上一次未完成的安装。"
    IfSilent install_target_conflict_silent
        MessageBox MB_ICONSTOP "$FailureMessage"
    install_target_conflict_silent:
    DetailPrint "$FailureMessage"
    Abort
FunctionEnd

Function SetTransactionPaths
    StrCpy $StageDir "$InstallBaseDir\update"
    StrCpy $BackupDir "$InstallBaseDir\.cxxime-backup"
    StrCpy $LockReportPath "$PLUGINSDIR\cxxime-locks.txt"
FunctionEnd

Function CheckFreshInstallBase
    ${If} $MultiVersionInstall == 1
        Push 1
        Return
    ${EndIf}
    FindFirst $0 $1 "$InstallBaseDir\*"
    IfErrors fresh_install_base_ready
    fresh_install_base_scan:
        StrCmp $1 "." fresh_install_base_next
        StrCmp $1 ".." fresh_install_base_next
        FindClose $0
        StrCpy $FailureMessage \
            "所选产品目录不为空。首次安装请选择一个空目录。"
        Push 0
        Return
    fresh_install_base_next:
        ClearErrors
        FindNext $0 $1
        IfErrors fresh_install_base_empty
        Goto fresh_install_base_scan
    fresh_install_base_empty:
        FindClose $0
    fresh_install_base_ready:
    Push 1
FunctionEnd

Function SecureInstallBase
    nsExec::Exec \
        '"$PLUGINSDIR\cxxime-installer-helper.exe" secure-install-root "$InstallBaseDir"'
    Pop $0
    StrCmp $0 "0" 0 secure_install_base_failed
    System::Call 'kernel32::CreateFileW(\
        w "$InstallBaseDir", i 0x80, i 0x3, p 0, i 3, i 0x02200000, p 0) p .r0'
    StrCpy $InstallBaseHandle $0
    IntCmp $InstallBaseHandle -1 secure_install_base_failed
    nsExec::Exec \
        '"$PLUGINSDIR\cxxime-installer-helper.exe" validate-install-directory "$StageDir"'
    Pop $0
    StrCmp $0 "0" 0 secure_install_base_failed
    nsExec::Exec \
        '"$PLUGINSDIR\cxxime-installer-helper.exe" validate-install-directory "$BackupDir"'
    Pop $0
    StrCmp $0 "0" 0 secure_install_base_failed
    nsExec::Exec \
        '"$PLUGINSDIR\cxxime-installer-helper.exe" validate-install-directory "$INSTDIR"'
    Pop $0
    StrCmp $0 "0" secure_install_base_done
    secure_install_base_failed:
    StrCpy $FailureMessage "无法安全地准备 CxxIME 产品目录。"
    Push 0
    Return
    secure_install_base_done:
    Push 1
FunctionEnd

Function PrepareInstallTarget
    StrCpy $PreviousInstallFlat 0
    ${If} $MultiVersionInstall == 1
        StrCpy $ActiveServerDir "$PreviousInstallDir"
        StrCpy $StateInstallDir "$PreviousInstallDir"
        StrCpy $INSTDIR $InstallTargetDir
        ${If} $PreviousInstallDir == $InstallBaseDir
            StrCpy $PreviousInstallFlat 1
        ${EndIf}
    ${Else}
        ${If} $InstallTargetPrepared == 0
            StrCpy $InstallBaseDir "$INSTDIR"
            StrCpy $InstallTargetDir "$InstallBaseDir\${VERSION}"
            StrCpy $InstallTargetPrepared 1
        ${EndIf}
        StrCpy $ActiveServerDir "$INSTDIR"
        StrCpy $StateInstallDir "$INSTDIR"
        StrCpy $INSTDIR "$InstallTargetDir"
    ${EndIf}
FunctionEnd
