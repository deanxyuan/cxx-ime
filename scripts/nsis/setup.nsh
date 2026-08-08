Function AcquireInstallerMutex
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Global\CxxIME.Installation") p .r1 ?e'
    Pop $0
    ${If} $1 == 0
        StrCpy $FailureMessage "Failed to initialize the CxxIME installer."
        IfSilent installer_mutex_failed_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        installer_mutex_failed_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} $0 == ${ERROR_ALREADY_EXISTS}
        StrCpy $FailureMessage "Another CxxIME installer or uninstaller is already running."
        IfSilent installer_already_running_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        installer_already_running_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}
FunctionEnd

Function .onInit
    StrCpy $LaunchSettings 0
    ${IfNot} ${RunningX64}
        StrCpy $FailureMessage "CxxIME requires 64-bit Windows."
        IfSilent installer_requires_x64_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        installer_requires_x64_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}

    Call AcquireInstallerMutex
    SetShellVarContext all
    SetRegView 64
    StrCpy $INSTDIR "$PROGRAMFILES64\CxxIME"
    StrCpy $RegisteredInstallDir ""
    ClearErrors
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
    ${IfNot} ${Errors}
    ${AndIf} $0 != ""
        StrCpy $RegisteredInstallDir $0
        StrCpy $INSTDIR $0
    ${EndIf}
FunctionEnd

Function un.AcquireInstallerMutex
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Global\CxxIME.Installation") p .r1 ?e'
    Pop $0
    ${If} $1 == 0
        StrCpy $FailureMessage "Failed to initialize the CxxIME uninstaller."
        IfSilent un_mutex_failed_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        un_mutex_failed_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} $0 == ${ERROR_ALREADY_EXISTS}
        StrCpy $FailureMessage "Another CxxIME installer or uninstaller is already running."
        IfSilent un_already_running_silent
            MessageBox MB_ICONSTOP "$FailureMessage"
        un_already_running_silent:
        DetailPrint "$FailureMessage"
        SetErrorLevel 1
        Abort
    ${EndIf}
FunctionEnd

Function un.onInit
    Call un.AcquireInstallerMutex
    SetShellVarContext all
    SetRegView 64
    StrCpy $UninstallRemoveUserData 0
    StrCpy $UninstallUserDataDir "$PROFILE\cxxime"
    StrCpy $UninstallRollbackDir "$INSTDIR\${UNINSTALL_ROLLBACK_DIR}"
FunctionEnd

Function .onInstSuccess
    Exec '"$INSTDIR\cxxime-server.exe"'
    ${If} $LaunchSettings == ${BST_CHECKED}
        Exec '"$INSTDIR\cxxime-settings.exe"'
    ${EndIf}
FunctionEnd

Function FinishPage
    nsDialogs::Create 1018
    Pop $1
    ${NSD_CreateCheckbox} 20u 50u 100% 20u "Launch CxxIME Settings"
    Pop $1
    nsDialogs::Show
FunctionEnd

Function FinishPageLeave
    ${NSD_GetState} $1 $LaunchSettings
FunctionEnd

Function un.UserDataPage
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 20u 16u 100% 24u \
        "Choose whether to remove CxxIME user data. By default it is kept."
    Pop $0
    ${NSD_CreateLabel} 20u 46u 100% 12u "User data directory:"
    Pop $0
    ${NSD_CreateText} 28u 60u 100% 12u "$UninstallUserDataDir"
    Pop $0
    SendMessage $0 0x00CF 1 0
    ${NSD_CreateCheckbox} 20u 88u 100% 20u "Remove user configuration and dictionary data"
    Pop $UninstallRemoveUserDataCheckbox
    ${NSD_SetState} $UninstallRemoveUserDataCheckbox $UninstallRemoveUserData
    nsDialogs::Show
FunctionEnd

Function un.UserDataPageLeave
    ${NSD_GetState} $UninstallRemoveUserDataCheckbox $UninstallRemoveUserData
FunctionEnd

Function CheckInstallDirectory
    StrCpy $ExistingInstall 0
    IfFileExists "$INSTDIR\${INSTALL_MARKER}" install_directory_owned
    StrCmp $RegisteredInstallDir "$INSTDIR" 0 install_directory_scan_start
    IfFileExists "$INSTDIR\cxxime-server.exe" 0 install_directory_scan_start
    IfFileExists "$INSTDIR\cxxime_tsf_x64.dll" 0 install_directory_scan_start
    IfFileExists "$INSTDIR\uninstall.exe" install_directory_owned

    install_directory_scan_start:
    FindFirst $0 $1 "$INSTDIR\*"
    IfErrors install_directory_ready
    install_directory_scan:
        StrCmp $1 "." install_directory_next
        StrCmp $1 ".." install_directory_next
        FindClose $0
        StrCpy $FailureMessage \
            "The selected directory is not empty and is not a CxxIME installation. \
            Choose an empty directory."
        Push 0
        Return
    install_directory_next:
        ClearErrors
        FindNext $0 $1
        IfErrors install_directory_empty
        Goto install_directory_scan
    install_directory_empty:
        FindClose $0
        Goto install_directory_ready

    install_directory_owned:
        StrCpy $ExistingInstall 1
    install_directory_ready:
    Push 1
FunctionEnd

Function ValidateInstallDirectory
    Call CheckInstallDirectory
    Pop $0
    StrCmp $0 "1" install_directory_valid
        MessageBox MB_ICONSTOP "$FailureMessage"
        Abort
    install_directory_valid:
FunctionEnd

Function SetTransactionPaths
    StrCpy $StageDir "$INSTDIR.cxxime-stage"
    StrCpy $BackupDir "$INSTDIR.cxxime-backup"
    StrCpy $LockReportPath "$PLUGINSDIR\cxxime-locks.txt"
FunctionEnd
