Function un.StopServer
    StrCpy $UninstallServerWasRunning 0
    nsExec::Exec 'taskkill /im cxxime-server.exe'
    Pop $0
    StrCmp $0 "0" 0 +2
        StrCpy $UninstallServerWasRunning 1
    Sleep 500
    nsExec::Exec 'taskkill /f /im cxxime-server.exe'
    Pop $0
    StrCmp $0 "0" 0 +2
        StrCpy $UninstallServerWasRunning 1
FunctionEnd

Function un.RestartInstalledServer
    StrCmp $UninstallServerWasRunning "1" 0 un_restart_installed_server_done
    IfFileExists "$INSTDIR\cxxime-server.exe" 0 un_restart_installed_server_done
        Exec '"$INSTDIR\cxxime-server.exe"'
    un_restart_installed_server_done:
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
            "Windows could not provide details about the applications using \
            CxxIME."
    ${EndIf}
FunctionEnd

Function un.CheckFileLocks
    un_lock_retry:
        Delete "$LockReportPath"
        nsExec::ExecToStack \
            '"$PLUGINSDIR\cxxime-installer-helper.exe" query --report "$LockReportPath" \
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
        Call un.ReadLockReport
        IfSilent un_lock_silent
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION|MB_DEFBUTTON1 \
            "$LockReportText$\r$\n$\r$\nClose these applications and click Retry. \
            If a Windows process retains CxxIME, sign out or restart Windows first." \
            IDRETRY un_lock_retry
        Call un.RestartInstalledServer
        SetErrorLevel 2
        Abort
    un_lock_silent:
        DetailPrint "$LockReportText"
        Call un.RestartInstalledServer
        SetErrorLevel 2
        Abort
    un_lock_done:
FunctionEnd
