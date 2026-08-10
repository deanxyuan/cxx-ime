Function LoadTransactionState
    StrCpy $OldInstallAvailable 0
    StrCpy $OldTsfX64Present 0
    StrCpy $OldTsfX86Present 0
    StrCpy $OldTsfX64Registered 0
    StrCpy $OldTsfX86Registered 0
    StrCpy $SystemImeX64Present 0
    StrCpy $SystemImeX86Present 0
    StrCpy $OldUninstallPresent 0
    StrCpy $OldDisplayVersion ""
    StrCpy $OldRunPresent 0
    StrCpy $OldRunValue ""

    ClearErrors
    ReadINIStr $0 "$TransactionDir\${TRANSACTION_MARKER}" "transaction" "format"
    IfErrors transaction_state_invalid
    StrCmp $0 "2" 0 transaction_state_invalid
    ReadINIStr $OldInstallAvailable "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_install_available"
    ReadINIStr $OldTsfX64Present "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_tsf_x64_present"
    ReadINIStr $OldTsfX86Present "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_tsf_x86_present"
    ReadINIStr $OldTsfX64Registered "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_tsf_x64_registered"
    ReadINIStr $OldTsfX86Registered "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_tsf_x86_registered"
    ReadINIStr $SystemImeX64Present "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "system_ime_x64_present"
    ReadINIStr $SystemImeX86Present "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "system_ime_x86_present"
    ReadINIStr $OldUninstallPresent "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_uninstall_present"
    ReadINIStr $OldDisplayVersion "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_display_version"
    ReadINIStr $OldRunPresent "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_run_present"
    ReadINIStr $OldRunValue "$TransactionDir\${TRANSACTION_MARKER}" "transaction" "old_run_value"
    StrCmp $OldInstallAvailable "0" transaction_old_install_valid
    StrCmp $OldInstallAvailable "1" transaction_old_install_valid transaction_state_invalid
    transaction_old_install_valid:
    StrCmp $OldTsfX64Present "0" transaction_tsf_x64_valid
    StrCmp $OldTsfX64Present "1" transaction_tsf_x64_valid transaction_state_invalid
    transaction_tsf_x64_valid:
    StrCmp $OldTsfX86Present "0" transaction_tsf_x86_valid
    StrCmp $OldTsfX86Present "1" transaction_tsf_x86_valid transaction_state_invalid
    transaction_tsf_x86_valid:
    StrCmp $OldTsfX64Registered "0" transaction_tsf_x64_registration_valid
    StrCmp $OldTsfX64Registered "1" transaction_tsf_x64_registration_valid \
        transaction_state_invalid
    transaction_tsf_x64_registration_valid:
    StrCmp $OldTsfX86Registered "0" transaction_tsf_x86_registration_valid
    StrCmp $OldTsfX86Registered "1" transaction_tsf_x86_registration_valid \
        transaction_state_invalid
    transaction_tsf_x86_registration_valid:
    StrCmp $OldTsfX64Registered "0" transaction_tsf_x64_state_valid
    StrCmp $OldTsfX64Present "1" transaction_tsf_x64_state_valid transaction_state_invalid
    transaction_tsf_x64_state_valid:
    StrCmp $OldTsfX86Registered "0" transaction_tsf_x86_state_valid
    StrCmp $OldTsfX86Present "1" transaction_tsf_x86_state_valid transaction_state_invalid
    transaction_tsf_x86_state_valid:
    StrCmp $SystemImeX64Present "0" transaction_system_x64_valid
    StrCmp $SystemImeX64Present "1" transaction_system_x64_valid transaction_state_invalid
    transaction_system_x64_valid:
    StrCmp $SystemImeX86Present "0" transaction_system_x86_valid
    StrCmp $SystemImeX86Present "1" transaction_system_x86_valid transaction_state_invalid
    transaction_system_x86_valid:
    StrCmp $OldUninstallPresent "0" transaction_uninstall_valid
    StrCmp $OldUninstallPresent "1" transaction_uninstall_valid transaction_state_invalid
    transaction_uninstall_valid:
    StrCmp $OldRunPresent "0" transaction_run_valid
    StrCmp $OldRunPresent "1" transaction_run_valid transaction_state_invalid
    transaction_run_valid:
    Push 1
    Return

    transaction_state_invalid:
    StrCpy $FailureMessage "先前的 CxxIME 安装事务不完整或无效。"
    Push 0
FunctionEnd

Function UnregisterTransactionTsf
    IfFileExists "$TransactionDir\cxxime_tsf_x86.dll" 0 unregister_transaction_x64
        nsExec::ExecToStack \
            '"$SYSDIR\regsvr32.exe" /u /s "$TransactionDir\cxxime_tsf_x86.dll"'
        Pop $0
        Pop $1
        StrCmp $0 "0" unregister_transaction_x64
            StrCpy $FailureMessage "无法注销中断安装留下的 32 位 TSF 模块。"
            Push 0
            Return
    unregister_transaction_x64:
    IfFileExists "$TransactionDir\cxxime_tsf_x64.dll" 0 unregister_transaction_done
        nsExec::ExecToStack \
            '"$WINDIR\Sysnative\regsvr32.exe" /u /s "$TransactionDir\cxxime_tsf_x64.dll"'
        Pop $0
        Pop $1
        StrCmp $0 "0" unregister_transaction_done
            StrCpy $FailureMessage "无法注销中断安装留下的 64 位 TSF 模块。"
            Push 0
            Return
    unregister_transaction_done:
    Push 1
FunctionEnd

Function RecoverTransaction
    Call LoadTransactionState
    Pop $0
    StrCmp $0 "1" transaction_state_loaded
        Push 0
        Return

    transaction_state_loaded:
    StrCmp $TransactionDir "$INSTDIR" transaction_remove_partial_install
        Goto transaction_restore_system_ime

    transaction_remove_partial_install:
    Call UnregisterTransactionTsf
    Pop $0
    StrCmp $0 "1" transaction_restore_system_ime
        Push 0
        Return

    transaction_restore_system_ime:
    Call RestoreSystemIme
    Pop $0
    StrCmp $0 "1" transaction_restore_program_files
        Push 0
        Return

    transaction_restore_program_files:
    StrCmp $TransactionDir "$INSTDIR" 0 transaction_restore_staged_update
        RMDir /r "$INSTDIR"
        IfFileExists "$INSTDIR\*" transaction_recovery_failed
        Goto transaction_restore_old_install

    transaction_restore_staged_update:
    ${If} $OldInstallAvailable == 1
        IfFileExists "$BackupDir\*" 0 transaction_old_install_in_place
        IfFileExists "$INSTDIR\*" transaction_recovery_failed
            ClearErrors
            Rename "$BackupDir" "$INSTDIR"
            IfErrors transaction_recovery_failed
            Goto transaction_register_old_install
        transaction_old_install_in_place:
        IfFileExists "$INSTDIR\*" transaction_register_old_install
            Goto transaction_recovery_failed
    ${EndIf}
    Goto transaction_restore_registry

    transaction_restore_old_install:
    ${If} $OldInstallAvailable == 1
        IfFileExists "$BackupDir\*" 0 transaction_recovery_failed
        ClearErrors
        Rename "$BackupDir" "$INSTDIR"
        IfErrors transaction_recovery_failed
    ${Else}
        RMDir /r "$BackupDir"
        Goto transaction_restore_registry
    ${EndIf}

    transaction_register_old_install:
    Call RegisterPreviousTsf
    Pop $0
    StrCmp $0 "1" transaction_restore_registry
        Push 0
        Return

    transaction_restore_registry:
    Call RestorePreviousRegistry
    Pop $0
    StrCmp $0 "1" transaction_cleanup
        Push 0
        Return

    transaction_cleanup:
    StrCmp $TransactionDir "$StageDir" 0 +2
        RMDir /r "$StageDir"
    RMDir /r "$BackupDir"
    DetailPrint "已恢复先前的 CxxIME 安装状态。"
    Push 1
    Return

    transaction_recovery_failed:
    StrCpy $FailureMessage "无法恢复先前的 CxxIME 程序文件。"
    Push 0
FunctionEnd

Function RecoverInterruptedInstall
    IfFileExists "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" 0 recover_check_committed_install
        StrCpy $FailureMessage \
            "上一次 CxxIME 卸载尚未完成。请先重新运行已安装的卸载程序。"
        Push 0
        Return

    recover_check_committed_install:
    IfFileExists "$INSTDIR\${INSTALL_MARKER}" 0 recover_incomplete_install
        RMDir /r "$INSTDIR\${ROLLBACK_DIR}"
        Delete "$INSTDIR\${TRANSACTION_MARKER}"
        RMDir /r "$BackupDir"
        RMDir /r "$StageDir"
        IfFileExists "$BackupDir\*" recover_committed_cleanup_failed
        IfFileExists "$StageDir\*" recover_committed_cleanup_failed
        Push 1
        Return

    recover_incomplete_install:
    IfFileExists "$INSTDIR\${TRANSACTION_MARKER}" 0 recover_staged_transaction
        StrCpy $TransactionDir "$INSTDIR"
        Call RecoverTransaction
        Return

    recover_staged_transaction:
    IfFileExists "$StageDir\${TRANSACTION_MARKER}" 0 recover_untracked_backup
        StrCpy $TransactionDir "$StageDir"
        Call RecoverTransaction
        Return

    recover_untracked_backup:
    IfFileExists "$BackupDir\*" 0 recover_remove_uncommitted_stage
        StrCpy $FailureMessage \
            "检测到 CxxIME 备份目录，但没有有效的安装事务，无法安全恢复。"
        Push 0
        Return

    recover_remove_uncommitted_stage:
    RMDir /r "$StageDir"
    Push 1
    Return

    recover_committed_cleanup_failed:
    StrCpy $FailureMessage "无法清理已完成安装留下的 CxxIME 文件。"
    Push 0
FunctionEnd
