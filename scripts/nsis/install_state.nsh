Function SnapshotPreviousState
    StrCpy $OldInstallAvailable $ExistingInstall
    StrCpy $OldTsfX64Present 0
    StrCpy $OldTsfX86Present 0
    StrCpy $OldTsfX64Registered 0
    StrCpy $OldTsfX86Registered 0
    StrCpy $OldUninstallPresent 0
    StrCpy $OldDisplayVersion ""
    StrCpy $OldRunPresent 0
    StrCpy $OldRunValue ""

    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 +2
        StrCpy $OldTsfX64Present 1
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 +2
        StrCpy $OldTsfX86Present 1

    ${If} $OldTsfX64Present == 1
        SetRegView 64
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${IfNot} ${Errors}
        ${AndIf} $0 == "$INSTDIR\cxxime_tsf_x64.dll"
            StrCpy $OldTsfX64Registered 1
        ${EndIf}
    ${EndIf}
    ${If} $OldTsfX86Present == 1
        SetRegView 32
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${IfNot} ${Errors}
        ${AndIf} $0 == "$INSTDIR\cxxime_tsf_x86.dll"
            StrCpy $OldTsfX86Registered 1
        ${EndIf}
    ${EndIf}
    SetRegView 64

    ClearErrors
    ReadRegStr $OldDisplayVersion HKLM "${UNINSTALL_KEY}" "DisplayVersion"
    ${IfNot} ${Errors}
        StrCpy $OldUninstallPresent 1
    ${EndIf}
    ClearErrors
    ReadRegStr $OldRunValue HKLM "${RUN_KEY}" "CxxIMEServer"
    ${IfNot} ${Errors}
        StrCpy $OldRunPresent 1
    ${EndIf}
FunctionEnd

Function BackupSystemIme
    StrCpy $SystemImeX64Present 0
    StrCpy $SystemImeX86Present 0
    RMDir /r "$StageDir\${ROLLBACK_DIR}"
    ClearErrors
    CreateDirectory "$StageDir\${ROLLBACK_DIR}"
    IfErrors backup_system_directory_failed

    IfFileExists "$WINDIR\Sysnative\cxxime.ime" 0 backup_system_x86
        System::Call 'kernel32::CopyFileW(\
            w "$WINDIR\Sysnative\cxxime.ime", \
            w "$StageDir\${ROLLBACK_DIR}\system-x64.ime", \
            i 0) i .r0'
        ${If} $0 == 0
            StrCpy $FailureMessage "无法备份已安装的 64 位传统 IME 模块。"
            Push 0
            Return
        ${EndIf}
        StrCpy $SystemImeX64Present 1
    backup_system_x86:
    IfFileExists "$SYSDIR\cxxime.ime" 0 backup_system_done
        System::Call 'kernel32::CopyFileW(\
            w "$SYSDIR\cxxime.ime", \
            w "$StageDir\${ROLLBACK_DIR}\system-x86.ime", \
            i 0) i .r0'
        ${If} $0 == 0
            StrCpy $FailureMessage "无法备份已安装的 32 位传统 IME 模块。"
            Push 0
            Return
        ${EndIf}
        StrCpy $SystemImeX86Present 1
    backup_system_done:
    Push 1
    Return

    backup_system_directory_failed:
    StrCpy $FailureMessage "无法创建 CxxIME 回滚目录。"
    Push 0
FunctionEnd

Function WriteTransactionState
    Delete "$StageDir\${TRANSACTION_TEMP}"
    ClearErrors
    FileOpen $0 "$StageDir\${TRANSACTION_TEMP}" w
    IfErrors transaction_state_write_failed
    FileWriteUTF16LE /BOM $0 "[transaction]$\r$\n"
    FileWriteUTF16LE $0 "format=2$\r$\n"
    FileWriteUTF16LE $0 "old_install_available=$OldInstallAvailable$\r$\n"
    FileWriteUTF16LE $0 "old_tsf_x64_present=$OldTsfX64Present$\r$\n"
    FileWriteUTF16LE $0 "old_tsf_x86_present=$OldTsfX86Present$\r$\n"
    FileWriteUTF16LE $0 "old_tsf_x64_registered=$OldTsfX64Registered$\r$\n"
    FileWriteUTF16LE $0 "old_tsf_x86_registered=$OldTsfX86Registered$\r$\n"
    FileWriteUTF16LE $0 "system_ime_x64_present=$SystemImeX64Present$\r$\n"
    FileWriteUTF16LE $0 "system_ime_x86_present=$SystemImeX86Present$\r$\n"
    FileWriteUTF16LE $0 "old_uninstall_present=$OldUninstallPresent$\r$\n"
    FileWriteUTF16LE $0 "old_display_version=$OldDisplayVersion$\r$\n"
    FileWriteUTF16LE $0 "old_run_present=$OldRunPresent$\r$\n"
    FileWriteUTF16LE $0 "old_run_value=$OldRunValue$\r$\n"
    IfErrors transaction_state_write_close_failed
    FileClose $0
    IfErrors transaction_state_write_failed
    IfFileExists "$StageDir\${TRANSACTION_TEMP}" transaction_state_commit \
        transaction_state_write_failed

    transaction_state_commit:
    System::Call 'kernel32::MoveFileExW(\
        w "$StageDir\${TRANSACTION_TEMP}", \
        w "$StageDir\${TRANSACTION_MARKER}", \
        i ${MOVEFILE_REPLACE_WRITE_THROUGH}) i .r0 ?e'
    Pop $1
    StrCmp $0 "0" transaction_state_commit_failed
    Push 1
    Return

    transaction_state_write_close_failed:
    FileClose $0
    transaction_state_write_failed:
    Delete "$StageDir\${TRANSACTION_TEMP}"
    StrCpy $FailureMessage "无法写入 CxxIME 安装事务。"
    Push 0
    Return

    transaction_state_commit_failed:
    Delete "$StageDir\${TRANSACTION_TEMP}"
    StrCpy $FailureMessage \
        "无法提交 CxxIME 安装事务（Win32 错误 $1）。"
    Push 0
FunctionEnd

Function RestoreSystemIme
    ${If} $SystemImeX64Present == 1
        IfFileExists "$TransactionDir\${ROLLBACK_DIR}\system-x64.ime" 0 restore_system_failed
        System::Call 'kernel32::CopyFileW(\
            w "$TransactionDir\${ROLLBACK_DIR}\system-x64.ime", \
            w "$WINDIR\Sysnative\cxxime.ime", \
            i 0) i .r0'
        StrCmp $0 "0" restore_system_failed
    ${Else}
        Delete "$WINDIR\Sysnative\cxxime.ime"
        IfFileExists "$WINDIR\Sysnative\cxxime.ime" restore_system_failed
    ${EndIf}
    ${If} $SystemImeX86Present == 1
        IfFileExists "$TransactionDir\${ROLLBACK_DIR}\system-x86.ime" 0 restore_system_failed
        System::Call 'kernel32::CopyFileW(\
            w "$TransactionDir\${ROLLBACK_DIR}\system-x86.ime", \
            w "$SYSDIR\cxxime.ime", \
            i 0) i .r0'
        StrCmp $0 "0" restore_system_failed
    ${Else}
        Delete "$SYSDIR\cxxime.ime"
        IfFileExists "$SYSDIR\cxxime.ime" restore_system_failed
    ${EndIf}
    Push 1
    Return

    restore_system_failed:
    StrCpy $FailureMessage "无法恢复先前的传统 IME 模块。"
    Push 0
FunctionEnd

Function CopyNewSystemIme
    System::Call 'kernel32::CopyFileW(\
        w "$INSTDIR\cxxime_ime_x64.ime", \
        w "$WINDIR\Sysnative\cxxime.ime", \
        i 0) i .r0'
    ${If} $0 == 0
        StrCpy $FailureMessage "无法安装 64 位传统 IME 模块。"
        Push 0
        Return
    ${EndIf}
    System::Call 'kernel32::CopyFileW(\
        w "$INSTDIR\cxxime_ime_x86.ime", \
        w "$SYSDIR\cxxime.ime", \
        i 0) i .r0'
    ${If} $0 == 0
        StrCpy $FailureMessage "无法安装 32 位传统 IME 模块。"
        Push 0
        Return
    ${EndIf}
    Push 1
FunctionEnd
