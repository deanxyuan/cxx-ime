Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "Win\WinError.nsh"
!include "x64.nsh"

!define PRODUCT "CxxIME"
!define PUBLISHER "CxxIME Contributors"
!define CLSID "{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"
!define UNINSTALL_KEY "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CxxIME"
!define RUN_KEY "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
!define TSF_INPROC_KEY "SOFTWARE\Classes\CLSID\${CLSID}\InprocServer32"
!define TSF_TIP_KEY "SOFTWARE\Microsoft\CTF\TIP\${CLSID}"
!define INSTALL_MARKER ".cxxime-install-complete"
!define INSTALL_STATE_MARKER ".cxxime-install-state"
!define INSTALL_STATE_TEMP ".cxxime-install-state.tmp"
!define TRANSACTION_MARKER ".cxxime-install-transaction"
!define TRANSACTION_TEMP ".cxxime-install-transaction.tmp"
!define RUNTIME_MARKER ".cxxime-install-runtime"
!define RUNTIME_TEMP ".cxxime-install-runtime.tmp"
!define SYSTEM_IME_X64_PENDING ".cxxime-ime-x64.pending"
!define SYSTEM_IME_X86_PENDING ".cxxime-ime-x86.pending"
!define SYSTEM_IME_UPDATE_MARKER ".cxxime-ime-update"
!define ROLLBACK_DIR ".cxxime-rollback"
!define UNINSTALL_TRANSACTION_MARKER ".cxxime-uninstall-transaction"
!define UNINSTALL_TRANSACTION_TEMP ".cxxime-uninstall-transaction.tmp"
!define UNINSTALL_ROLLBACK_DIR ".cxxime-uninstall-rollback"
!define UNINSTALL_DEFERRED_MARKER ".cxxime-uninstall-pending"
!define MOVEFILE_REPLACE_WRITE_THROUGH 0x9
!define MOVEFILE_DELAY_UNTIL_REBOOT 0x4
!define MOVEFILE_REPLACE_DELAY_UNTIL_REBOOT 0x5

!ifndef VERSION
    !error "VERSION must be provided by package.py"
!endif
!ifndef VERSION_NUMERIC
    !error "VERSION_NUMERIC must be provided by package.py"
!endif

Name "${PRODUCT} ${VERSION}"
!ifdef HOST_DIAGNOSTICS
    OutFile "cxxime-v${VERSION}-host-diag-setup.exe"
!else
    OutFile "cxxime-v${VERSION}-setup.exe"
!endif
InstallDir "$PROGRAMFILES\CxxIME"
RequestExecutionLevel admin
SetCompressor lzma
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VERSION_NUMERIC}"
VIAddVersionKey /LANG=2052 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=2052 "FileDescription" "CxxIME 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${VERSION_NUMERIC}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Copyright (c) 2026 CxxIME Contributors"
VIAddVersionKey /LANG=2052 "ProductName" "${PRODUCT}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${VERSION}"

!define MUI_ICON "cxxime.ico"
!define MUI_UNICON "cxxime.ico"

Var LaunchSettings
Var ExistingInstall
Var RegisteredInstallDir
Var StageDir
Var BackupDir
Var TransactionDir
Var InstallTransactionFormat
Var LockReportPath
Var LockReportText
Var LockResult
Var LockPromptOptions
Var FailureMessage
Var OldInstallAvailable
Var OldTsfX64Present
Var OldTsfX86Present
Var OldTsfX64Registered
Var OldTsfX86Registered
Var OldTipX64Present
Var OldTipX86Present
Var SystemImeX64Present
Var SystemImeX86Present
Var OldUninstallPresent
Var OldDisplayVersion
Var OldRunPresent
Var OldRunValue
Var ServerWasRunning
Var InitialServerWasRunning
Var TransactionServerWasRunning
Var ServerRestartResult
Var ServerStopResult
Var ServerProcessId
Var InstallStateVerified
Var InstallBaseHandle
Var InstallBaseDir
Var PreviousInstallDir
Var OldPreviousInstallDir
Var MultiVersionInstall
Var ActiveServerDir
Var StateInstallDir
Var RegistryInstallDir
Var PreviousVersionDir
Var InstallTargetDir
Var InstallTargetPrepared
Var PreviousInstallFlat
Var UninstallRemoveUserData
Var UninstallRemoveUserDataCheckbox
Var UninstallUserDataDir
Var UninstallUserDataDirSuffix
Var UninstallServerWasRunning
Var UninstallServerStopResult
Var UninstallDeferred
Var UninstallDeferredResume
Var UninstallRollbackDir
Var UninstallTransactionPhase
Var UninstallSystemImeX64Present
Var UninstallSystemImeX86Present
Var UninstallTsfX64Registered
Var UninstallTsfX86Registered

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE ValidateInstallDirectory
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
Page custom FinishPage FinishPageLeave
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
UninstPage custom un.UserDataPage un.UserDataPageLeave
!insertmacro MUI_UNPAGE_INSTFILES
!define MUI_UNTEXT_FINISH_INFO_REBOOT \
    "CxxIME 已停用。请重新启动 Windows，以删除仍在使用的文件并完成卸载。"
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"

!include "nsis\setup.nsh"
!include "nsis\install_recovery.nsh"
!include "nsis\install_locks.nsh"
!include "nsis\install_state.nsh"
!include "nsis\install_tsf.nsh"

Section "Install"
    SetRegView 64
    SetShellVarContext all
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=cxxime-installer-helper.exe "cxxime-installer-helper.exe"

    Call PrepareInstallTarget
    Call SetTransactionPaths
    Call CheckFreshInstallBase
    Pop $0
    StrCmp $0 "1" install_base_contents_ready
        Goto install_failed_untrusted_base
    install_base_contents_ready:
    Call SecureInstallBase
    Pop $0
    StrCmp $0 "1" install_base_ready
        Goto install_failed_untrusted_base
    install_base_ready:
    Call CaptureServerState
    Pop $0
    StrCmp $0 "1" runtime_snapshot_ready
        Goto install_failed_before_swap
    runtime_snapshot_ready:
    Call ReleaseInputProcessor
    Call StopServer
    StrCmp $ServerStopResult "0" install_server_stopped
        StrCpy $FailureMessage "无法确认 CxxIME 后台已终止，未继续覆盖文件。"
        Goto install_failed_before_swap
    install_server_stopped:
    Call CheckInstallLocks
    Call RecoverInterruptedInstall
    Pop $0
    StrCmp $0 "1" install_recovery_ready
        StrCmp $FailureMessage "" 0 install_failed_recovery
        StrCpy $FailureMessage "无法安全恢复上一次未完成的 CxxIME 安装。"
        Goto install_failed_recovery

    install_recovery_ready:
    ${If} $InitialServerWasRunning == 0
    ${AndIf} $TransactionServerWasRunning == 1
        StrCpy $InitialServerWasRunning 1
    ${EndIf}
    StrCpy $ServerWasRunning $InitialServerWasRunning
    Call RefreshInstallLayoutAfterRecovery
    Call SetTransactionPaths
    Call RecoverPendingSystemIme
    Pop $0
    StrCmp $0 "1" install_system_ime_recovery_ready
        Goto install_failed_before_swap
    install_system_ime_recovery_ready:
    Call CheckPreviousVersionLimit
    Pop $0
    StrCmp $0 "1" install_previous_version_ready
        Goto install_failed_before_swap
    install_previous_version_ready:
    Call CheckInstallDirectory
    Pop $0
    StrCmp $0 "1" install_directory_checked
        Goto install_failed_before_swap

    install_directory_checked:
    Call PrepareInstallTarget
    Call SetTransactionPaths
    Call SnapshotPreviousState

    ClearErrors
    RMDir "$StageDir"
    IfFileExists "$StageDir" 0 install_stage_path_ready
        StrCpy $FailureMessage "CxxIME 更新目录中仍有未完成的安装文件。"
        Goto install_failed_before_swap
    install_stage_path_ready:
    CreateDirectory "$StageDir"
    IfErrors 0 install_stage_directory_ready
        StrCpy $FailureMessage "无法创建 CxxIME 更新目录。"
        Goto install_failed_before_swap
    install_stage_directory_ready:

    SetOutPath "$StageDir"
    File "cxxime_tsf_x64.dll"
    File "cxxime_tsf_x86.dll"
    File "cxxime_ime_x64.ime"
    File "cxxime_ime_x86.ime"
    File "cxxime-resources.dll"
    File "cxxime-server.exe"
    File "cxxime-settings.exe"
    File "collect_diagnostics.ps1"
    File "license.txt"
    File "THIRD_PARTY_NOTICES.txt"

    SetOutPath "$StageDir\licenses"
    File "licenses\rime-ice-GPL-3.0.txt"

    !ifdef HOST_DIAGNOSTICS
        SetOutPath "$StageDir"
        File "cxxime-ime-host-probe-x64.exe"
        File "cxxime-ime-host-probe-x86.exe"
        File "export_host_trace.ps1"
    !endif

    !ifdef FAST
        SetCompress off
    !endif
    SetOutPath "$StageDir\data"
    File "data\default.json"
    File "data\settings_presets.json"
    File "data\themes.json"
    File "data\punctuation.json"
    File "data\symbols.json"
    File "data\dictionary_manifest.json"
    File "data\pinyin.dict.bin"
    File "data\pinyin.dict.idx"
    File "data\pinyin.spellings.bin"
    File "data\pinyin.topn.bin"
    File "data\pinyin.reverse.idx"
    File "data\wubi86.dict.bin"
    File "data\wubi86.dict.idx"
    File "data\wubi86.reverse.idx"
    !ifdef FAST
        SetCompress auto
    !endif

    WriteUninstaller "$StageDir\uninstall.exe"
    IfErrors 0 install_stage_ready
        StrCpy $FailureMessage "无法解压 CxxIME 安装文件。"
        Goto install_failed_before_swap

    install_stage_ready:
    Call BackupSystemIme
    Pop $0
    StrCmp $0 "1" install_write_transaction
        Goto install_failed_before_swap

    install_write_transaction:
    Call WriteTransactionState
    Pop $0
    StrCmp $0 "1" install_transaction_ready
        Goto install_failed_before_swap

    install_transaction_ready:
    SetOutPath "$PLUGINSDIR"
    ${If} $MultiVersionInstall == 0
    ${AndIf} $ExistingInstall == 1
        IfFileExists "$BackupDir\*" 0 install_backup_path_ready
            StrCpy $FailureMessage "无法准备 CxxIME 备份目录。"
            Goto install_failed_after_transaction
        install_backup_path_ready:
        ClearErrors
        Rename "$INSTDIR" "$BackupDir"
        IfErrors 0 install_backup_ready
            StrCpy $FailureMessage "无法备份已安装的 CxxIME 版本。"
            Goto install_failed_after_transaction
        install_backup_ready:
        ${If} $MultiVersionInstall == 0
            Call UnregisterPreviousTsf
            Pop $0
            StrCmp $0 "1" install_swap_stage
                Goto install_failed_after_transaction
        ${EndIf}
    ${Else}
        RMDir "$INSTDIR"
    ${EndIf}

    install_swap_stage:
    ClearErrors
    Rename "$StageDir" "$INSTDIR"
    IfErrors 0 install_stage_swapped
        StrCpy $FailureMessage "无法启用新的 CxxIME 文件。"
        Goto install_failed_after_transaction

    install_stage_swapped:
    Call RegisterNewTsf
    Pop $0
    StrCmp $0 "1" install_write_registry
        Goto install_failed_after_transaction

    install_write_registry:
    Call WriteInstallationRegistry
    Pop $0
    StrCmp $0 "1" install_start_new_server
        Goto install_failed_after_transaction

    install_start_new_server:
    Call StartNewServer
    Pop $0
    StrCmp $0 "1" install_prepare_system_ime
        Goto install_failed_after_transaction

    install_prepare_system_ime:
    Call PrepareSystemImeUpdate
    Pop $0
    StrCmp $0 "1" install_write_marker
        Goto install_failed_after_transaction

    install_write_marker:
    Call WriteInstallMarker
    Pop $0
    StrCmp $0 "1" install_commit
        Goto install_failed_after_transaction

    install_commit:
    ClearErrors
    Delete "$INSTDIR\${TRANSACTION_MARKER}"
    IfErrors install_failed_after_transaction
    Call CopyNewSystemIme
    Pop $0
    StrCmp $0 "1" install_system_ime_committed
        IfSilent install_system_ime_warning_silent
            MessageBox MB_ICONEXCLAMATION \
                "CxxIME ${VERSION} 已安装，但系统 IME 模块未能完成更新。$\r$\n$\r$\n$FailureMessage$\r$\n$\r$\n\
                安装状态已保留，后续运行安装程序时会再次尝试。"
        install_system_ime_warning_silent:
        DetailPrint "$FailureMessage"
    install_system_ime_committed:
    IfFileExists "$INSTDIR\${ROLLBACK_DIR}" 0 install_commit_cleanup_backup
        ClearErrors
        RMDir /r "$INSTDIR\${ROLLBACK_DIR}"
        IfFileExists "$INSTDIR\${ROLLBACK_DIR}" 0 install_commit_cleanup_backup
            DetailPrint "安装已提交，但未能删除回滚数据，将在后续安装时再次清理。"
    install_commit_cleanup_backup:
    IfFileExists "$BackupDir" 0 install_commit_delete_runtime
        ClearErrors
        RMDir /r "$BackupDir"
        IfFileExists "$BackupDir" 0 install_commit_delete_runtime
            DetailPrint "安装已提交，但未能删除备份目录，将在后续安装时再次清理。"
    install_commit_delete_runtime:
    Call CleanupPreviousInstall
    Pop $0
    StrCmp $0 "1" install_old_version_removed
        DetailPrint "$FailureMessage"
    install_old_version_removed:
    Call WriteInstallLayoutState
    Pop $0
    StrCmp $0 "1" install_layout_state_written
        StrCpy $LaunchSettings 0
        SetErrorLevel 1
        IfSilent install_layout_state_failed_silent
            MessageBox MB_ICONSTOP \
                "CxxIME ${VERSION} 文件已安装，但无法完成安装状态记录。$\r$\n$\r$\n$FailureMessage$\r$\n$\r$\n请重新运行安装程序完成恢复。"
        install_layout_state_failed_silent:
        DetailPrint "$FailureMessage"
        Abort
    install_layout_state_written:
    CreateDirectory "$PROFILE\cxxime"
    IfFileExists "$PROFILE\cxxime\default.json" install_user_config_ready
        CopyFiles /SILENT /FILESONLY "$INSTDIR\data\default.json" "$PROFILE\cxxime"
    install_user_config_ready:
    Call CreateInstallShortcuts
    DetailPrint "CxxIME ${VERSION} 安装已完成。"
    Goto install_done

    install_failed_untrusted_base:
    StrCmp $InstallBaseHandle "0" install_untrusted_base_handle_closed
    StrCmp $InstallBaseHandle "-1" install_untrusted_base_handle_closed
        System::Call 'kernel32::CloseHandle(p $InstallBaseHandle)'
        StrCpy $InstallBaseHandle 0
    install_untrusted_base_handle_closed:
    IfSilent install_untrusted_base_silent
        MessageBox MB_ICONSTOP "$FailureMessage"
    install_untrusted_base_silent:
    DetailPrint "$FailureMessage"
    SetErrorLevel 1
    Abort

    install_failed_before_swap:
    StrCpy $InstallStateVerified 1
    ClearErrors
    RMDir /r "$StageDir"
    IfErrors install_failed_before_swap_cleanup_failed
    IfFileExists "$StageDir" 0 install_failed_before_swap_cleanup_done
    install_failed_before_swap_cleanup_failed:
    StrCpy $InstallStateVerified 0
    StrCpy $FailureMessage "$FailureMessage$\r$\n$\r$\n无法清理安装暂存目录，未恢复启动后台。"
    Goto install_failed_before_swap_report_ready
    install_failed_before_swap_cleanup_done:
    Call RestartInstalledServer
    Call CleanupRuntimeSnapshotAfterServerRestore
    StrCmp $ServerRestartResult "2" 0 install_failed_before_swap_report_ready
        StrCpy $FailureMessage "$FailureMessage$\r$\n$\r$\nCxxIME 后台未能自动恢复，请检查占用进程或手动启动 CxxIME。"
    install_failed_before_swap_report_ready:
    IfSilent install_failed_silent
        MessageBox MB_ICONSTOP "$FailureMessage$\r$\n$\r$\n已安装的 CxxIME 文件未发生变化。"
        Goto install_failed_abort

    install_failed_after_transaction:
    StrCpy $InstallStateVerified 0
    nsExec::Exec '"$PLUGINSDIR\cxxime-installer-helper.exe" force-stop-server "$INSTDIR\cxxime-server.exe"'
    Pop $0
    Call RollbackInstall
    Pop $0
    StrCmp $0 "1" install_rollback_complete
        StrCpy $FailureMessage \
            "$FailureMessage$\r$\n$\r$\n自动回滚未能完成。请重新运行安装程序后再使用 CxxIME。"
        Goto install_failed_silent_or_message
    install_rollback_complete:
        Call VerifyRestoredInstall
        Pop $0
        StrCmp $0 "1" install_rollback_verified
            Goto install_failed_silent_or_message
        install_rollback_verified:
        StrCpy $FailureMessage "$FailureMessage$\r$\n$\r$\n已恢复 CxxIME 安装前的状态。"
        Goto install_failed_silent_or_message

    install_failed_recovery:
        StrCpy $InstallStateVerified 0
        Goto install_failed_silent_or_message

    install_failed_silent_or_message:
    Call RestartInstalledServer
    Call CleanupRuntimeSnapshotAfterServerRestore
    StrCmp $ServerRestartResult "2" 0 install_failed_restart_report_ready
        StrCpy $FailureMessage "$FailureMessage$\r$\n$\r$\nCxxIME 后台未能自动恢复，请检查占用进程或手动启动 CxxIME。"
    install_failed_restart_report_ready:
    IfSilent install_failed_silent
        MessageBox MB_ICONSTOP "$FailureMessage"
        Goto install_failed_abort

    install_failed_silent:
    DetailPrint "$FailureMessage"
    install_failed_abort:
    SetErrorLevel 1
    Abort

    install_done:
    StrCmp $InstallBaseHandle "0" install_base_handle_closed
        System::Call 'kernel32::CloseHandle(p $InstallBaseHandle)'
        StrCpy $InstallBaseHandle 0
    install_base_handle_closed:
SectionEnd

!include "nsis\uninstall_locks.nsh"
!include "nsis\uninstall_state.nsh"
!include "nsis\uninstall_files.nsh"
!include "nsis\uninstall_deferred.nsh"

Section "Uninstall"
    SetRegView 64
    SetShellVarContext all
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=cxxime-installer-helper.exe "cxxime-installer-helper.exe"
    StrCpy $LockReportPath "$PLUGINSDIR\cxxime-locks.txt"

    Call un.ReleaseInputProcessor
    Call un.StopServer
    StrCmp $UninstallServerStopResult "0" un_server_stopped
        StrCpy $FailureMessage "无法确认 CxxIME 后台已终止，卸载已停止。"
        Call un.FailAndRestart
    un_server_stopped:
    StrCmp $UninstallDeferredResume "1" un_deferred_resume
    Call un.CheckFileLocks
    StrCmp $UninstallDeferred "1" un_deferred_prepare
    Call un.PrepareTransaction
    Pop $0
    StrCmp $0 "1" un_transaction_ready
        Call un.FailAndRestart

    un_transaction_ready:
    StrCmp $UninstallTransactionPhase "staged" un_program_files_staged
    Call un.UnregisterInstalledTsf
    Pop $0
    StrCmp $0 "1" un_tsf_unregistered
        Goto un_rollback_failure

    un_tsf_unregistered:
    Call un.RemoveSystemIme
    Pop $0
    StrCmp $0 "1" un_system_ime_removed
        Goto un_rollback_failure

    un_system_ime_removed:
    Call un.StageInstalledFiles
    Pop $0
    StrCmp $0 "1" un_mark_files_staged
        Goto un_rollback_failure

    un_mark_files_staged:
    Call un.MarkTransactionStaged
    Pop $0
    StrCmp $0 "1" un_program_files_staged
        Goto un_rollback_failure

    un_program_files_staged:
    Call un.DeleteStagedFiles
    Pop $0
    StrCmp $0 "1" un_remove_registry
        Call un.FailIncomplete

    un_deferred_prepare:
    Call un.PrepareTransaction
    Pop $0
    StrCmp $0 "1" un_deferred_unregister
        Call un.FailAndRestart

    un_deferred_unregister:
    Call un.UnregisterInstalledTsf
    Pop $0
    StrCmp $0 "1" un_deferred_schedule
        Goto un_rollback_failure

    un_deferred_resume:
    un_deferred_schedule:
    Call un.BeginDeferredUninstall
    Pop $0
    StrCmp $0 "1" un_remove_registry
        Call un.FailDeferred

    un_rollback_failure:
    Call un.RollbackTransaction
    Pop $0
    StrCmp $0 "1" un_rollback_complete
        StrCpy $FailureMessage \
            "$FailureMessage$\r$\n$\r$\n自动回滚未能完成。请重新运行卸载程序后再使用 CxxIME。"
        Call un.FailAndRestart
    un_rollback_complete:
    StrCpy $FailureMessage "$FailureMessage$\r$\n$\r$\n已恢复卸载前的 CxxIME 状态。"
    Call un.FailAndRestart

    un_remove_registry:
    DeleteRegValue HKLM "${RUN_KEY}" "CxxIMEServer"
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
    DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\${CLSID}"
    DeleteRegKey HKLM "SOFTWARE\Microsoft\CTF\TIP\${CLSID}"
    SetRegView 32
    DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\${CLSID}"
    DeleteRegKey HKLM "SOFTWARE\Microsoft\CTF\TIP\${CLSID}"
    SetRegView 64
    ClearErrors
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "DisplayName"
    IfErrors un_uninstall_registry_removed
        StrCpy $FailureMessage \
            "无法删除 CxxIME 卸载注册表项。请重新运行卸载程序。"
        StrCmp $UninstallDeferred "1" 0 +2
            Call un.FailDeferred
        Call un.FailIncomplete
    un_uninstall_registry_removed:
    ClearErrors
    ReadRegStr $0 HKLM "${RUN_KEY}" "CxxIMEServer"
    IfErrors un_run_registry_removed
        StrCpy $FailureMessage \
            "无法删除 CxxIME 启动注册表项。请重新运行卸载程序。"
        StrCmp $UninstallDeferred "1" 0 +2
            Call un.FailDeferred
        Call un.FailIncomplete
    un_run_registry_removed:

    RMDir /r "$SMPROGRAMS\CxxIME"
    StrCmp $UninstallDeferred "1" un_commit_deferred
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}"
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    IfFileExists "$INSTDIR\*" 0 un_program_files_removed
        DetailPrint "$INSTDIR 中仍有无法识别的文件。"
    un_program_files_removed:
    Goto un_remove_user_data

    un_commit_deferred:
    Call un.CommitDeferredUninstall
    Pop $0
    StrCmp $0 "1" un_remove_user_data
        Call un.FailDeferred

    un_remove_user_data:
    ${If} $PreviousVersionDir != ""
        Push "$PreviousVersionDir"
        Call un.CleanupKnownVersion
    ${EndIf}
    Push "$InstallBaseDir\update"
    Call un.CleanupKnownVersion
    Delete /REBOOTOK "$InstallBaseDir\${INSTALL_STATE_MARKER}"
    Delete /REBOOTOK "$InstallBaseDir\${INSTALL_STATE_TEMP}"
    Delete /REBOOTOK "$InstallBaseDir\${RUNTIME_MARKER}"
    Delete /REBOOTOK "$InstallBaseDir\${RUNTIME_TEMP}"
    Delete /REBOOTOK "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}"
    Delete /REBOOTOK "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}"
    Delete /REBOOTOK "$InstallBaseDir\${SYSTEM_IME_UPDATE_MARKER}"
    ${If} $UninstallRemoveUserData == ${BST_CHECKED}
        StrCpy $UninstallUserDataDirSuffix $UninstallUserDataDir 7 -7
        ${If} $UninstallUserDataDir != ""
        ${AndIf} $UninstallUserDataDirSuffix == "\cxxime"
            RMDir /r "$UninstallUserDataDir"
        ${EndIf}
    ${EndIf}
SectionEnd
