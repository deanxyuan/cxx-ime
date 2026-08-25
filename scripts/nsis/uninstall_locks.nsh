Function un.StopServer
    StrCpy $UninstallServerStopResult 0
    nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-running "$INSTDIR\cxxime-server.exe"'
    Pop $0
    StrCmp $0 "1" un_stop_server_done
    StrCmp $0 "0" un_server_found
        StrCpy $UninstallServerStopResult 2
        Return
    un_server_found:
    StrCpy $UninstallServerWasRunning 1
    nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" stop-server "$INSTDIR\cxxime-server.exe"'
    Pop $0
    StrCmp $0 "0" un_stop_server_wait_start
        StrCpy $UninstallServerStopResult 2
        Return
    un_stop_server_wait_start:
    StrCpy $1 0
    un_stop_server_wait:
        Sleep 100
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-running "$INSTDIR\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "1" un_stop_server_done
        StrCmp $0 "2" un_stop_server_failed
        IntOp $1 $1 + 1
        IntCmp $1 30 un_stop_server_force un_stop_server_wait un_stop_server_force
    un_stop_server_force:
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" force-stop-server "$INSTDIR\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "0" un_stop_server_force_wait
        Goto un_stop_server_failed
    un_stop_server_force_wait:
        StrCpy $1 0
    un_stop_server_force_poll:
        Sleep 100
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-running "$INSTDIR\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "1" un_stop_server_done
        StrCmp $0 "2" un_stop_server_failed
        IntOp $1 $1 + 1
        IntCmp $1 30 un_stop_server_failed un_stop_server_force_poll un_stop_server_failed
    un_stop_server_failed:
        StrCpy $UninstallServerStopResult 2
        Return
    un_stop_server_done:
FunctionEnd

Function un.RestartInstalledServer
    StrCmp $UninstallServerWasRunning "1" 0 un_restart_installed_server_done
    IfFileExists "$INSTDIR\cxxime-server.exe" 0 un_restart_installed_server_done
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" start-server "$INSTDIR\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "0" un_restart_server_wait_start
        Goto un_restart_server_failed
    un_restart_server_wait_start:
        StrCpy $1 0
    un_restart_server_wait:
        Sleep 100
        nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" server-running "$INSTDIR\cxxime-server.exe"'
        Pop $0
        StrCmp $0 "0" un_restart_installed_server_done
        IntOp $1 $1 + 1
        IntCmp $1 30 un_restart_server_failed un_restart_server_wait un_restart_server_failed
    un_restart_server_failed:
        StrCpy $FailureMessage "$FailureMessage$\r$\n$\r$\nCxxIME 后台恢复启动失败。"
    un_restart_installed_server_done:
FunctionEnd

Function un.ReleaseInputProcessor
    nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" release'
    Pop $0
    Sleep 500
    StrCmp $0 "0" un_release_input_processor_done
        DetailPrint "CxxIME TSF 释放请求失败，继续检查文件占用。"
    un_release_input_processor_done:
FunctionEnd

Function un.ReadLockReport
    StrCpy $LockReportText ""
    ClearErrors
    FileOpen $0 "$LockReportPath" r
    IfErrors un_lock_report_done
    un_lock_report_read:
        ClearErrors
        FileReadUTF16LE $0 $1
        IfErrors un_lock_report_close
        StrCpy $LockReportText "$LockReportText$1"
        Goto un_lock_report_read
    un_lock_report_close:
        FileClose $0
    un_lock_report_done:
    ${If} $LockReportText == ""
        StrCpy $LockReportText \
            "Windows 无法提供正在使用 CxxIME 的应用程序详情。"
    ${EndIf}
FunctionEnd

Function un.CheckFileLocks
    StrCpy $LockPromptOptions ""
    IfSilent un_lock_options_ready
        StrCpy $LockPromptOptions "--prompt=uninstall --parent=$HWNDPARENT"
    un_lock_options_ready:
    un_lock_retry:
        Call un.ReleaseInputProcessor
        Delete "$LockReportPath"
        nsExec::ExecToStack \
            '"$PLUGINSDIR\cxxime-installer-helper.exe" query --report "$LockReportPath" \
            $LockPromptOptions \
            "$INSTDIR\cxxime_tsf_x64.dll" "$INSTDIR\cxxime_tsf_x86.dll" \
            "$INSTDIR\cxxime_ime_x64.ime" "$INSTDIR\cxxime_ime_x86.ime" \
            "$INSTDIR\cxxime-resources.dll" "$INSTDIR\cxxime-server.exe" \
            "$INSTDIR\cxxime-settings.exe" "$INSTDIR\uninstall.exe" \
            "$WINDIR\System32\cxxime.ime" "$SYSDIR\cxxime.ime"'
        Pop $0
        Pop $1
        StrCmp $0 "0" un_lock_check_rollback
            Goto un_lock_report
    un_lock_check_rollback:
        nsExec::ExecToStack \
            '"$PLUGINSDIR\cxxime-installer-helper.exe" query --report "$LockReportPath" \
            $LockPromptOptions \
            "$UninstallRollbackDir\cxxime_tsf_x64.dll" \
            "$UninstallRollbackDir\cxxime_tsf_x86.dll" \
            "$UninstallRollbackDir\cxxime_ime_x64.ime" \
            "$UninstallRollbackDir\cxxime_ime_x86.ime" \
            "$UninstallRollbackDir\cxxime-resources.dll" \
            "$UninstallRollbackDir\cxxime-server.exe" \
            "$UninstallRollbackDir\cxxime-settings.exe"'
        Pop $0
        Pop $1
        StrCmp $0 "0" un_lock_done
    un_lock_report:
        StrCpy $LockResult $0
        Call un.ReadLockReport
        IfSilent un_lock_silent
        StrCmp $LockResult "10" un_lock_retry
        StrCmp $LockResult "11" un_lock_deferred
        StrCmp $LockResult "12" un_lock_cancel
        MessageBox MB_RETRYCANCEL|MB_ICONSTOP|MB_DEFBUTTON1 \
            "$LockReportText$\r$\n$\r$\n无法显示文件占用详情。关闭相关应用程序后单击“重试”。" \
            IDRETRY un_lock_retry
    un_lock_cancel:
        Call un.RestartInstalledServer
        SetErrorLevel 2
        Abort
    un_lock_deferred:
        StrCpy $UninstallDeferred 1
        Return
    un_lock_silent:
        DetailPrint "$LockReportText"
        Call un.RestartInstalledServer
        SetErrorLevel 2
        Abort
    un_lock_done:
FunctionEnd
