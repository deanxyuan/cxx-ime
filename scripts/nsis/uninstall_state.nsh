Function un.LoadTransactionState
    StrCpy $UninstallTransactionPhase ""
    StrCpy $UninstallSystemImeX64Present 0
    StrCpy $UninstallSystemImeX86Present 0
    StrCpy $UninstallTsfX64Registered 0
    StrCpy $UninstallTsfX86Registered 0
    ClearErrors
    ReadINIStr $0 "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" "transaction" "format"
    IfErrors un_transaction_state_invalid
    StrCmp $0 "2" 0 un_transaction_state_invalid
    ReadINIStr $UninstallTransactionPhase "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" \
        "transaction" "phase"
    ReadINIStr $UninstallSystemImeX64Present "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" \
        "transaction" "system_ime_x64_present"
    ReadINIStr $UninstallSystemImeX86Present "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" \
        "transaction" "system_ime_x86_present"
    ReadINIStr $UninstallTsfX64Registered "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" \
        "transaction" "tsf_x64_registered"
    ReadINIStr $UninstallTsfX86Registered "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" \
        "transaction" "tsf_x86_registered"
    StrCmp $UninstallTransactionPhase "prepared" un_transaction_phase_valid
    StrCmp $UninstallTransactionPhase "staged" un_transaction_phase_valid \
        un_transaction_state_invalid
    un_transaction_phase_valid:
    StrCmp $UninstallSystemImeX64Present "0" un_transaction_system_x64_valid
    StrCmp $UninstallSystemImeX64Present "1" un_transaction_system_x64_valid \
        un_transaction_state_invalid
    un_transaction_system_x64_valid:
    StrCmp $UninstallSystemImeX86Present "0" un_transaction_system_x86_valid
    StrCmp $UninstallSystemImeX86Present "1" un_transaction_system_x86_valid \
        un_transaction_state_invalid
    un_transaction_system_x86_valid:
    StrCmp $UninstallTsfX64Registered "0" un_transaction_tsf_x64_valid
    StrCmp $UninstallTsfX64Registered "1" un_transaction_tsf_x64_valid \
        un_transaction_state_invalid
    un_transaction_tsf_x64_valid:
    StrCmp $UninstallTsfX86Registered "0" un_transaction_tsf_x86_valid
    StrCmp $UninstallTsfX86Registered "1" un_transaction_tsf_x86_valid \
        un_transaction_state_invalid
    un_transaction_tsf_x86_valid:
    Push 1
    Return

    un_transaction_state_invalid:
    StrCpy $FailureMessage "The previous CxxIME uninstall transaction is incomplete or invalid."
    Push 0
FunctionEnd

Function un.WriteTransactionState
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}"
    ClearErrors
    FileOpen $0 "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}" w
    IfErrors un_write_transaction_failed
    FileWriteUTF16LE /BOM $0 "[transaction]$\r$\n"
    FileWriteUTF16LE $0 "format=2$\r$\n"
    FileWriteUTF16LE $0 "phase=$UninstallTransactionPhase$\r$\n"
    FileWriteUTF16LE $0 \
        "system_ime_x64_present=$UninstallSystemImeX64Present$\r$\n"
    FileWriteUTF16LE $0 \
        "system_ime_x86_present=$UninstallSystemImeX86Present$\r$\n"
    FileWriteUTF16LE $0 "tsf_x64_registered=$UninstallTsfX64Registered$\r$\n"
    FileWriteUTF16LE $0 "tsf_x86_registered=$UninstallTsfX86Registered$\r$\n"
    IfErrors un_write_transaction_close_failed
    FileClose $0
    IfErrors un_write_transaction_failed
    System::Call 'kernel32::MoveFileExW(\
        w "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}", \
        w "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}", \
        i ${MOVEFILE_REPLACE_WRITE_THROUGH}) i .r0 ?e'
    Pop $1
    StrCmp $0 "0" un_commit_transaction_failed
    Push 1
    Return

    un_write_transaction_close_failed:
    FileClose $0
    un_write_transaction_failed:
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}"
    StrCpy $FailureMessage "Failed to write the CxxIME uninstall transaction."
    Push 0
    Return

    un_commit_transaction_failed:
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}"
    StrCpy $FailureMessage \
        "Failed to commit the CxxIME uninstall transaction (Win32 error $1)."
    Push 0
FunctionEnd

Function un.MarkTransactionStaged
    StrCpy $UninstallTransactionPhase "staged"
    Call un.WriteTransactionState
FunctionEnd

Function un.SnapshotTsfRegistration
    StrCpy $UninstallTsfX64Registered 0
    StrCpy $UninstallTsfX86Registered 0

    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 un_snapshot_tsf_x86
        SetRegView 64
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${IfNot} ${Errors}
        ${AndIf} $0 == "$INSTDIR\cxxime_tsf_x64.dll"
            StrCpy $UninstallTsfX64Registered 1
        ${EndIf}
    un_snapshot_tsf_x86:
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 un_snapshot_tsf_done
        SetRegView 32
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${IfNot} ${Errors}
        ${AndIf} $0 == "$INSTDIR\cxxime_tsf_x86.dll"
            StrCpy $UninstallTsfX86Registered 1
        ${EndIf}
    un_snapshot_tsf_done:
    SetRegView 64
FunctionEnd

Function un.PrepareTransaction
    IfFileExists "$INSTDIR\${TRANSACTION_MARKER}" 0 un_prepare_check_existing_transaction
    IfFileExists "$INSTDIR\${INSTALL_MARKER}" 0 un_prepare_install_incomplete
        RMDir /r "$INSTDIR\${ROLLBACK_DIR}"
        Delete "$INSTDIR\${TRANSACTION_MARKER}"

    un_prepare_check_existing_transaction:
    IfFileExists "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}" 0 un_prepare_new_transaction
        Call un.LoadTransactionState
        Return

    un_prepare_new_transaction:
    RMDir /r "$UninstallRollbackDir"
    ClearErrors
    CreateDirectory "$UninstallRollbackDir"
    IfErrors un_prepare_transaction_failed
    StrCpy $UninstallSystemImeX64Present 0
    StrCpy $UninstallSystemImeX86Present 0
    StrCpy $UninstallTsfX64Registered 0
    StrCpy $UninstallTsfX86Registered 0
    StrCpy $UninstallTransactionPhase "prepared"

    IfFileExists "$WINDIR\Sysnative\cxxime.ime" 0 un_prepare_system_x86
        System::Call 'kernel32::CopyFileW(\
            w "$WINDIR\Sysnative\cxxime.ime", \
            w "$UninstallRollbackDir\system-x64.ime", \
            i 0) i .r0'
        StrCmp $0 "0" un_prepare_transaction_failed
        StrCpy $UninstallSystemImeX64Present 1
    un_prepare_system_x86:
    IfFileExists "$SYSDIR\cxxime.ime" 0 un_prepare_snapshot_registration
        System::Call 'kernel32::CopyFileW(\
            w "$SYSDIR\cxxime.ime", \
            w "$UninstallRollbackDir\system-x86.ime", \
            i 0) i .r0'
        StrCmp $0 "0" un_prepare_transaction_failed
        StrCpy $UninstallSystemImeX86Present 1

    un_prepare_snapshot_registration:
    Call un.SnapshotTsfRegistration
    Call un.WriteTransactionState
    Return

    un_prepare_transaction_failed:
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}"
    StrCpy $FailureMessage "Failed to create the CxxIME uninstall transaction."
    Push 0
    Return

    un_prepare_install_incomplete:
    StrCpy $FailureMessage \
        "A CxxIME installation is incomplete. Run setup again before uninstalling CxxIME."
    Push 0
FunctionEnd

Function un.UnregisterInstalledTsf
    StrCmp $UninstallTsfX86Registered "1" 0 un_unregister_installed_x64_path
    StrCpy $2 "$INSTDIR\cxxime_tsf_x86.dll"
    IfFileExists "$2" un_unregister_installed_x86 0
    StrCpy $2 "$UninstallRollbackDir\cxxime_tsf_x86.dll"
    un_unregister_installed_x86:
    IfFileExists "$2" 0 un_unregister_installed_x86_missing
        nsExec::ExecToStack '"$SYSDIR\regsvr32.exe" /u /s "$2"'
        Pop $0
        Pop $1
        StrCmp $0 "0" un_unregister_installed_x64_path
            StrCpy $FailureMessage "Failed to unregister the 32-bit TSF module."
            Push 0
            Return

    un_unregister_installed_x64_path:
    StrCmp $UninstallTsfX64Registered "1" 0 un_unregister_installed_done
    StrCpy $2 "$INSTDIR\cxxime_tsf_x64.dll"
    IfFileExists "$2" un_unregister_installed_x64 0
    StrCpy $2 "$UninstallRollbackDir\cxxime_tsf_x64.dll"
    un_unregister_installed_x64:
    IfFileExists "$2" 0 un_unregister_installed_x64_missing
        nsExec::ExecToStack '"$WINDIR\Sysnative\regsvr32.exe" /u /s "$2"'
        Pop $0
        Pop $1
        StrCmp $0 "0" un_unregister_installed_done
            StrCpy $FailureMessage "Failed to unregister the 64-bit TSF module."
            Push 0
            Return
    un_unregister_installed_done:
    Push 1
    Return

    un_unregister_installed_x86_missing:
    StrCpy $FailureMessage "The registered 32-bit TSF module is missing."
    Push 0
    Return

    un_unregister_installed_x64_missing:
    StrCpy $FailureMessage "The registered 64-bit TSF module is missing."
    Push 0
FunctionEnd

Function un.RemoveSystemIme
    Delete "$WINDIR\Sysnative\cxxime.ime"
    IfFileExists "$WINDIR\Sysnative\cxxime.ime" un_remove_system_ime_failed
    Delete "$SYSDIR\cxxime.ime"
    IfFileExists "$SYSDIR\cxxime.ime" un_remove_system_ime_failed
    Push 1
    Return

    un_remove_system_ime_failed:
    StrCpy $FailureMessage "Failed to remove the legacy IME modules."
    Push 0
FunctionEnd

Function un.RestoreSystemIme
    ${If} $UninstallSystemImeX64Present == 1
        IfFileExists "$UninstallRollbackDir\system-x64.ime" 0 un_restore_system_ime_failed
        System::Call 'kernel32::CopyFileW(\
            w "$UninstallRollbackDir\system-x64.ime", \
            w "$WINDIR\Sysnative\cxxime.ime", \
            i 0) i .r0'
        StrCmp $0 "0" un_restore_system_ime_failed
    ${Else}
        Delete "$WINDIR\Sysnative\cxxime.ime"
    ${EndIf}
    ${If} $UninstallSystemImeX86Present == 1
        IfFileExists "$UninstallRollbackDir\system-x86.ime" 0 un_restore_system_ime_failed
        System::Call 'kernel32::CopyFileW(\
            w "$UninstallRollbackDir\system-x86.ime", \
            w "$SYSDIR\cxxime.ime", \
            i 0) i .r0'
        StrCmp $0 "0" un_restore_system_ime_failed
    ${Else}
        Delete "$SYSDIR\cxxime.ime"
    ${EndIf}
    Push 1
    Return

    un_restore_system_ime_failed:
    StrCpy $FailureMessage "Failed to restore the legacy IME modules."
    Push 0
FunctionEnd
