Function un.WriteDeferredMarker
    Pop $2
    ClearErrors
    FileOpen $0 "$INSTDIR\${UNINSTALL_DEFERRED_MARKER}" w
    IfErrors un_write_deferred_marker_failed
    FileWriteUTF16LE /BOM $0 "[uninstall]$\r$\n"
    FileWriteUTF16LE $0 "format=1$\r$\n"
    FileWriteUTF16LE $0 "state=$2$\r$\n"
    IfErrors un_write_deferred_marker_close_failed
    FileClose $0
    IfErrors un_write_deferred_marker_failed
    Push 1
    Return

    un_write_deferred_marker_close_failed:
    FileClose $0
    un_write_deferred_marker_failed:
    StrCpy $FailureMessage "无法创建 CxxIME 延期卸载标记。"
    Push 0
FunctionEnd

Function un.DeleteDeferredFile
    Pop $2
    ClearErrors
    Delete /REBOOTOK "$2"
    IfErrors un_delete_deferred_file_failed
    Push 1
    Return

    un_delete_deferred_file_failed:
    StrCpy $FailureMessage "无法安排在重启后删除：$2。"
    Push 0
FunctionEnd

Function un.DeleteDeferredDirectory
    Pop $2
    ClearErrors
    RMDir /r /REBOOTOK "$2"
    IfErrors un_delete_deferred_directory_failed
    Push 1
    Return

    un_delete_deferred_directory_failed:
    StrCpy $FailureMessage "无法安排在重启后删除：$2。"
    Push 0
FunctionEnd

Function un.ScheduleDeferredPath
    Pop $2
    IfFileExists "$2" un_schedule_deferred_path 0
    IfFileExists "$2\*" un_schedule_deferred_path un_schedule_deferred_path_done
    un_schedule_deferred_path:
    System::Call 'kernel32::MoveFileExW(w r2, p 0, i ${MOVEFILE_DELAY_UNTIL_REBOOT}) i .r0 ?e'
    Pop $1
    StrCmp $0 "0" un_schedule_deferred_path_failed
    un_schedule_deferred_path_done:
    Push 1
    Return

    un_schedule_deferred_path_failed:
    StrCpy $FailureMessage \
        "无法安排在重启后删除 $2（Win32 错误 $1）。"
    Push 0
FunctionEnd

!macro DeleteDeferredFile PATH
    Push "${PATH}"
    Call un.DeleteDeferredFile
    Pop $0
    StrCmp $0 "1" +3
        Push 0
        Return
!macroend

!macro DeleteDeferredDirectory PATH
    Push "${PATH}"
    Call un.DeleteDeferredDirectory
    Pop $0
    StrCmp $0 "1" +3
        Push 0
        Return
!macroend

!macro ScheduleDeferredPath PATH
    Push "${PATH}"
    Call un.ScheduleDeferredPath
    Pop $0
    StrCmp $0 "1" +3
        Push 0
        Return
!macroend

Function un.BeginDeferredUninstall
    Push "removing"
    Call un.WriteDeferredMarker
    Pop $0
    StrCmp $0 "1" +3
        Push 0
        Return

    !insertmacro DeleteDeferredDirectory "$INSTDIR\data"
    !insertmacro DeleteDeferredDirectory "$INSTDIR\licenses"
    !insertmacro DeleteDeferredDirectory "$INSTDIR\${ROLLBACK_DIR}"
    !insertmacro DeleteDeferredDirectory "$INSTDIR\${UNINSTALL_ROLLBACK_DIR}"
    !insertmacro DeleteDeferredFile "$WINDIR\Sysnative\cxxime.ime"
    !insertmacro DeleteDeferredFile "$SYSDIR\cxxime.ime"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime_tsf_x64.dll"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime_tsf_x86.dll"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime_ime_x64.ime"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime_ime_x86.ime"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime-resources.dll"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime-server.exe"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime-settings.exe"
    !insertmacro DeleteDeferredFile "$INSTDIR\collect_diagnostics.ps1"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime-ime-host-probe-x64.exe"
    !insertmacro DeleteDeferredFile "$INSTDIR\cxxime-ime-host-probe-x86.exe"
    !insertmacro DeleteDeferredFile "$INSTDIR\export_host_trace.ps1"
    !insertmacro DeleteDeferredFile "$INSTDIR\license.txt"
    !insertmacro DeleteDeferredFile "$INSTDIR\THIRD_PARTY_NOTICES.txt"
    !insertmacro DeleteDeferredFile "$INSTDIR\${INSTALL_MARKER}"
    !insertmacro DeleteDeferredFile "$INSTDIR\${TRANSACTION_MARKER}"
    !insertmacro DeleteDeferredFile "$INSTDIR\${TRANSACTION_TEMP}"
    !insertmacro DeleteDeferredFile "$INSTDIR\${UNINSTALL_TRANSACTION_MARKER}"
    !insertmacro DeleteDeferredFile "$INSTDIR\${UNINSTALL_TRANSACTION_TEMP}"
    Push 1
FunctionEnd

Function un.CleanupKnownVersion
    Pop $2
    StrCmp $2 "" un_cleanup_known_version_done
    StrCmp $2 $INSTDIR un_cleanup_known_version_files
    StrCmp $2 $InstallBaseDir un_cleanup_known_version_files
    StrLen $0 "$InstallBaseDir\"
    StrCpy $1 "$2" $0
    StrCmp $1 "$InstallBaseDir\" un_cleanup_known_version_files \
        un_cleanup_known_version_done

    un_cleanup_known_version_files:
    Delete /REBOOTOK "$2\cxxime_tsf_x64.dll"
    Delete /REBOOTOK "$2\cxxime_tsf_x86.dll"
    Delete /REBOOTOK "$2\cxxime_ime_x64.ime"
    Delete /REBOOTOK "$2\cxxime_ime_x86.ime"
    Delete /REBOOTOK "$2\cxxime-resources.dll"
    Delete /REBOOTOK "$2\cxxime-server.exe"
    Delete /REBOOTOK "$2\cxxime-settings.exe"
    Delete /REBOOTOK "$2\collect_diagnostics.ps1"
    Delete /REBOOTOK "$2\cxxime-ime-host-probe-x64.exe"
    Delete /REBOOTOK "$2\cxxime-ime-host-probe-x86.exe"
    Delete /REBOOTOK "$2\export_host_trace.ps1"
    Delete /REBOOTOK "$2\license.txt"
    Delete /REBOOTOK "$2\THIRD_PARTY_NOTICES.txt"
    Delete /REBOOTOK "$2\uninstall.exe"
    Delete /REBOOTOK "$2\${INSTALL_MARKER}"
    Delete /REBOOTOK "$2\${TRANSACTION_MARKER}"
    Delete /REBOOTOK "$2\${TRANSACTION_TEMP}"
    ClearErrors
    RMDir /r /REBOOTOK "$2\${UNINSTALL_ROLLBACK_DIR}"
    IfErrors un_cleanup_known_version_preserve_uninstall_markers
    Delete /REBOOTOK "$2\${UNINSTALL_TRANSACTION_MARKER}"
    Delete /REBOOTOK "$2\${UNINSTALL_TRANSACTION_TEMP}"
    Delete /REBOOTOK "$2\${UNINSTALL_DEFERRED_MARKER}"
    Goto un_cleanup_known_version_data
    un_cleanup_known_version_preserve_uninstall_markers:
    DetailPrint "无法安排清理旧版本的卸载回滚目录，已保留事务标记。"
    un_cleanup_known_version_data:
    Delete /REBOOTOK "$2\data\default.json"
    Delete /REBOOTOK "$2\data\settings_presets.json"
    Delete /REBOOTOK "$2\data\themes.json"
    Delete /REBOOTOK "$2\data\punctuation.json"
    Delete /REBOOTOK "$2\data\symbols.json"
    Delete /REBOOTOK "$2\data\dictionary_manifest.json"
    Delete /REBOOTOK "$2\data\pinyin.dict.bin"
    Delete /REBOOTOK "$2\data\pinyin.dict.idx"
    Delete /REBOOTOK "$2\data\pinyin.spellings.bin"
    Delete /REBOOTOK "$2\data\pinyin.topn.bin"
    Delete /REBOOTOK "$2\data\pinyin.reverse.idx"
    Delete /REBOOTOK "$2\data\wubi86.dict.bin"
    Delete /REBOOTOK "$2\data\wubi86.dict.idx"
    Delete /REBOOTOK "$2\data\wubi86.reverse.idx"
    Delete /REBOOTOK "$2\licenses\rime-ice-GPL-3.0.txt"
    RMDir /REBOOTOK "$2\data"
    RMDir /REBOOTOK "$2\licenses"
    StrCmp $2 $InstallBaseDir un_cleanup_known_version_done
    RMDir /REBOOTOK "$2"
    un_cleanup_known_version_done:
FunctionEnd

Function un.CommitDeferredUninstall
    SetOutPath "$PLUGINSDIR"
    Push "pending_restart"
    Call un.WriteDeferredMarker
    Pop $0
    StrCmp $0 "1" +3
        Push 0
        Return
    !insertmacro ScheduleDeferredPath "$INSTDIR\uninstall.exe"
    !insertmacro ScheduleDeferredPath "$INSTDIR\${UNINSTALL_DEFERRED_MARKER}"
    !insertmacro ScheduleDeferredPath "$INSTDIR"
    !insertmacro ScheduleDeferredPath "$InstallBaseDir\${RUNTIME_MARKER}"
    !insertmacro ScheduleDeferredPath "$InstallBaseDir\${RUNTIME_TEMP}"
    !insertmacro ScheduleDeferredPath "$InstallBaseDir\${SYSTEM_IME_X64_PENDING}"
    !insertmacro ScheduleDeferredPath "$InstallBaseDir\${SYSTEM_IME_X86_PENDING}"
    !insertmacro ScheduleDeferredPath "$InstallBaseDir\${SYSTEM_IME_UPDATE_MARKER}"
    SetRebootFlag true
    Push 1
FunctionEnd

Function un.FailDeferred
    Push "removing"
    Call un.WriteDeferredMarker
    Pop $0
    SetRebootFlag true
    StrCpy $FailureMessage \
        "$FailureMessage$\r$\n$\r$\n请重新启动 Windows，然后再次运行卸载程序。"
    IfSilent un_deferred_failure_silent
        MessageBox MB_ICONSTOP "$FailureMessage"
    un_deferred_failure_silent:
    DetailPrint "$FailureMessage"
    SetErrorLevel 1
    Abort
FunctionEnd
