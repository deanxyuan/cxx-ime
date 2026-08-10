# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

from __future__ import annotations

from package_checks.common import forbid_text, require_text


def check_installer_payload(
    errors: list[str],
    text: str,
    require_x86: bool,
    manifest_files: list[str],
    host_diagnostics: bool,
) -> None:
    label = "cxxime-setup.nsi"
    require_text(errors, text, '!define MUI_ICON "cxxime.ico"', label)
    require_text(errors, text, '!define MUI_UNICON "cxxime.ico"', label)
    require_text(errors, text, 'File /oname=cxxime-installer-helper.exe', label)
    require_text(errors, text, 'File "cxxime_tsf_x64.dll"', label)
    require_text(errors, text, 'File "cxxime_ime_x64.ime"', label)
    require_text(errors, text, 'File "cxxime-resources.dll"', label)
    require_text(errors, text, 'File "license.txt"', label)
    require_text(errors, text, 'File "THIRD_PARTY_NOTICES.txt"', label)
    require_text(errors, text, 'File "licenses\\rime-ice-GPL-3.0.txt"', label)
    require_text(errors, text, "!ifdef HOST_DIAGNOSTICS", label)
    if host_diagnostics:
        require_text(errors, text, 'File "cxxime-ime-host-probe-x64.exe"', label)
        require_text(errors, text, 'File "export_host_trace.ps1"', label)
    installer_data_files = [
        "default.json",
        "settings_presets.json",
        "themes.json",
        "punctuation.json",
        "symbols.json",
        "dictionary_manifest.json",
    ] + manifest_files
    for name in installer_data_files:
        require_text(errors, text, f'"data\\{name}"', label)

    uninstall_entries = [
        "data",
        "licenses",
        "cxxime_tsf_x64.dll",
        "cxxime_tsf_x86.dll",
        "cxxime_ime_x64.ime",
        "cxxime_ime_x86.ime",
        "cxxime-resources.dll",
        "cxxime-server.exe",
        "cxxime-settings.exe",
        "collect_diagnostics.ps1",
        "cxxime-ime-host-probe-x64.exe",
        "cxxime-ime-host-probe-x86.exe",
        "export_host_trace.ps1",
        "license.txt",
        "THIRD_PARTY_NOTICES.txt",
    ]
    for name in uninstall_entries:
        require_text(errors, text, f'!insertmacro StageInstalledEntry "{name}"', label)

    require_text(errors, text, '"$StageDir\\data"', label)
    require_text(errors, text, '"$WINDIR\\Sysnative\\regsvr32.exe" /s', label)
    require_text(errors, text, '"$WINDIR\\Sysnative\\regsvr32.exe" /u /s', label)
    require_text(errors, text, '"$SYSDIR\\regsvr32.exe" /s', label)
    require_text(errors, text, '"$SYSDIR\\regsvr32.exe" /u /s', label)
    require_text(errors, text, "kernel32::CopyFileW", label)
    require_text(errors, text, '"$WINDIR\\Sysnative\\cxxime.ime"', label)
    require_text(errors, text, '"$SYSDIR\\cxxime.ime"', label)
    require_text(errors, text, 'Delete "$WINDIR\\Sysnative\\cxxime.ime"', label)
    require_text(errors, text, "CxxIME requires 64-bit Windows.", label)
    require_text(errors, text, 'File "data\\wubi86.dict.bin"', label)
    require_text(errors, text, 'File "data\\wubi86.dict.idx"', label)
    forbid_text(errors, text, 'File /nonfatal "data\\wubi86.dict.bin"', label)
    forbid_text(errors, text, 'File /nonfatal "data\\wubi86.dict.idx"', label)
    forbid_text(errors, text, "switch to another input method", label)
    require_text(
        errors,
        text,
        '"DisplayIcon" \'"$INSTDIR\\cxxime-resources.dll",-100\'',
        label,
    )
    require_text(
        errors,
        text,
        '"UninstallString" \'"$INSTDIR\\uninstall.exe"\'',
        label,
    )
    require_text(
        errors,
        text,
        '"QuietUninstallString" \'"$INSTDIR\\uninstall.exe" /S\'',
        label,
    )
    require_text(errors, text, "UninstPage custom un.UserDataPage un.UserDataPageLeave", label)
    require_text(errors, text, "User data directory:", label)
    require_text(errors, text, '${NSD_CreateText} 28u 60u 100% 12u "$UninstallUserDataDir"', label)
    require_text(errors, text, "Remove user configuration and dictionary data", label)
    require_text(errors, text, 'StrCpy $UninstallUserDataDir "$PROFILE\\cxxime"', label)
    require_text(
        errors,
        text,
        "StrCpy $UninstallUserDataDirSuffix $UninstallUserDataDir 7 -7",
        label,
    )
    require_text(errors, text, '${AndIf} $UninstallUserDataDirSuffix == "\\cxxime"', label)
    require_text(errors, text, 'RMDir /r "$UninstallUserDataDir"', label)

    if require_x86:
        require_text(errors, text, 'File "cxxime_tsf_x86.dll"', label)
        require_text(errors, text, 'File "cxxime_ime_x86.ime"', label)
        if host_diagnostics:
            require_text(errors, text, 'File "cxxime-ime-host-probe-x86.exe"', label)
        require_text(errors, text, 'Delete "$SYSDIR\\cxxime.ime"', label)

    forbid_text(errors, text, "cxxime_tsf.dll", label)
