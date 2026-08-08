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
!define INSTALL_MARKER ".cxxime-install-complete"
!define TRANSACTION_MARKER ".cxxime-install-transaction"
!define TRANSACTION_TEMP ".cxxime-install-transaction.tmp"
!define ROLLBACK_DIR ".cxxime-rollback"
!define UNINSTALL_TRANSACTION_MARKER ".cxxime-uninstall-transaction"
!define UNINSTALL_TRANSACTION_TEMP ".cxxime-uninstall-transaction.tmp"
!define UNINSTALL_ROLLBACK_DIR ".cxxime-uninstall-rollback"
!define MOVEFILE_REPLACE_WRITE_THROUGH 0x9

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
VIAddVersionKey /LANG=2052 "FileDescription" "CxxIME Installer"
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
Var LockReportPath
Var LockReportText
Var LockResult
Var FailureMessage
Var OldInstallAvailable
Var OldTsfX64Present
Var OldTsfX86Present
Var OldTsfX64Registered
Var OldTsfX86Registered
Var SystemImeX64Present
Var SystemImeX86Present
Var OldUninstallPresent
Var OldDisplayVersion
Var OldRunPresent
Var OldRunValue
Var ServerWasRunning
Var UninstallRemoveUserData
Var UninstallRemoveUserDataCheckbox
Var UninstallUserDataDir
Var UninstallUserDataDirSuffix
Var UninstallServerWasRunning
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
!insertmacro MUI_UNPAGE_CONFIRM
UninstPage custom un.UserDataPage un.UserDataPageLeave
!insertmacro MUI_UNPAGE_INSTFILES
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

    Call SetTransactionPaths
    Call StopServer
    Call CheckInstallLocks
    Call RecoverInterruptedInstall
    Pop $0
    StrCmp $0 "1" install_recovery_ready
        StrCmp $FailureMessage "" 0 install_failed_recovery
        StrCpy $FailureMessage "A previous CxxIME installation could not be recovered safely."
        Goto install_failed_recovery

    install_recovery_ready:
    Call CheckInstallDirectory
    Pop $0
    StrCmp $0 "1" install_directory_checked
        Goto install_failed_before_swap

    install_directory_checked:
    Call SnapshotPreviousState

    RMDir /r "$StageDir"
    CreateDirectory "$StageDir"
    ClearErrors

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
        File "export_stage_trace.ps1"
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
    File "data\wubi86.dict.bin"
    File "data\wubi86.dict.idx"
    !ifdef FAST
        SetCompress auto
    !endif

    WriteUninstaller "$StageDir\uninstall.exe"
    IfErrors 0 install_stage_ready
        StrCpy $FailureMessage "Failed to extract the CxxIME installation files."
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
    ${If} $ExistingInstall == 1
        IfFileExists "$BackupDir\*" 0 install_backup_path_ready
            StrCpy $FailureMessage "The CxxIME backup directory could not be prepared."
            Goto install_failed_after_transaction
        install_backup_path_ready:
        ClearErrors
        Rename "$INSTDIR" "$BackupDir"
        IfErrors 0 install_backup_ready
            StrCpy $FailureMessage "Failed to back up the installed CxxIME version."
            Goto install_failed_after_transaction
        install_backup_ready:
        Call UnregisterPreviousTsf
        Pop $0
        StrCmp $0 "1" install_swap_stage
            Goto install_failed_after_transaction
    ${Else}
        RMDir "$INSTDIR"
    ${EndIf}

    install_swap_stage:
    ClearErrors
    Rename "$StageDir" "$INSTDIR"
    IfErrors 0 install_stage_swapped
        StrCpy $FailureMessage "Failed to activate the new CxxIME files."
        Goto install_failed_after_transaction

    install_stage_swapped:
    Call CopyNewSystemIme
    Pop $0
    StrCmp $0 "1" install_register_tsf
        Goto install_failed_after_transaction

    install_register_tsf:
    Call RegisterNewTsf
    Pop $0
    StrCmp $0 "1" install_write_registry
        Goto install_failed_after_transaction

    install_write_registry:
    Call WriteInstallationRegistry
    Pop $0
    StrCmp $0 "1" install_write_marker
        Goto install_failed_after_transaction

    install_write_marker:
    Call WriteInstallMarker
    Pop $0
    StrCmp $0 "1" install_commit
        Goto install_failed_after_transaction

    install_commit:
    RMDir /r "$INSTDIR\${ROLLBACK_DIR}"
    Delete "$INSTDIR\${TRANSACTION_MARKER}"
    RMDir /r "$BackupDir"
    CreateDirectory "$PROFILE\cxxime"
    IfFileExists "$PROFILE\cxxime\default.json" install_user_config_ready
        CopyFiles /SILENT /FILESONLY "$INSTDIR\data\default.json" "$PROFILE\cxxime"
    install_user_config_ready:
    Call CreateInstallShortcuts
    DetailPrint "CxxIME ${VERSION} installation committed."
    Goto install_done

    install_failed_before_swap:
    RMDir /r "$StageDir"
    IfSilent install_failed_silent
        MessageBox MB_ICONSTOP "$FailureMessage$\r$\n$\r$\nNo installed CxxIME files were changed."
        Goto install_failed_abort

    install_failed_after_transaction:
    Call RollbackInstall
    Pop $0
    StrCmp $0 "1" install_rollback_complete
        StrCpy $FailureMessage \
            "$FailureMessage$\r$\n$\r$\nAutomatic rollback did not complete. \
            Run setup again before using CxxIME."
        Goto install_failed_silent_or_message
    install_rollback_complete:
        StrCpy $FailureMessage "$FailureMessage$\r$\n$\r$\nThe previous CxxIME state was restored."
        Goto install_failed_silent_or_message

    install_failed_recovery:
        Goto install_failed_silent_or_message

    install_failed_silent_or_message:
    IfSilent install_failed_silent
        MessageBox MB_ICONSTOP "$FailureMessage"
        Goto install_failed_abort

    install_failed_silent:
    DetailPrint "$FailureMessage"
    install_failed_abort:
    Call RestartInstalledServer
    SetErrorLevel 1
    Abort

    install_done:
SectionEnd

!include "nsis\uninstall_locks.nsh"
!include "nsis\uninstall_state.nsh"
!include "nsis\uninstall_files.nsh"

Section "Uninstall"
    SetRegView 64
    SetShellVarContext all
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=cxxime-installer-helper.exe "cxxime-installer-helper.exe"
    StrCpy $LockReportPath "$PLUGINSDIR\cxxime-locks.txt"

    Call un.StopServer
    Call un.CheckFileLocks
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

    un_rollback_failure:
    Call un.RollbackTransaction
    Pop $0
    StrCmp $0 "1" un_rollback_complete
        StrCpy $FailureMessage \
            "$FailureMessage$\r$\n$\r$\nAutomatic rollback did not complete. \
            Run the uninstaller again before using CxxIME."
        Call un.FailAndRestart
    un_rollback_complete:
    StrCpy $FailureMessage "$FailureMessage$\r$\n$\r$\nThe installed CxxIME state was restored."
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
            "The CxxIME uninstall registry entry could not be removed. Run the uninstaller again."
        Call un.FailIncomplete
    un_uninstall_registry_removed:
    ClearErrors
    ReadRegStr $0 HKLM "${RUN_KEY}" "CxxIMEServer"
    IfErrors un_run_registry_removed
        StrCpy $FailureMessage \
            "The CxxIME startup registry entry could not be removed. Run the uninstaller again."
        Call un.FailIncomplete
    un_run_registry_removed:

    RMDir /r "$SMPROGRAMS\CxxIME"
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}"
    Delete "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    IfFileExists "$INSTDIR\*" 0 un_program_files_removed
        DetailPrint "Some unrecognized files remain in $INSTDIR."
    un_program_files_removed:

    ${If} $UninstallRemoveUserData == ${BST_CHECKED}
        StrCpy $UninstallUserDataDirSuffix $UninstallUserDataDir 7 -7
        ${If} $UninstallUserDataDir != ""
        ${AndIf} $UninstallUserDataDirSuffix == "\cxxime"
            RMDir /r "$UninstallUserDataDir"
        ${EndIf}
    ${EndIf}
SectionEnd
