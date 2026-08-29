Function LoadTransactionState
    StrCpy $ServerWasRunning ""
    StrCpy $TransactionServerWasRunning ""
    StrCpy $OldInstallAvailable 0
    StrCpy $OldTsfX64Present 0
    StrCpy $OldTsfX86Present 0
    StrCpy $OldTsfX64Registered 0
    StrCpy $OldTsfX86Registered 0
    StrCpy $OldTipX64Present 0
    StrCpy $OldTipX86Present 0
    StrCpy $SystemImeX64Present 0
    StrCpy $SystemImeX86Present 0
    StrCpy $OldUninstallPresent 0
    StrCpy $OldDisplayVersion ""
    StrCpy $OldRunPresent 0
    StrCpy $OldRunValue ""
    StrCpy $OldPreviousInstallDir ""
    StrCpy $InstallTransactionFormat ""

    ClearErrors
    ReadINIStr $0 "$TransactionDir\${TRANSACTION_MARKER}" "transaction" "format"
    IfErrors transaction_state_invalid
    StrCpy $InstallTransactionFormat $0
    StrCmp $0 "2" transaction_state_format_2
    StrCmp $0 "3" transaction_state_format_3
    StrCmp $0 "4" transaction_state_format_3 transaction_state_invalid
    transaction_state_format_3:
    ReadINIStr $OldPreviousInstallDir "$TransactionDir\${TRANSACTION_MARKER}" \
        "transaction" "old_previous_install_dir"
    IfErrors transaction_state_invalid
    Goto transaction_state_format_ready
    transaction_state_format_2:
    StrCpy $OldPreviousInstallDir ""
    transaction_state_format_ready:
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
    ReadINIStr $OldTipX64Present "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_tip_x64_present"
    ReadINIStr $OldTipX86Present "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_tip_x86_present"
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
    ReadINIStr $PreviousInstallDir "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "old_install_dir"
    StrCpy $StateInstallDir "$PreviousInstallDir"
    ${If} $PreviousInstallDir != ""
        StrCpy $MultiVersionInstall 1
        StrCpy $ActiveServerDir "$PreviousInstallDir"
        StrCpy $PreviousInstallFlat 0
        StrCmp $PreviousInstallDir $InstallBaseDir 0 +2
            StrCpy $PreviousInstallFlat 1
    ${EndIf}
    ReadINIStr $ServerWasRunning "$TransactionDir\${TRANSACTION_MARKER}" "transaction" \
        "server_was_running"
    StrCpy $TransactionServerWasRunning $ServerWasRunning
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
    StrCmp $OldTipX64Present "0" transaction_tip_x86_valid
    StrCmp $OldTipX64Present "1" transaction_tip_x86_valid transaction_state_invalid
    transaction_tip_x86_valid:
    StrCmp $OldTipX86Present "0" transaction_old_install_state_valid
    StrCmp $OldTipX86Present "1" transaction_old_install_state_valid transaction_state_invalid
    transaction_old_install_state_valid:
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
    StrCmp $TransactionServerWasRunning "0" transaction_server_state_valid
    StrCmp $TransactionServerWasRunning "1" transaction_server_state_valid transaction_state_invalid
    transaction_server_state_valid:
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
    StrCmp $InstallTransactionFormat "4" 0 transaction_pending_ime_cancelled
        Call CancelPendingSystemImeUpdate
    transaction_pending_ime_cancelled:
    StrCmp $MultiVersionInstall "1" transaction_multiversion_restore
    StrCmp $TransactionDir "$INSTDIR" transaction_remove_partial_install
        Goto transaction_restore_system_ime

    transaction_multiversion_restore:
    Call UnregisterTransactionTsf
    Pop $0
    StrCmp $0 "1" transaction_multiversion_restore_ime
        Push 0
        Return
    transaction_multiversion_restore_ime:
    StrCmp $InstallTransactionFormat "4" transaction_multiversion_restore_register
    Call RestoreSystemIme
    Pop $0
    StrCmp $0 "1" transaction_multiversion_restore_register
        Push 0
        Return
    transaction_multiversion_restore_register:
    Call RegisterPreviousTsf
    Pop $0
    StrCmp $0 "1" transaction_multiversion_restore_registry
        Push 0
        Return
    transaction_multiversion_restore_registry:
    Call RestorePreviousRegistry
    Pop $0
    StrCmp $0 "1" transaction_multiversion_restore_cleanup
        Push 0
        Return
    transaction_multiversion_restore_cleanup:
    RMDir /r "$INSTDIR"
    IfFileExists "$INSTDIR\*" transaction_recovery_failed
    StrCmp $TransactionDir $INSTDIR transaction_multiversion_restore_done
        RMDir /r "$TransactionDir"
        IfFileExists "$TransactionDir" transaction_recovery_failed
    transaction_multiversion_restore_done:
    Push 1
    Return

    transaction_remove_partial_install:
    Call UnregisterTransactionTsf
    Pop $0
    StrCmp $0 "1" transaction_restore_system_ime
        Push 0
        Return

    transaction_restore_system_ime:
    StrCmp $InstallTransactionFormat "4" transaction_restore_program_files
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
    StrCmp $TransactionDir "$StageDir" 0 transaction_cleanup_backup
        ClearErrors
        RMDir /r "$StageDir"
        IfErrors transaction_recovery_failed
        IfFileExists "$StageDir" 0 transaction_cleanup_backup
        Goto transaction_recovery_failed
    transaction_cleanup_backup:
    ClearErrors
    RMDir /r "$BackupDir"
    IfErrors transaction_recovery_failed
    IfFileExists "$BackupDir" 0 transaction_cleanup_done
        Goto transaction_recovery_failed
    transaction_cleanup_done:
    DetailPrint "已恢复先前的 CxxIME 安装状态。"
    Push 1
    Return

    transaction_recovery_failed:
    StrCpy $FailureMessage "无法恢复先前的 CxxIME 程序文件。"
    Push 0
FunctionEnd

Function FinalizeCommittedInstall
    ClearErrors
    RMDir /r "$INSTDIR\${ROLLBACK_DIR}"
    IfErrors finalize_committed_install_failed
    IfFileExists "$INSTDIR\${ROLLBACK_DIR}" 0 finalize_committed_cleanup_backup
    Goto finalize_committed_install_failed
    finalize_committed_cleanup_backup:
    ClearErrors
    RMDir /r "$BackupDir"
    IfErrors finalize_committed_install_failed
    RMDir /r "$StageDir"
    IfErrors finalize_committed_install_failed
    IfFileExists "$BackupDir" 0 finalize_committed_check_stage
    Goto finalize_committed_install_failed
    finalize_committed_check_stage:
    IfFileExists "$StageDir" 0 finalize_committed_cleanup_previous
    Goto finalize_committed_install_failed
    finalize_committed_cleanup_previous:
    StrCpy $PreviousInstallDir $PreviousVersionDir
    StrCpy $PreviousInstallFlat 0
    StrCmp $PreviousInstallDir $InstallBaseDir 0 +2
        StrCpy $PreviousInstallFlat 1
    Call CleanupPreviousInstall
    Pop $0
    StrCmp $0 "1" 0 finalize_committed_install_failed
    Call WriteInstallLayoutState
    Pop $0
    StrCmp $0 "1" 0 finalize_committed_install_failed
    ClearErrors
    Delete "$INSTDIR\${TRANSACTION_MARKER}"
    IfErrors finalize_committed_install_failed
    Delete "$INSTDIR\${TRANSACTION_TEMP}"
    Push 1
    Return

    finalize_committed_install_failed:
    StrCpy $FailureMessage "无法清理已完成安装留下的 CxxIME 文件。"
    Push 0
FunctionEnd

Function RecoverInterruptedInstall
    IfFileExists "$INSTDIR\${TRANSACTION_MARKER}" recover_target_transaction
    IfFileExists "$StageDir\${TRANSACTION_MARKER}" recover_stage_transaction
    Goto recover_check_registered_install

    recover_target_transaction:
        StrCpy $TransactionDir "$INSTDIR"
        Call RecoverTransaction
        Return

    recover_stage_transaction:
        StrCpy $TransactionDir "$StageDir"
        Call RecoverTransaction
        Return

    recover_check_registered_install:
    ${If} $MultiVersionInstall == 1
        ; An interrupted uninstall still belongs to the registered installation, not .next.
        IfFileExists "$PreviousInstallDir\${UNINSTALL_TRANSACTION_MARKER}" \
            recover_uninstall_transaction_pending
        IfFileExists "$PreviousInstallDir\${INSTALL_MARKER}" \
            recover_registered_install_complete recover_registered_transaction
        recover_registered_install_complete:
        Push $INSTDIR
        StrCpy $INSTDIR $PreviousInstallDir
        Call FinalizeCommittedInstall
        Pop $0
        Pop $INSTDIR
        StrCmp $0 "1" recover_check_uninstall_transaction
            Push 0
            Return
        recover_registered_transaction:
        IfFileExists "$PreviousInstallDir\${TRANSACTION_MARKER}" 0 \
            recover_check_uninstall_transaction
            StrCpy $INSTDIR "$PreviousInstallDir"
            StrCpy $TransactionDir "$PreviousInstallDir"
            Call RecoverTransaction
            Return
    ${EndIf}

    recover_check_uninstall_transaction:
    IfFileExists "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" \
        recover_uninstall_transaction_pending recover_check_committed_install
    recover_uninstall_transaction_pending:
        StrCpy $FailureMessage \
            "上一次 CxxIME 卸载尚未完成。请先重新运行已安装的卸载程序。"
        Push 0
        Return

    recover_check_committed_install:
    IfFileExists "$INSTDIR\${INSTALL_MARKER}" 0 recover_incomplete_install
        Call FinalizeCommittedInstall
        Return

    recover_incomplete_install:
    Goto recover_untracked_backup

    recover_untracked_backup:
    IfFileExists "$BackupDir" 0 recover_remove_uncommitted_stage
        StrCpy $FailureMessage \
            "检测到 CxxIME 备份目录，但没有有效的安装事务，无法安全恢复。"
        Push 0
        Return

    recover_remove_uncommitted_stage:
    ClearErrors
    RMDir /r "$StageDir"
    IfErrors recover_staged_cleanup_failed
    IfFileExists "$StageDir" 0 recover_remove_uncommitted_stage_done
        Goto recover_staged_cleanup_failed
    recover_remove_uncommitted_stage_done:
    Push 1
    Return

    recover_staged_cleanup_failed:
    StrCpy $FailureMessage "无法清理已完成安装留下的 CxxIME 文件。"
    Push 0
FunctionEnd
