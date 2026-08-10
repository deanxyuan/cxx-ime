# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

from __future__ import annotations

from package_checks.common import add_error, forbid_text, require_order, require_text


def check_installer_flow(
    errors: list[str],
    text: str,
    install_text: str,
    uninstall_text: str,
) -> None:
    label = "cxxime-setup.nsi"
    require_text(errors, text, "Function AcquireInstallerMutex", label)
    require_text(errors, text, "Function CheckInstallLocks", label)
    require_text(errors, text, "Function CheckInstallDirectory", label)
    require_text(errors, text, "Function un.CheckFileLocks", label)
    require_text(errors, text, "Function RecoverInterruptedInstall", label)
    require_text(errors, text, "Function RollbackInstall", label)
    require_text(errors, text, "Function WriteTransactionState", label)
    require_text(errors, text, "Function un.PrepareTransaction", label)
    require_text(errors, text, "Function un.MarkTransactionStaged", label)
    require_text(errors, text, "Function un.StageInstalledFiles", label)
    require_text(errors, text, "Function un.DeleteStagedFiles", label)
    require_text(errors, text, 'StrCpy $StageDir "$INSTDIR.cxxime-stage"', label)
    require_text(errors, text, 'StrCpy $BackupDir "$INSTDIR.cxxime-backup"', label)
    require_text(errors, text, "StrCpy $RegisteredInstallDir $0", label)
    require_text(errors, text, "StrCpy $LockResult $0", label)
    require_text(errors, text, "!define TSF_INPROC_KEY", label)
    require_text(errors, text, '"old_tsf_x64_registered"', label)
    require_text(errors, text, '"old_tsf_x86_registered"', label)
    require_text(errors, text, '"tsf_x64_registered"', label)
    require_text(errors, text, '"tsf_x86_registered"', label)
    require_text(errors, text, "$OldTsfX64Registered == 1", label)
    require_text(errors, text, "$OldTsfX86Registered == 1", label)
    require_text(errors, text, '$UninstallTsfX64Registered "1"', label)
    require_text(errors, text, '$UninstallTsfX86Registered "1"', label)
    require_text(errors, text, '$UninstallTransactionPhase "staged"', label)
    require_text(errors, text, '"transaction" "phase"', label)
    require_text(errors, text, "phase=$UninstallTransactionPhase", label)
    if text.count("MOVEFILE_REPLACE_WRITE_THROUGH})") != 2:
        add_error(errors, f"{label}: install and uninstall transactions must commit atomically")
    require_text(errors, text, "!define MOVEFILE_REPLACE_WRITE_THROUGH 0x9", label)
    require_text(errors, text, "!define MOVEFILE_DELAY_UNTIL_REBOOT 0x4", label)
    if text.count("FileWriteUTF16LE /BOM") != 3:
        add_error(
            errors,
            f"{label}: transaction and deferred marker files must be UTF-16 INI files",
        )
    if text.count('"format=2$\\r$\\n"') != 2:
        add_error(errors, f"{label}: install and uninstall transactions must use format 2")
    if text.count('ReadRegStr $0 HKLM "${TSF_INPROC_KEY}" ""') != 4:
        add_error(errors, f"{label}: both architectures must snapshot TSF registration")
    require_text(
        errors,
        text,
        'StrCmp $RegisteredInstallDir "$INSTDIR" 0 install_directory_scan_start',
        label,
    )
    require_text(errors, text, 'Rename "$INSTDIR" "$BackupDir"', label)
    require_text(errors, text, 'Rename "$StageDir" "$INSTDIR"', label)
    require_text(errors, text, "Call RollbackInstall", label)
    require_text(errors, text, ".cxxime-install-complete", label)
    require_text(errors, text, 'IfFileExists "$INSTDIR\\cxxime-resources.dll"', label)
    require_text(errors, text, 'IfFileExists "$INSTDIR\\cxxime_tsf_x64.dll"', label)
    require_text(errors, text, 'IfFileExists "$INSTDIR\\cxxime_tsf_x86.dll"', label)
    require_text(errors, text, ".cxxime-install-transaction", label)
    require_text(errors, text, ".cxxime-uninstall-transaction", label)
    require_text(errors, text, ".cxxime-uninstall-pending", label)
    require_text(errors, text, 'Push "removing"', label)
    require_text(errors, text, 'Push "pending_restart"', label)
    require_text(errors, text, '$UninstallDeferredResume "1"', label)
    require_text(errors, text, "Function un.BeginDeferredUninstall", label)
    require_text(errors, text, "Function un.CommitDeferredUninstall", label)
    require_text(errors, text, "Call un.FailDeferred", label)
    require_text(errors, text, "SetRebootFlag true", label)
    require_text(errors, text, "cxxime-installer-helper.exe", label)
    require_text(errors, text, "--prompt=install", label)
    require_text(errors, text, "--prompt=uninstall", label)
    require_text(errors, text, "--parent=$HWNDPARENT", label)
    if text.count('"$WINDIR\\System32\\cxxime.ime"') != 2:
        add_error(errors, f"{label}: lock checks must use the x64 helper's System32 path")
    require_text(errors, text, "nsExec::ExecToStack", label)
    require_text(errors, install_text, "Call CheckInstallLocks", "Install section")
    require_text(errors, uninstall_text, "Call un.CheckFileLocks", "Uninstall section")
    require_order(
        errors,
        install_text,
        [
            "Call StopServer",
            "Call CheckInstallLocks",
            "Call RecoverInterruptedInstall",
            "Call CheckInstallDirectory",
            "Call SnapshotPreviousState",
            "Call WriteTransactionState",
            'Rename "$INSTDIR" "$BackupDir"',
            'Rename "$StageDir" "$INSTDIR"',
            "Call CopyNewSystemIme",
            "Call RegisterNewTsf",
            "Call WriteInstallationRegistry",
            "Call WriteInstallMarker",
        ],
        "Install section",
    )
    require_order(
        errors,
        uninstall_text,
        [
            "Call un.StopServer",
            "Call un.CheckFileLocks",
            "Call un.PrepareTransaction",
            "Call un.UnregisterInstalledTsf",
            "Call un.RemoveSystemIme",
            "Call un.StageInstalledFiles",
            "Call un.MarkTransactionStaged",
            "Call un.DeleteStagedFiles",
            'DeleteRegKey HKLM "${UNINSTALL_KEY}"',
            'Delete "$INSTDIR\\uninstall.exe"',
        ],
        "Uninstall section",
    )
    forbid_text(errors, install_text, "/REBOOTOK", "Install section")
    forbid_text(errors, text, r"Keyboard Layout\Preload", label)
    forbid_text(errors, text, "LoadKeyboardLayoutW", label)
    forbid_text(errors, text, "RequireReplaceableTsfDll", label)
    forbid_text(errors, text, "kernel32::CreateFileW", label)
    forbid_text(errors, text, "WriteINIStr", label)
    forbid_text(errors, text, "FlushINI", label)
    forbid_text(errors, text, '"format" "1"', label)
    forbid_text(errors, text, "recover_legacy_backup", label)
    forbid_text(errors, uninstall_text, 'RMDir /r "$INSTDIR"', "Uninstall section")
    require_text(errors, uninstall_text, 'RMDir "$INSTDIR"', "Uninstall section")
