Function SnapshotPreviousState
    StrCmp $MultiVersionInstall "1" 0 snapshot_state_dir_ready
        StrCpy $StateInstallDir "$PreviousInstallDir"
    snapshot_state_dir_ready:
    StrCpy $OldInstallAvailable $ExistingInstall
    ${If} $MultiVersionInstall == 1
        StrCpy $OldInstallAvailable 1
    ${EndIf}
    StrCpy $OldTsfX64Present 0
    StrCpy $OldTsfX86Present 0
    StrCpy $OldTsfX64Registered 0
    StrCpy $OldTsfX86Registered 0
    StrCpy $OldTipX64Present 0
    StrCpy $OldTipX86Present 0
    StrCpy $OldUninstallPresent 0
    StrCpy $OldDisplayVersion ""
    StrCpy $OldRunPresent 0
    StrCpy $OldRunValue ""
    StrCpy $OldPreviousInstallDir ""

    IfFileExists "$StateInstallDir\cxxime_tsf_x64.dll" 0 +2
        StrCpy $OldTsfX64Present 1
    IfFileExists "$StateInstallDir\cxxime_tsf_x86.dll" 0 +2
        StrCpy $OldTsfX86Present 1

    ${If} $OldTsfX64Present == 1
        SetRegView 64
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${IfNot} ${Errors}
        ${AndIf} $0 == "$StateInstallDir\cxxime_tsf_x64.dll"
            StrCpy $OldTsfX64Registered 1
        ${EndIf}
    ${EndIf}
    ${If} $OldTsfX86Present == 1
        SetRegView 32
        ClearErrors
        ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""
        ${IfNot} ${Errors}
        ${AndIf} $0 == "$StateInstallDir\cxxime_tsf_x86.dll"
            StrCpy $OldTsfX86Registered 1
        ${EndIf}
    ${EndIf}
    SetRegView 64
    Call QueryTipRegistration
    Pop $0
    StrCpy $OldTipX64Present $0
    SetRegView 32
    Call QueryTipRegistration
    Pop $0
    StrCpy $OldTipX86Present $0
    SetRegView 64
    ${If} $OldInstallAvailable == 0
        StrCpy $OldTipX64Present 0
        StrCpy $OldTipX86Present 0
    ${EndIf}

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
    ClearErrors
    ReadRegStr $OldPreviousInstallDir HKLM "${UNINSTALL_KEY}" "PreviousInstallLocation"
    ${If} ${Errors}
        StrCpy $OldPreviousInstallDir ""
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
    FileWriteUTF16LE $0 "format=4$\r$\n"
    FileWriteUTF16LE $0 "old_install_available=$OldInstallAvailable$\r$\n"
    FileWriteUTF16LE $0 "old_tsf_x64_present=$OldTsfX64Present$\r$\n"
    FileWriteUTF16LE $0 "old_tsf_x86_present=$OldTsfX86Present$\r$\n"
    FileWriteUTF16LE $0 "old_tsf_x64_registered=$OldTsfX64Registered$\r$\n"
    FileWriteUTF16LE $0 "old_tsf_x86_registered=$OldTsfX86Registered$\r$\n"
    FileWriteUTF16LE $0 "old_tip_x64_present=$OldTipX64Present$\r$\n"
    FileWriteUTF16LE $0 "old_tip_x86_present=$OldTipX86Present$\r$\n"
    FileWriteUTF16LE $0 "system_ime_x64_present=$SystemImeX64Present$\r$\n"
    FileWriteUTF16LE $0 "system_ime_x86_present=$SystemImeX86Present$\r$\n"
    FileWriteUTF16LE $0 "old_uninstall_present=$OldUninstallPresent$\r$\n"
    FileWriteUTF16LE $0 "old_display_version=$OldDisplayVersion$\r$\n"
    FileWriteUTF16LE $0 "old_run_present=$OldRunPresent$\r$\n"
    FileWriteUTF16LE $0 "old_run_value=$OldRunValue$\r$\n"
    FileWriteUTF16LE $0 "old_install_dir=$PreviousInstallDir$\r$\n"
    FileWriteUTF16LE $0 "old_previous_install_dir=$OldPreviousInstallDir$\r$\n"
    FileWriteUTF16LE $0 "server_was_running=$InitialServerWasRunning$\r$\n"
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

Function WriteInstallLayoutState
    ClearErrors
    CreateDirectory "$InstallBaseDir\update"
    IfErrors install_layout_state_failed
    Delete "$InstallBaseDir\${INSTALL_STATE_TEMP}"
    ClearErrors
    FileOpen $0 "$InstallBaseDir\${INSTALL_STATE_TEMP}" w
    IfErrors install_layout_state_failed
    FileWriteUTF16LE /BOM $0 "[install]$\r$\n"
    FileWriteUTF16LE $0 "format=1$\r$\n"
    FileWriteUTF16LE $0 "phase=committed$\r$\n"
    FileWriteUTF16LE $0 "version=${VERSION}$\r$\n"
    FileWriteUTF16LE $0 "active=$INSTDIR$\r$\n"
    FileWriteUTF16LE $0 "previous=$PreviousInstallDir$\r$\n"
    FileClose $0
    IfErrors install_layout_state_failed
    System::Call 'kernel32::MoveFileExW(\
        w "$InstallBaseDir\${INSTALL_STATE_TEMP}", \
        w "$InstallBaseDir\${INSTALL_STATE_MARKER}", \
        i ${MOVEFILE_REPLACE_WRITE_THROUGH}) i .r0 ?e'
    Pop $1
    StrCmp $0 "0" install_layout_state_commit_failed
    Push 1
    Return

    install_layout_state_failed:
    Delete "$InstallBaseDir\${INSTALL_STATE_TEMP}"
    StrCpy $FailureMessage "无法写入 CxxIME 安装状态。"
    Push 0
    Return
    install_layout_state_commit_failed:
    Delete "$InstallBaseDir\${INSTALL_STATE_TEMP}"
    StrCpy $FailureMessage "无法提交 CxxIME 安装状态（Win32 错误 $1）。"
    Push 0
FunctionEnd

Function CleanupPreviousInstall
    StrCmp $PreviousInstallDir "" cleanup_previous_install_done
    StrCmp $PreviousInstallDir $INSTDIR cleanup_previous_install_invalid
    StrCmp $PreviousInstallFlat "1" cleanup_previous_install_files
    StrLen $0 "$InstallBaseDir\"
    StrCpy $1 "$PreviousInstallDir" $0
    StrCmp $1 "$InstallBaseDir\" cleanup_previous_install_files \
        cleanup_previous_install_invalid

    cleanup_previous_install_files:
    Delete /REBOOTOK "$PreviousInstallDir\cxxime_tsf_x64.dll"
    Delete /REBOOTOK "$PreviousInstallDir\cxxime_tsf_x86.dll"
    Delete /REBOOTOK "$PreviousInstallDir\cxxime_ime_x64.ime"
    Delete /REBOOTOK "$PreviousInstallDir\cxxime_ime_x86.ime"
    Delete /REBOOTOK "$PreviousInstallDir\cxxime-resources.dll"
    Delete /REBOOTOK "$PreviousInstallDir\cxxime-server.exe"
    Delete /REBOOTOK "$PreviousInstallDir\cxxime-settings.exe"
    Delete /REBOOTOK "$PreviousInstallDir\collect_diagnostics.ps1"
    Delete /REBOOTOK "$PreviousInstallDir\cxxime-ime-host-probe-x64.exe"
    Delete /REBOOTOK "$PreviousInstallDir\cxxime-ime-host-probe-x86.exe"
    Delete /REBOOTOK "$PreviousInstallDir\export_host_trace.ps1"
    Delete /REBOOTOK "$PreviousInstallDir\license.txt"
    Delete /REBOOTOK "$PreviousInstallDir\THIRD_PARTY_NOTICES.txt"
    Delete /REBOOTOK "$PreviousInstallDir\uninstall.exe"
    Delete /REBOOTOK "$PreviousInstallDir\${INSTALL_MARKER}"
    Delete /REBOOTOK "$PreviousInstallDir\${TRANSACTION_MARKER}"
    Delete /REBOOTOK "$PreviousInstallDir\${TRANSACTION_TEMP}"
    Delete /REBOOTOK "$PreviousInstallDir\data\default.json"
    Delete /REBOOTOK "$PreviousInstallDir\data\settings_presets.json"
    Delete /REBOOTOK "$PreviousInstallDir\data\themes.json"
    Delete /REBOOTOK "$PreviousInstallDir\data\punctuation.json"
    Delete /REBOOTOK "$PreviousInstallDir\data\symbols.json"
    Delete /REBOOTOK "$PreviousInstallDir\data\dictionary_manifest.json"
    Delete /REBOOTOK "$PreviousInstallDir\data\pinyin.dict.bin"
    Delete /REBOOTOK "$PreviousInstallDir\data\pinyin.dict.idx"
    Delete /REBOOTOK "$PreviousInstallDir\data\pinyin.spellings.bin"
    Delete /REBOOTOK "$PreviousInstallDir\data\pinyin.topn.bin"
    Delete /REBOOTOK "$PreviousInstallDir\data\pinyin.reverse.idx"
    Delete /REBOOTOK "$PreviousInstallDir\data\wubi86.dict.bin"
    Delete /REBOOTOK "$PreviousInstallDir\data\wubi86.dict.idx"
    Delete /REBOOTOK "$PreviousInstallDir\data\wubi86.reverse.idx"
    Delete /REBOOTOK "$PreviousInstallDir\licenses\rime-ice-GPL-3.0.txt"
    RMDir /REBOOTOK "$PreviousInstallDir\data"
    RMDir /REBOOTOK "$PreviousInstallDir\licenses"
    ${If} $PreviousInstallFlat == 0
        RMDir /REBOOTOK "$PreviousInstallDir"
    ${EndIf}
    IfFileExists "$PreviousInstallDir\cxxime_tsf_x64.dll" 0 +2
        SetRebootFlag true
    Push 1
    Return

    cleanup_previous_install_invalid:
    DetailPrint "旧版本目录不在 CxxIME 产品目录内，已跳过自动清理。"
    Push 1
    Return
    cleanup_previous_install_done:
    Push 1
FunctionEnd

Function RestoreSystemIme
    Delete "$InstallBaseDir\${SYSTEM_IME_UPDATE_MARKER}"
    Delete "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}"
    Delete "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}"
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

Function PrepareSystemImeUpdate
    WriteINIStr "$InstallBaseDir\${SYSTEM_IME_UPDATE_MARKER}" "update" "source" "$INSTDIR"
    IfErrors prepare_system_ime_failed
    Push 1
    Return

    prepare_system_ime_failed:
    StrCpy $FailureMessage "无法记录 CxxIME 系统 IME 更新状态。"
    Push 0
FunctionEnd

Function CancelPendingSystemImeUpdate
    Delete "$InstallBaseDir\${SYSTEM_IME_UPDATE_MARKER}"
    Delete "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}"
    Delete "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}"
FunctionEnd

Function CopyNewSystemIme
    Delete "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}"
    Delete "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}"
    System::Call 'kernel32::CopyFileW(\
        w "$INSTDIR\cxxime_ime_x64.ime", \
        w "$WINDIR\Sysnative\cxxime.ime", \
        i 0) i .r0'
    ${If} $0 == 0
        System::Call 'kernel32::CopyFileW(\
            w "$INSTDIR\cxxime_ime_x64.ime", \
            w "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}", \
            i 0) i .r0'
        StrCmp $0 "0" install_system_ime_x64_failed
        System::Call 'kernel32::MoveFileExW(\
            w "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}", \
            w "$WINDIR\Sysnative\cxxime.ime", \
            i ${MOVEFILE_REPLACE_DELAY_UNTIL_REBOOT}) i .r0 ?e'
        StrCmp $0 "0" install_system_ime_x64_failed
        SetRebootFlag true
    ${EndIf}
    System::Call 'kernel32::CopyFileW(\
        w "$INSTDIR\cxxime_ime_x86.ime", \
        w "$SYSDIR\cxxime.ime", \
        i 0) i .r0'
    ${If} $0 == 0
        System::Call 'kernel32::CopyFileW(\
            w "$INSTDIR\cxxime_ime_x86.ime", \
            w "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}", \
            i 0) i .r0'
        StrCmp $0 "0" install_system_ime_x86_failed
        System::Call 'kernel32::MoveFileExW(\
            w "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}", \
            w "$SYSDIR\cxxime.ime", \
            i ${MOVEFILE_REPLACE_DELAY_UNTIL_REBOOT}) i .r0 ?e'
        StrCmp $0 "0" install_system_ime_x86_failed
        SetRebootFlag true
    ${EndIf}
    Delete "$InstallBaseDir\${SYSTEM_IME_UPDATE_MARKER}"
    IfErrors install_system_ime_marker_failed
    Push 1
    Return

    install_system_ime_marker_failed:
    StrCpy $FailureMessage "无法完成 CxxIME 系统 IME 更新状态。"
    Push 0
    Return
    install_system_ime_x64_failed:
    Delete "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}"
    StrCpy $FailureMessage "无法安装 64 位传统 IME 模块。"
    Push 0
    Return
    install_system_ime_x86_failed:
    Delete "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}"
    StrCpy $FailureMessage "无法安装 32 位传统 IME 模块。"
    Push 0
FunctionEnd
