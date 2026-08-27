Function StopServer
    StrCpy $ServerStopResult 0
    StrCpy $ServerWasRunning $InitialServerWasRunning
    StrCmp $InitialServerWasRunning "1" stop_server_request stop_server_done
    stop_server_request:
    nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" stop-server "$ActiveServerDir\cxxime-server.exe"'
    Pop $0
    StrCmp $0 "0" stop_server_wait_start
        StrCpy $ServerStopResult 2
        Return
    stop_server_wait_start:
    StrCpy $1 0
    stop_server_wait:
        Sleep 100
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-running "$ActiveServerDir\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "1" stop_server_done
        StrCmp $0 "2" stop_server_failed
        IntOp $1 $1 + 1
        IntCmp $1 30 stop_server_force stop_server_wait stop_server_force
    stop_server_force:
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" force-stop-server "$ActiveServerDir\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "0" stop_server_force_wait
        Goto stop_server_failed
    stop_server_force_wait:
        StrCpy $1 0
    stop_server_force_poll:
        Sleep 100
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-running "$ActiveServerDir\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "1" stop_server_done
        StrCmp $0 "2" stop_server_failed
        IntOp $1 $1 + 1
        IntCmp $1 30 stop_server_failed stop_server_force_poll stop_server_failed
    stop_server_failed:
        StrCpy $ServerStopResult 2
        Return
    stop_server_done:
    StrCpy $ServerWasRunning $InitialServerWasRunning
FunctionEnd

Function QueryTipRegistration
    StrCpy $1 0
    tip_registration_enum:
        ClearErrors
        EnumRegKey $0 HKLM "${TSF_TIP_KEY}" $1
        IfErrors tip_registration_missing
        StrCmp $0 "LanguageProfile" tip_registration_profile
        IntOp $1 $1 + 1
        Goto tip_registration_enum
    tip_registration_profile:
        ClearErrors
        EnumRegKey $0 HKLM "${TSF_TIP_KEY}\LanguageProfile" 0
        IfErrors tip_registration_missing
        Push 1
        Return
    tip_registration_missing:
        Push 0
FunctionEnd

Function CaptureServerState
    nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-running "$ActiveServerDir\cxxime-server.exe"'
    Pop $0
    StrCmp $0 "0" capture_server_running
    StrCmp $0 "1" capture_server_not_running
        StrCpy $FailureMessage "无法读取 CxxIME 安装前后台状态。"
        Push 0
        Return
    capture_server_not_running:
        StrCpy $InitialServerWasRunning 0
        StrCpy $ServerWasRunning 0
        Push 1
        Return
    capture_server_running:
        StrCpy $InitialServerWasRunning 1
        StrCpy $ServerWasRunning 1
        nsExec::ExecToStack \
            '"$PLUGINSDIR\cxxime-installer-helper.exe" server-pid "$ActiveServerDir\cxxime-server.exe"'
        Pop $1
        Pop $2
        StrCmp $1 "0" 0 capture_server_query_failed
        StrCpy $ServerProcessId $2
        Push 1
        Return
    capture_server_query_failed:
        StrCpy $FailureMessage "无法读取 CxxIME 后台进程标识。"
        Push 0
FunctionEnd

Function RestartInstalledServer
    StrCpy $ServerRestartResult 0
    StrCmp $InitialServerWasRunning "1" 0 restart_installed_server_done
    StrCmp $InstallStateVerified "1" 0 restart_installed_server_failed
    IfFileExists "$ActiveServerDir\cxxime-server.exe" 0 restart_installed_server_failed
        ClearErrors
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" start-server "$ActiveServerDir\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "0" 0 restart_installed_server_failed
        StrCpy $1 0
    restart_installed_server_wait:
        Sleep 100
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-running "$ActiveServerDir\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "0" restart_installed_server_ready
        StrCmp $0 "2" restart_installed_server_failed
        IntOp $1 $1 + 1
        IntCmp $1 30 restart_installed_server_failed restart_installed_server_wait \
            restart_installed_server_failed
    restart_installed_server_ready:
        StrCpy $ServerRestartResult 1
        DetailPrint "CxxIME 后台已恢复启动。"
        Goto restart_installed_server_done
    restart_installed_server_failed:
        StrCpy $ServerRestartResult 2
        DetailPrint "CxxIME 后台恢复启动失败。"
    restart_installed_server_done:
FunctionEnd

Function StartNewServer
    IfFileExists "$INSTDIR\cxxime-server.exe" 0 start_new_server_failed
    nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" start-server "$INSTDIR\cxxime-server.exe"'
    Pop $0
    StrCmp $0 "0" start_new_server_poll start_new_server_failed
    StrCpy $1 0
    start_new_server_poll:
        Sleep 100
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-ready "$INSTDIR\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "0" start_new_server_ready
        StrCmp $0 "2" start_new_server_failed
        IntOp $1 $1 + 1
        IntCmp $1 30 start_new_server_failed start_new_server_poll start_new_server_failed
    start_new_server_ready:
        Push 1
        Return
    start_new_server_failed:
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" force-stop-server "$INSTDIR\cxxime-server.exe"'
        Pop $0
        StrCpy $FailureMessage "无法确认新版本 CxxIME 后台已启动。"
        Push 0
FunctionEnd

Function CleanupRuntimeSnapshotAfterServerRestore
    StrCmp $ServerRestartResult "2" cleanup_runtime_snapshot_done
        Delete "$INSTDIR\..\${RUNTIME_MARKER}"
    cleanup_runtime_snapshot_done:
FunctionEnd

Function ReleaseInputProcessor
    nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" release'
    Pop $0
    Sleep 500
    StrCmp $0 "0" release_input_processor_done
        DetailPrint "CxxIME TSF 释放请求失败，继续检查文件占用。"
    release_input_processor_done:
FunctionEnd

Function VerifyRestoredInstall
    ${If} $MultiVersionInstall == 1
        StrCpy $INSTDIR "$StateInstallDir"
    ${EndIf}
    StrCmp $OldInstallAvailable "1" restored_install_check_files
    IfFileExists "$INSTDIR\cxxime-server.exe" 0 restored_install_no_old_resources
        Goto restored_install_invalid
    restored_install_no_old_resources:
    IfFileExists "$INSTDIR\cxxime-resources.dll" 0 restored_install_no_old_tsf_x64
        Goto restored_install_invalid
    restored_install_no_old_tsf_x64:
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 restored_install_no_old_tsf_x86
        Goto restored_install_invalid
    restored_install_no_old_tsf_x86:
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 restored_install_no_old_ime_x64
        Goto restored_install_invalid
    restored_install_no_old_ime_x64:
    IfFileExists "$WINDIR\Sysnative\cxxime.ime" 0 restored_install_no_old_ime_x86
        Goto restored_install_invalid
    restored_install_no_old_ime_x86:
    IfFileExists "$SYSDIR\cxxime.ime" 0 restored_install_registry
        Goto restored_install_invalid

    restored_install_check_files:
    IfFileExists "$INSTDIR\cxxime-server.exe" 0 restored_install_invalid
    IfFileExists "$INSTDIR\cxxime-resources.dll" 0 restored_install_invalid
    StrCmp $OldTsfX64Present "1" restored_install_tsf_x64_present restored_install_tsf_x64_absent
    restored_install_tsf_x64_present:
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 restored_install_invalid
    Goto restored_install_x86
    restored_install_tsf_x64_absent:
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 restored_install_x86
        Goto restored_install_invalid
    restored_install_x86:
    StrCmp $OldTsfX86Present "1" restored_install_tsf_x86_present restored_install_tsf_x86_absent
    restored_install_tsf_x86_present:
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 restored_install_invalid
    Goto restored_install_system_x64
    restored_install_tsf_x86_absent:
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 restored_install_system_x64
        Goto restored_install_invalid
    restored_install_system_x64:
    StrCmp $SystemImeX64Present "1" restored_install_ime_x64_present restored_install_ime_x64_absent
    restored_install_ime_x64_present:
    IfFileExists "$WINDIR\Sysnative\cxxime.ime" 0 restored_install_invalid
    Goto restored_install_system_x86
    restored_install_ime_x64_absent:
    IfFileExists "$WINDIR\Sysnative\cxxime.ime" 0 restored_install_system_x86
        Goto restored_install_invalid
    restored_install_system_x86:
    StrCmp $SystemImeX86Present "1" restored_install_ime_x86_present restored_install_ime_x86_absent
    restored_install_ime_x86_present:
    IfFileExists "$SYSDIR\cxxime.ime" 0 restored_install_invalid
    Goto restored_install_registry
    restored_install_ime_x86_absent:
    IfFileExists "$SYSDIR\cxxime.ime" 0 restored_install_registry
        Goto restored_install_invalid
    restored_install_registry:
    ${If} $OldTsfX64Registered == 1
        SetRegView 64
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${If} ${Errors}
            Goto restored_install_invalid
        ${EndIf}
        ${If} $0 != "$INSTDIR\cxxime_tsf_x64.dll"
            Goto restored_install_invalid
        ${EndIf}
    ${Else}
        SetRegView 64
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${IfNot} ${Errors}
            Goto restored_install_invalid
        ${EndIf}
    ${EndIf}
    ${If} $OldTsfX86Registered == 1
        SetRegView 32
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${If} ${Errors}
            SetRegView 64
            Goto restored_install_invalid
        ${EndIf}
        ${If} $0 != "$INSTDIR\cxxime_tsf_x86.dll"
            SetRegView 64
            Goto restored_install_invalid
        ${EndIf}
        SetRegView 64
    ${Else}
        SetRegView 32
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${IfNot} ${Errors}
            SetRegView 64
            Goto restored_install_invalid
        ${EndIf}
        SetRegView 64
    ${EndIf}
    SetRegView 64
    Call QueryTipRegistration
    Pop $0
    ${If} $0 != $OldTipX64Present
        Goto restored_install_invalid
    ${EndIf}
    SetRegView 32
    Call QueryTipRegistration
    Pop $0
    ${If} $0 != $OldTipX86Present
        SetRegView 64
        Goto restored_install_invalid
    ${EndIf}
    SetRegView 64
    ClearErrors
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "DisplayVersion"
    ${If} $OldUninstallPresent == 1
        ${If} ${Errors}
            Goto restored_install_invalid
        ${EndIf}
        ${If} $0 != "$OldDisplayVersion"
            Goto restored_install_invalid
        ${EndIf}
    ${Else}
        ${IfNot} ${Errors}
            Goto restored_install_invalid
        ${EndIf}
    ${EndIf}
    ClearErrors
    ReadRegStr $0 HKLM "${RUN_KEY}" "CxxIMEServer"
    ${If} $OldRunPresent == 1
        ${If} ${Errors}
            Goto restored_install_invalid
        ${EndIf}
        ${If} $0 != "$OldRunValue"
            Goto restored_install_invalid
        ${EndIf}
    ${Else}
        ${IfNot} ${Errors}
            Goto restored_install_invalid
        ${EndIf}
    ${EndIf}
    StrCpy $InstallStateVerified 1
    Push 1
    Return
    restored_install_invalid:
        StrCpy $InstallStateVerified 0
        StrCpy $FailureMessage \
            "$FailureMessage$\r$\n$\r$\nCxxIME 安装前状态未通过恢复校验，未启动后台。"
        Push 0
FunctionEnd

Function ReadLockReport
    StrCpy $LockReportText ""
    ClearErrors
    FileOpen $0 "$LockReportPath" r
    IfErrors lock_report_done
    lock_report_read:
        ClearErrors
        FileReadUTF16LE $0 $1
        IfErrors lock_report_close
        StrCpy $LockReportText "$LockReportText$1"
        Goto lock_report_read
    lock_report_close:
        FileClose $0
    lock_report_done:
    ${If} $LockReportText == ""
        StrCpy $LockReportText \
            "Windows 无法提供正在使用 CxxIME 的应用程序详情。"
    ${EndIf}
FunctionEnd

Function CheckInstallLocks
    StrCmp $MultiVersionInstall "1" install_lock_done
    StrCpy $LockPromptOptions ""
    IfSilent install_lock_options_ready
        StrCpy $LockPromptOptions "--prompt=install --parent=$HWNDPARENT"
    install_lock_options_ready:
    install_lock_query:
        Delete "$LockReportPath"
        nsExec::ExecToStack \
            '"$PLUGINSDIR\cxxime-installer-helper.exe" query --report "$LockReportPath" \
            $LockPromptOptions \
            "$ActiveServerDir\cxxime_tsf_x64.dll" "$ActiveServerDir\cxxime_tsf_x86.dll" \
            "$ActiveServerDir\cxxime_ime_x64.ime" "$ActiveServerDir\cxxime_ime_x86.ime" \
            "$ActiveServerDir\cxxime-resources.dll" "$ActiveServerDir\cxxime-server.exe" \
            "$ActiveServerDir\cxxime-settings.exe" "$ActiveServerDir\uninstall.exe" \
            "$WINDIR\System32\cxxime.ime" "$SYSDIR\cxxime.ime"'
        Pop $0
        Pop $1
        StrCmp $0 "0" install_lock_check_backup
            Goto install_lock_report
    install_lock_check_backup:
        nsExec::ExecToStack \
            '"$PLUGINSDIR\cxxime-installer-helper.exe" query --report "$LockReportPath" \
            $LockPromptOptions \
            "$BackupDir\cxxime_tsf_x64.dll" "$BackupDir\cxxime_tsf_x86.dll" \
            "$BackupDir\cxxime_ime_x64.ime" "$BackupDir\cxxime_ime_x86.ime" \
            "$BackupDir\cxxime-resources.dll" "$BackupDir\cxxime-server.exe" \
            "$BackupDir\cxxime-settings.exe" "$BackupDir\uninstall.exe"'
        Pop $0
        Pop $1
        StrCmp $0 "0" install_lock_check_stage
            Goto install_lock_report
    install_lock_check_stage:
        nsExec::ExecToStack \
            '"$PLUGINSDIR\cxxime-installer-helper.exe" query --report "$LockReportPath" \
            $LockPromptOptions \
            "$StageDir\cxxime_tsf_x64.dll" "$StageDir\cxxime_tsf_x86.dll" \
            "$StageDir\cxxime_ime_x64.ime" "$StageDir\cxxime_ime_x86.ime" \
            "$StageDir\cxxime-resources.dll" "$StageDir\cxxime-server.exe" \
            "$StageDir\cxxime-settings.exe" "$StageDir\uninstall.exe"'
        Pop $0
        Pop $1
        StrCmp $0 "0" install_lock_done
    install_lock_report:
        StrCpy $LockResult $0
        Call ReadLockReport
        IfSilent install_lock_silent
        StrCmp $LockResult "10" install_lock_retry
        StrCmp $LockResult "12" install_lock_cancel_restore
        MessageBox MB_RETRYCANCEL|MB_ICONSTOP|MB_DEFBUTTON1 \
            "$LockReportText$\r$\n$\r$\n无法显示文件占用详情。关闭相关应用程序后单击“重试”。" \
            IDRETRY install_lock_retry
    install_lock_cancel_restore:
        Call RestartInstalledServer
        Call CleanupRuntimeSnapshotAfterServerRestore
        SetErrorLevel 2
        Abort
    install_lock_silent:
        DetailPrint "$LockReportText"
        Call RestartInstalledServer
        Call CleanupRuntimeSnapshotAfterServerRestore
        SetErrorLevel 2
        Abort
    install_lock_retry:
        Call ReleaseInputProcessor
        Goto install_lock_query
    install_lock_done:
FunctionEnd
