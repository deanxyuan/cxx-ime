Function un.MoveInstalledEntry
    Pop $2
    IfFileExists "$INSTDIR\$2" un_move_installed_entry 0
    IfFileExists "$INSTDIR\$2\*" un_move_installed_entry un_move_installed_entry_done
    un_move_installed_entry:
    IfFileExists "$UninstallRollbackDir\$2" un_move_installed_entry_failed 0
    IfFileExists "$UninstallRollbackDir\$2\*" un_move_installed_entry_failed
    ClearErrors
    Rename "$INSTDIR\$2" "$UninstallRollbackDir\$2"
    IfErrors un_move_installed_entry_failed
    un_move_installed_entry_done:
    Push 1
    Return

    un_move_installed_entry_failed:
    StrCpy $FailureMessage "无法暂存待删除项：$2。"
    Push 0
FunctionEnd

Function un.RestoreInstalledEntry
    Pop $2
    IfFileExists "$UninstallRollbackDir\$2" un_restore_installed_entry 0
    IfFileExists "$UninstallRollbackDir\$2\*" un_restore_installed_entry \
        un_restore_installed_entry_done
    un_restore_installed_entry:
    IfFileExists "$INSTDIR\$2" un_restore_installed_entry_failed 0
    IfFileExists "$INSTDIR\$2\*" un_restore_installed_entry_failed
    ClearErrors
    Rename "$UninstallRollbackDir\$2" "$INSTDIR\$2"
    IfErrors un_restore_installed_entry_failed
    un_restore_installed_entry_done:
    Push 1
    Return

    un_restore_installed_entry_failed:
    StrCpy $FailureMessage "卸载出错后无法恢复：$2。"
    Push 0
FunctionEnd

!macro StageInstalledEntry ENTRY
    Push "${ENTRY}"
    Call un.MoveInstalledEntry
    Pop $0
    StrCmp $0 "1" +3
        Push 0
        Return
!macroend

!macro RestoreInstalledEntry ENTRY
    Push "${ENTRY}"
    Call un.RestoreInstalledEntry
    Pop $0
    StrCmp $0 "1" +3
        Push 0
        Return
!macroend

Function un.StageInstalledFiles
    !insertmacro StageInstalledEntry "data"
    !insertmacro StageInstalledEntry "licenses"
    !insertmacro StageInstalledEntry "${ROLLBACK_DIR}"
    !insertmacro StageInstalledEntry "cxxime_tsf_x64.dll"
    !insertmacro StageInstalledEntry "cxxime_tsf_x86.dll"
    !insertmacro StageInstalledEntry "cxxime_ime_x64.ime"
    !insertmacro StageInstalledEntry "cxxime_ime_x86.ime"
    !insertmacro StageInstalledEntry "cxxime-resources.dll"
    !insertmacro StageInstalledEntry "cxxime-server.exe"
    !insertmacro StageInstalledEntry "cxxime-settings.exe"
    !insertmacro StageInstalledEntry "collect_diagnostics.ps1"
    !insertmacro StageInstalledEntry "cxxime-ime-host-probe-x64.exe"
    !insertmacro StageInstalledEntry "cxxime-ime-host-probe-x86.exe"
    !insertmacro StageInstalledEntry "export_host_trace.ps1"
    !insertmacro StageInstalledEntry "license.txt"
    !insertmacro StageInstalledEntry "THIRD_PARTY_NOTICES.txt"
    !insertmacro StageInstalledEntry "${INSTALL_MARKER}"
    Push 1
FunctionEnd

Function un.RestoreInstalledFiles
    !insertmacro RestoreInstalledEntry "${INSTALL_MARKER}"
    !insertmacro RestoreInstalledEntry "THIRD_PARTY_NOTICES.txt"
    !insertmacro RestoreInstalledEntry "license.txt"
    !insertmacro RestoreInstalledEntry "export_host_trace.ps1"
    !insertmacro RestoreInstalledEntry "cxxime-ime-host-probe-x86.exe"
    !insertmacro RestoreInstalledEntry "cxxime-ime-host-probe-x64.exe"
    !insertmacro RestoreInstalledEntry "collect_diagnostics.ps1"
    !insertmacro RestoreInstalledEntry "cxxime-settings.exe"
    !insertmacro RestoreInstalledEntry "cxxime-server.exe"
    !insertmacro RestoreInstalledEntry "cxxime-resources.dll"
    !insertmacro RestoreInstalledEntry "cxxime_ime_x86.ime"
    !insertmacro RestoreInstalledEntry "cxxime_ime_x64.ime"
    !insertmacro RestoreInstalledEntry "cxxime_tsf_x86.dll"
    !insertmacro RestoreInstalledEntry "cxxime_tsf_x64.dll"
    !insertmacro RestoreInstalledEntry "${ROLLBACK_DIR}"
    !insertmacro RestoreInstalledEntry "licenses"
    !insertmacro RestoreInstalledEntry "data"
    Push 1
FunctionEnd

Function un.RegisterInstalledTsf
    StrCmp $UninstallTsfX64Registered "1" 0 un_restore_register_x86
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 un_restore_register_x64_missing
        nsExec::ExecToStack \
            '"$WINDIR\Sysnative\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x64.dll"'
        Pop $0
        Pop $1
        StrCmp $0 "0" un_restore_register_x86
            StrCpy $FailureMessage "无法恢复 64 位 TSF 注册。"
            Push 0
            Return
    un_restore_register_x86:
    StrCmp $UninstallTsfX86Registered "1" 0 un_restore_register_done
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 un_restore_register_x86_missing
        nsExec::ExecToStack '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x86.dll"'
        Pop $0
        Pop $1
        StrCmp $0 "0" un_restore_register_done
            StrCpy $FailureMessage "无法恢复 32 位 TSF 注册。"
            Push 0
            Return
    un_restore_register_done:
    Push 1
    Return

    un_restore_register_x64_missing:
    StrCpy $FailureMessage "无法恢复 64 位 TSF 模块。"
    Push 0
    Return

    un_restore_register_x86_missing:
    StrCpy $FailureMessage "无法恢复 32 位 TSF 模块。"
    Push 0
FunctionEnd

Function un.RollbackTransaction
    Call un.RestoreInstalledFiles
    Pop $0
    StrCmp $0 "1" 0 un_rollback_failed
    Call un.RestoreSystemIme
    Pop $0
    StrCmp $0 "1" 0 un_rollback_failed
    Call un.RegisterInstalledTsf
    Pop $0
    StrCmp $0 "1" 0 un_rollback_failed
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}"
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}"
    RMDir /r "$UninstallRollbackDir"
    Push 1
    Return

    un_rollback_failed:
    Push 0
FunctionEnd

Function un.DeleteStagedFiles
    StrCpy $2 0
    un_delete_staged_files_retry:
    ClearErrors
    RMDir /r "$UninstallRollbackDir"
    IfFileExists "$UninstallRollbackDir\*" 0 un_delete_staged_files_done
    IntOp $2 $2 + 1
    IntCmp $2 10 un_delete_staged_files_failed \
        un_delete_staged_files_wait un_delete_staged_files_failed

    un_delete_staged_files_wait:
    Sleep 100
    Goto un_delete_staged_files_retry

    un_delete_staged_files_done:
    Push 1
    Return

    un_delete_staged_files_failed:
    StrCpy $FailureMessage \
        "部分 CxxIME 文件无法立即删除，将在重新启动 Windows 后完成清理。"
    Push 0
FunctionEnd

Function un.FailAndRestart
    Call un.RestartInstalledServer
    IfSilent un_failure_silent
        MessageBox MB_ICONSTOP "$FailureMessage"
    un_failure_silent:
    DetailPrint "$FailureMessage"
    SetErrorLevel 1
    Abort
FunctionEnd

Function un.FailIncomplete
    IfSilent un_incomplete_silent
        MessageBox MB_ICONSTOP "$FailureMessage"
    un_incomplete_silent:
    DetailPrint "$FailureMessage"
    SetErrorLevel 1
    Abort
FunctionEnd
