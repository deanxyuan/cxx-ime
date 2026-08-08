Function UnregisterPreviousTsf
    ${If} $OldTsfX86Registered == 1
        IfFileExists "$BackupDir\cxxime_tsf_x86.dll" 0 unregister_previous_x86_missing
        nsExec::ExecToStack '"$SYSDIR\regsvr32.exe" /u /s "$BackupDir\cxxime_tsf_x86.dll"'
        Pop $0
        Pop $1
        ${If} $0 != "0"
            StrCpy $FailureMessage "Failed to unregister the installed 32-bit TSF module."
            Push 0
            Return
        ${EndIf}
    ${EndIf}
    ${If} $OldTsfX64Registered == 1
        IfFileExists "$BackupDir\cxxime_tsf_x64.dll" 0 unregister_previous_x64_missing
        nsExec::ExecToStack '"$WINDIR\Sysnative\regsvr32.exe" /u /s "$BackupDir\cxxime_tsf_x64.dll"'
        Pop $0
        Pop $1
        ${If} $0 != "0"
            StrCpy $FailureMessage "Failed to unregister the installed 64-bit TSF module."
            Push 0
            Return
        ${EndIf}
    ${EndIf}
    Push 1
    Return

    unregister_previous_x86_missing:
    StrCpy $FailureMessage "The registered 32-bit TSF module is missing from the installed version."
    Push 0
    Return

    unregister_previous_x64_missing:
    StrCpy $FailureMessage "The registered 64-bit TSF module is missing from the installed version."
    Push 0
FunctionEnd

Function RegisterNewTsf
    nsExec::ExecToStack '"$WINDIR\Sysnative\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x64.dll"'
    Pop $0
    Pop $1
    ${If} $0 != "0"
        StrCpy $FailureMessage "Failed to register the 64-bit TSF module."
        Push 0
        Return
    ${EndIf}

    nsExec::ExecToStack '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x86.dll"'
    Pop $0
    Pop $1
    ${If} $0 != "0"
        StrCpy $FailureMessage "Failed to register the 32-bit TSF module."
        Push 0
        Return
    ${EndIf}
    Push 1
FunctionEnd

Function RegisterPreviousTsf
    StrCmp $OldTsfX64Registered "1" 0 restore_register_x86
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 restore_register_x64_missing
        nsExec::ExecToStack \
            '"$WINDIR\Sysnative\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x64.dll"'
        Pop $0
        Pop $1
        StrCmp $0 "0" restore_register_x86
            StrCpy $FailureMessage "Failed to restore the previous 64-bit TSF registration."
            Push 0
            Return
    restore_register_x86:
    StrCmp $OldTsfX86Registered "1" 0 restore_register_done
    IfFileExists "$INSTDIR\cxxime_tsf_x86.dll" 0 restore_register_x86_missing
        nsExec::ExecToStack '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\cxxime_tsf_x86.dll"'
        Pop $0
        Pop $1
        StrCmp $0 "0" restore_register_done
            StrCpy $FailureMessage "Failed to restore the previous 32-bit TSF registration."
            Push 0
            Return
    restore_register_done:
    Push 1
    Return

    restore_register_x64_missing:
    StrCpy $FailureMessage "The previous 64-bit TSF module could not be restored."
    Push 0
    Return

    restore_register_x86_missing:
    StrCpy $FailureMessage "The previous 32-bit TSF module could not be restored."
    Push 0
FunctionEnd

Function WriteInstallationRegistry
    ClearErrors
    WriteRegStr HKLM "${RUN_KEY}" "CxxIMEServer" '"$INSTDIR\cxxime-server.exe"'
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "CxxIME"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" '"$INSTDIR\cxxime-resources.dll",-100'
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
    IfErrors 0 installation_registry_written
        StrCpy $FailureMessage "Failed to write the CxxIME installation registry entries."
        Push 0
        Return
    installation_registry_written:
    Push 1
FunctionEnd

Function RestorePreviousRegistry
    ${If} $OldUninstallPresent == 1
        ClearErrors
        WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "CxxIME"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "$OldDisplayVersion"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" '"$INSTDIR\cxxime-resources.dll",-100'
        WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
        WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
        WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
        WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
        IfErrors restore_registry_failed
    ${Else}
        DeleteRegKey HKLM "${UNINSTALL_KEY}"
    ${EndIf}
    ${If} $OldRunPresent == 1
        ClearErrors
        WriteRegStr HKLM "${RUN_KEY}" "CxxIMEServer" "$OldRunValue"
        IfErrors restore_registry_failed
    ${Else}
        DeleteRegValue HKLM "${RUN_KEY}" "CxxIMEServer"
    ${EndIf}
    Push 1
    Return

    restore_registry_failed:
    StrCpy $FailureMessage "Failed to restore the previous CxxIME registry state."
    Push 0
FunctionEnd

Function RollbackInstall
    IfFileExists "$INSTDIR\${TRANSACTION_MARKER}" 0 rollback_staged_transaction
        StrCpy $TransactionDir "$INSTDIR"
        Call RecoverTransaction
        Return
    rollback_staged_transaction:
    IfFileExists "$StageDir\${TRANSACTION_MARKER}" 0 rollback_transaction_missing
        StrCpy $TransactionDir "$StageDir"
        Call RecoverTransaction
        Return
    rollback_transaction_missing:
    StrCpy $FailureMessage "The CxxIME rollback transaction could not be found."
    Push 0
FunctionEnd

Function WriteInstallMarker
    ClearErrors
    FileOpen $0 "$INSTDIR\${INSTALL_MARKER}" w
    IfErrors install_marker_failed
    FileWrite $0 "version=${VERSION}$\r$\n"
    FileClose $0
    IfErrors install_marker_failed
    Push 1
    Return
    install_marker_failed:
        StrCpy $FailureMessage "Failed to finalize the CxxIME installation."
        Push 0
FunctionEnd

Function CreateInstallShortcuts
    SetShellVarContext all
    CreateDirectory "$SMPROGRAMS\CxxIME"
    CreateShortCut "$SMPROGRAMS\CxxIME\CxxIME Settings.lnk" "$INSTDIR\cxxime-settings.exe"
    Delete "$SMPROGRAMS\CxxIME\Host Candidate Probe x64.lnk"
    Delete "$SMPROGRAMS\CxxIME\Host Candidate Probe x86.lnk"
    Delete "$SMPROGRAMS\CxxIME\Export Stage 1 Trace.lnk"
    !ifdef HOST_DIAGNOSTICS
        CreateShortCut \
            "$SMPROGRAMS\CxxIME\Host Candidate Probe x64.lnk" \
            "$INSTDIR\cxxime-ime-host-probe-x64.exe"
        CreateShortCut \
            "$SMPROGRAMS\CxxIME\Host Candidate Probe x86.lnk" \
            "$INSTDIR\cxxime-ime-host-probe-x86.exe"
        CreateShortCut \
            "$SMPROGRAMS\CxxIME\Export Stage 1 Trace.lnk" \
            "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" \
            '-NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\export_stage_trace.ps1"'
    !endif
    CreateShortCut \
        "$SMPROGRAMS\CxxIME\Collect Diagnostics.lnk" \
        "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" \
        '-NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\collect_diagnostics.ps1"'
    CreateShortCut "$SMPROGRAMS\CxxIME\Uninstall CxxIME.lnk" "$INSTDIR\uninstall.exe"
FunctionEnd
