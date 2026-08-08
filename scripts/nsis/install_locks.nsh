Function StopServer
    StrCpy $ServerWasRunning 0
    nsExec::Exec 'taskkill /im cxxime-server.exe'
    Pop $0
    StrCmp $0 "0" 0 +2
        StrCpy $ServerWasRunning 1
    Sleep 500
    nsExec::Exec 'taskkill /f /im cxxime-server.exe'
    Pop $0
    StrCmp $0 "0" 0 +2
        StrCpy $ServerWasRunning 1
FunctionEnd

Function RestartInstalledServer
    StrCmp $ServerWasRunning "1" 0 restart_installed_server_done
    IfFileExists "$INSTDIR\cxxime-server.exe" 0 restart_installed_server_done
        Exec '"$INSTDIR\cxxime-server.exe"'
    restart_installed_server_done:
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
            "Windows could not provide details about the applications using \
            CxxIME."
    ${EndIf}
FunctionEnd

Function CheckInstallLocks
    install_lock_retry:
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
        StrCmp $0 "0" install_lock_check_backup
            Goto install_lock_report
    install_lock_check_backup:
        nsExec::ExecToStack \
            '"$PLUGINSDIR\cxxime-installer-helper.exe" query --report "$LockReportPath" \
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
        ${If} $LockResult == "2"
            MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION|MB_DEFBUTTON1 \
                "$LockReportText$\r$\n$\r$\nClose these applications and click Retry. \
                If a Windows process retains CxxIME, sign out or restart Windows first." \
                IDRETRY install_lock_retry
        ${ElseIf} $LockResult == "3"
            MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION|MB_DEFBUTTON1 \
                "$LockReportText$\r$\n$\r$\nRestart Windows, then run setup again." \
                IDRETRY install_lock_retry
        ${Else}
            MessageBox MB_RETRYCANCEL|MB_ICONSTOP|MB_DEFBUTTON1 \
                "$LockReportText$\r$\n$\r$\nSetup cannot safely replace the installed files." \
                IDRETRY install_lock_retry
        ${EndIf}
        Call RestartInstalledServer
        SetErrorLevel 2
        Abort
    install_lock_silent:
        DetailPrint "$LockReportText"
        Call RestartInstalledServer
        SetErrorLevel 2
        Abort
    install_lock_done:
FunctionEnd
