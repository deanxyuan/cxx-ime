# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

from __future__ import annotations

import re

from package_checks.common import forbid_text, require_order, require_text


def check_installer_flow(
    errors: list[str],
    text: str,
    install_text: str,
    uninstall_text: str,
) -> None:
    label = "cxxime-setup.nsi"
    required = [
        "Function AcquireInstallerMutex",
        "Function PrepareInstallTarget",
        "Function CheckFreshInstallBase",
        "Function SecureInstallBase",
        "Function CheckInstallLocks",
        "Function CollectPreviousVersionLockNotice",
        "Function StartNewServer",
        "Function ReleaseInputProcessor",
        "Function CaptureServerState",
        "Function SnapshotPreviousState",
        "Function CleanupPreviousInstall",
        "Function WriteInstallLayoutState",
        "Function RefreshInstallLayoutAfterRecovery",
        "Function RecoverPendingSystemIme",
        "Function PrepareSystemImeUpdate",
        "Function CancelPendingSystemImeUpdate",
        "Function WriteTransactionState",
        "Function RollbackInstall",
        "Function un.ReleaseInputProcessor",
        "Function un.CheckFileLocks",
        "!define MOVEFILE_DELAY_UNTIL_REBOOT 0x4",
        "!define MOVEFILE_REPLACE_DELAY_UNTIL_REBOOT 0x5",
        "!insertmacro MUI_PAGE_FINISH",
        "!define MUI_FINISHPAGE_NOREBOOTSUPPORT",
        "!define MUI_FINISHPAGE_RUN_NOTCHECKED",
        'StrCpy $LockReportPath "$PLUGINSDIR\\cxxime-locks.txt"',
        'StrCpy $InstallBaseDir "$PROGRAMFILES64\\CxxIME"',
        'StrCpy $PreviousInstallDir $0',
        'StrCpy $ActiveServerDir "$PreviousInstallDir"',
        'StrCpy $InstallTargetDir "$InstallBaseDir\\${VERSION}"',
        'StrCpy $InstallTargetPrepared 0',
        '${If} $InstallTargetPrepared == 0',
        'StrCpy $InstallBaseDir "$INSTDIR"',
        'StrCpy $InstallTargetPrepared 1',
        'StrCpy $StageDir "$InstallBaseDir\\update"',
        'StrCpy $InstallTargetDir "$InstallBaseDir\\${VERSION}.next"',
        'WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallBaseLocation" "$InstallBaseDir"',
        'WriteRegStr HKLM "${UNINSTALL_KEY}" "PreviousInstallLocation" "$PreviousInstallDir"',
        'FileWriteUTF16LE $0 "active=$INSTDIR$\\r$\\n"',
        'CreateDirectory "$InstallBaseDir\\update"',
        'server-ready "$INSTDIR\\cxxime-server.exe"',
        'secure-install-root "$InstallBaseDir"',
        'validate-install-directory "$StageDir"',
        'Delete /REBOOTOK "$PreviousInstallDir\\cxxime_tsf_x64.dll"',
        'WriteINIStr "$InstallBaseDir\\${SYSTEM_IME_UPDATE_MARKER}"',
    ]
    for item in required:
        require_text(errors, text, item, label)

    require_order(
        errors,
        text,
        [
            "Call PrepareInstallTarget",
            "Call SetTransactionPaths",
            "Call CheckFreshInstallBase",
            "Call SecureInstallBase",
            "Goto install_failed_untrusted_base",
            "Call CaptureServerState",
            "Call ReleaseInputProcessor",
            "Call StopServer",
            "Call CheckInstallLocks",
            "Call RecoverInterruptedInstall",
            "Call RefreshInstallLayoutAfterRecovery",
            "Call RecoverPendingSystemIme",
            "Call CheckPreviousVersionLimit",
            "Call CheckInstallDirectory",
            "Call SnapshotPreviousState",
            "Call WriteTransactionState",
            'Rename "$StageDir" "$INSTDIR"',
            "Call RegisterNewTsf",
            "Call WriteInstallationRegistry",
            "Call StartNewServer",
            "Call PrepareSystemImeUpdate",
            "Call WriteInstallMarker",
            'Delete "$INSTDIR\\${TRANSACTION_MARKER}"',
            "Call CopyNewSystemIme",
            "Call CollectPreviousVersionLockNotice",
            "Call CleanupPreviousInstall",
            "Call WriteInstallLayoutState",
        ],
        "Versioned install flow",
    )
    require_order(
        errors,
        text,
        [
            'StrCmp $MultiVersionInstall "1" install_lock_done',
            '"$ActiveServerDir\\cxxime_tsf_x64.dll"',
            '"$ActiveServerDir\\cxxime-server.exe"',
        ],
        "Legacy install lock source",
    )
    lock_notice_start = text.find("Function CollectPreviousVersionLockNotice")
    lock_notice_end = text.find("FunctionEnd", lock_notice_start)
    lock_notice_block = ""
    if lock_notice_start < 0 or lock_notice_end < 0:
        errors.append("Previous version lock notice: missing function")
    else:
        lock_notice_block = text[lock_notice_start:lock_notice_end]
        require_order(
            errors,
            lock_notice_block,
            [
                'StrCmp $MultiVersionInstall "1"',
                "nsExec::ExecToStack /TIMEOUT=3000",
                '"$WINDIR\\System32\\cxxime.ime"',
                'StrCmp $0 "2" collect_previous_locks_found',
                'StrCmp $0 "3" collect_previous_locks_reboot',
                'StrCmp $0 "5" collect_previous_locks_found_and_reboot',
                "collect_previous_locks_found_and_reboot:",
                "SetRebootFlag true",
                "collect_previous_locks_found:",
                "Call ReadLockReport",
                'DetailPrint "$LockReportText"',
            ],
            "Previous version lock notice",
        )
        for forbidden_item in [
            "--prompt=",
            "MessageBox",
            "Abort",
            "ReleaseInputProcessor",
            "StopServer",
        ]:
            if forbidden_item in lock_notice_block:
                errors.append(
                    "Previous version lock notice: forbidden "
                    f"`{forbidden_item}`"
                )
    require_order(
        errors,
        text,
        [
            "Function ToggleInstallLockDetails",
            "ShowWindow $InstallLockDetailsText ${SW_SHOW}",
            "Function FinishPageShow",
            "StrCpy $InstallLockDetailsVisible 0",
            '${NSD_CreateButton} 120u 108u 76u 16u "查看占用详情"',
            "${NSD_OnClick} $InstallLockDetailsButton ToggleInstallLockDetails",
            "ShowWindow $InstallLockDetailsText ${SW_HIDE}",
        ],
        "Install lock details disclosure",
    )
    require_order(
        errors,
        text,
        [
            "!define MUI_FINISHPAGE_NOREBOOTSUPPORT",
            '!define MUI_FINISHPAGE_RUN "$INSTDIR\\cxxime-settings.exe"',
            "!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishPageShow",
            "!insertmacro MUI_PAGE_FINISH",
        ],
        "Single install finish page",
    )
    install_pages_start = text.find("!insertmacro MUI_PAGE_INSTFILES")
    install_pages_end = text.find("!insertmacro MUI_PAGE_FINISH", install_pages_start)
    if install_pages_start < 0 or install_pages_end < 0:
        errors.append("Single install finish page: missing page declaration range")
    elif "Page custom" in text[install_pages_start:install_pages_end]:
        errors.append("Single install finish page: unexpected custom page")

    combined_reboot_start = lock_notice_block.find(
        "collect_previous_locks_found_and_reboot:"
    )
    reboot_only_start = lock_notice_block.find(
        "collect_previous_locks_reboot:", combined_reboot_start
    )
    lock_found_start = lock_notice_block.find(
        "collect_previous_locks_found:", reboot_only_start
    )
    if min(combined_reboot_start, reboot_only_start, lock_found_start) < 0:
        errors.append("Previous version lock notice: missing result branch")
    else:
        require_order(
            errors,
            lock_notice_block[combined_reboot_start:reboot_only_start],
            ["SetRebootFlag true", "Goto collect_previous_locks_found"],
            "Previous version combined lock result",
        )
        require_order(
            errors,
            lock_notice_block[reboot_only_start:lock_found_start],
            ["SetRebootFlag true", "Goto collect_previous_locks_done"],
            "Previous version reboot-only result",
        )

    lock_report_paths = re.findall(r'StrCpy \$LockReportPath "([^"]+)"', text)
    if not lock_report_paths:
        errors.append("Lock report path: missing assignment")
    elif any(path != r"$PLUGINSDIR\cxxime-locks.txt" for path in lock_report_paths):
        errors.append("Lock report path: must remain under $PLUGINSDIR")
    require_order(
        errors,
        uninstall_text,
        [
            "Call un.ReleaseInputProcessor",
            "Call un.StopServer",
            "Call un.CheckFileLocks",
            "Call un.PrepareTransaction",
            "Call un.UnregisterInstalledTsf",
        ],
        "Uninstall flow",
    )
    require_order(
        errors,
        text,
        [
            "Function FinalizeCommittedInstall",
            "Call CleanupPreviousInstall",
            "Call WriteInstallLayoutState",
            'Delete "$INSTDIR\\${TRANSACTION_MARKER}"',
        ],
        "Committed install recovery",
    )
    require_order(
        errors,
        text,
        [
            "Function RecoverInterruptedInstall",
            'IfFileExists "$INSTDIR\\${TRANSACTION_MARKER}" recover_target_transaction',
            'IfFileExists "$StageDir\\${TRANSACTION_MARKER}" recover_stage_transaction',
            'IfFileExists "$PreviousInstallDir\\${INSTALL_MARKER}"',
        ],
        "Interrupted transaction recovery priority",
    )
    require_order(
        errors,
        text,
        [
            "Function WriteInstallLayoutState",
            'CreateDirectory "$InstallBaseDir\\update"',
            'FileOpen $0 "$InstallBaseDir\\${INSTALL_STATE_TEMP}" w',
        ],
        "Stable install layout",
    )
    require_order(
        errors,
        install_text,
        [
            "Call WriteInstallLayoutState",
            'StrCmp $0 "1" install_layout_state_written',
            "SetErrorLevel 1",
            "Abort",
            "install_layout_state_written:",
        ],
        "Install layout failure handling",
    )
    untrusted_start = install_text.find("install_failed_untrusted_base:")
    untrusted_end = install_text.find("install_failed_before_swap:", untrusted_start)
    if untrusted_start < 0 or untrusted_end < 0:
        errors.append("Untrusted install base failure: missing isolated failure block")
    else:
        untrusted_block = install_text[untrusted_start:untrusted_end]
        for required_item in ["CloseHandle", "SetErrorLevel 1", "Abort"]:
            if required_item not in untrusted_block:
                errors.append(
                    f"Untrusted install base failure: missing `{required_item}`"
                )
        for forbidden_item in ["RMDir", "Delete", "StopServer", "RestartInstalledServer"]:
            if forbidden_item in untrusted_block:
                errors.append(
                    f"Untrusted install base failure: forbidden `{forbidden_item}`"
                )

    fresh_base_start = text.find("Function CheckFreshInstallBase")
    fresh_base_end = text.find("FunctionEnd", fresh_base_start)
    if fresh_base_start < 0 or fresh_base_end < 0:
        errors.append("Fresh install base validation: missing function")
    else:
        fresh_base_block = text[fresh_base_start:fresh_base_end]
        for forbidden_item in ["CreateDirectory", "RMDir", "Delete"]:
            if forbidden_item in fresh_base_block:
                errors.append(
                    f"Fresh install base validation: forbidden `{forbidden_item}`"
                )

    snapshot_start = text.find("Function SnapshotPreviousState")
    snapshot_end = text.find("FunctionEnd", snapshot_start)
    if snapshot_start < 0 or snapshot_end < 0:
        errors.append("Fresh install TIP state: missing snapshot function")
    else:
        require_order(
            errors,
            text[snapshot_start:snapshot_end],
            [
                "Call QueryTipRegistration",
                '${If} $OldInstallAvailable == 0',
                "StrCpy $OldTipX64Present 0",
                "StrCpy $OldTipX86Present 0",
            ],
            "Fresh install TIP state",
        )

    registry_start = text.find("Function WriteInstallationRegistry")
    registry_end = text.find("FunctionEnd", registry_start)
    if registry_start < 0 or registry_end < 0:
        errors.append("Installation registry writes: missing function")
    else:
        registry_block = text[registry_start:registry_end]
        require_order(
            errors,
            registry_block,
            [
                'WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallBaseLocation"',
                "IfErrors installation_registry_failed",
                'ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "PreviousInstallLocation"',
                'DeleteRegValue HKLM "${UNINSTALL_KEY}" "PreviousInstallLocation"',
                "ClearErrors",
                'WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString"',
                "IfErrors installation_registry_failed",
            ],
            "Installation registry error handling",
        )
        delete_previous_check = (
            'DeleteRegValue HKLM "${UNINSTALL_KEY}" "PreviousInstallLocation"\n'
            "            IfErrors installation_registry_failed"
        )
        if delete_previous_check not in registry_block:
            errors.append(
                "Installation registry error handling: optional value deletion "
                "is not checked"
            )

    restore_registry_start = text.find("Function RestorePreviousRegistry")
    restore_registry_end = text.find("FunctionEnd", restore_registry_start)
    if restore_registry_start < 0 or restore_registry_end < 0:
        errors.append("Registry restore error handling: missing function")
    else:
        restore_registry_block = text[restore_registry_start:restore_registry_end]
        for value_name in ("InstallBaseLocation", "PreviousInstallLocation"):
            delete_value_check = (
                f'DeleteRegValue HKLM "${{UNINSTALL_KEY}}" "{value_name}"\n'
                "                IfErrors restore_registry_failed"
            )
            if delete_value_check not in restore_registry_block:
                errors.append(
                    "Registry restore error handling: optional "
                    f"`{value_name}` deletion is not checked"
                )

    forbid_text(errors, text, "InitializeInstallResume", label)
    forbid_text(errors, text, "ScheduleDeferredInstall", label)
    forbid_text(errors, text, "INSTALL_PENDING_", label)
    forbid_text(errors, text, "InstallDeferred", label)
    forbid_text(errors, text, "InstallResume", label)
    forbid_text(errors, text, 'StrCpy $StageDir "$INSTDIR.cxxime-stage"', label)
    forbid_text(errors, text, 'StrCpy $InstallTargetDir "$InstallBaseDir\\versions', label)
    forbid_text(errors, text, 'RMDir /r "$PreviousInstallDir"', label)
    forbid_text(errors, text, 'RMDir /r /REBOOTOK "$PreviousInstallDir"', label)
    forbid_text(errors, text, "RmShutdown", label)
    forbid_text(errors, text, "FreeLibrary", label)
    forbid_text(errors, text, "VersionCompare", label)
    forbid_text(errors, text, "CompareVersion", label)
