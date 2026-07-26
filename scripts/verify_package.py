#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
#
# Static package preflight checks. This script validates dist/ contents before
# NSIS builds the installer. It does not install, register, or execute CxxIME.

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIST_DIR = os.path.join(ROOT, "dist")

MACHINE_X86 = 0x014C
MACHINE_X64 = 0x8664


def rel(path: str, base: str) -> str:
    return os.path.relpath(path, base).replace("\\", "/")


def add_error(errors: list[str], message: str) -> None:
    errors.append(message)


def require_file(errors: list[str], path: str, base: str) -> bool:
    if not os.path.isfile(path):
        add_error(errors, f"missing file: {rel(path, base)}")
        return False
    if os.path.getsize(path) <= 0:
        add_error(errors, f"empty file: {rel(path, base)}")
        return False
    return True


def read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
        return f.read()


def pe_machine(path: str) -> int:
    with open(path, "rb") as f:
        data = f.read(4096)

    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError("not a PE file")

    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 6 > len(data):
        raise ValueError("PE header is outside the read window")
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    return struct.unpack_from("<H", data, pe_offset + 4)[0]


def require_machine(errors: list[str], path: str, base: str, expected: int) -> None:
    try:
        actual = pe_machine(path)
    except OSError as exc:
        add_error(errors, f"cannot read PE file {rel(path, base)}: {exc}")
        return
    except ValueError as exc:
        add_error(errors, f"invalid PE file {rel(path, base)}: {exc}")
        return

    if actual != expected:
        add_error(
            errors,
            f"wrong PE machine for {rel(path, base)}: 0x{actual:04x}, expected 0x{expected:04x}",
        )


def require_text(errors: list[str], text: str, needle: str, label: str) -> None:
    if needle not in text:
        add_error(errors, f"{label}: missing `{needle}`")


def forbid_text(errors: list[str], text: str, needle: str, label: str) -> None:
    if needle in text:
        add_error(errors, f"{label}: forbidden obsolete text `{needle}`")


def normalize_manifest_path(path: object) -> str | None:
    if not isinstance(path, str) or not path:
        return None
    normalized = os.path.normpath(path.replace("\\", os.sep).replace("/", os.sep))
    if os.path.isabs(normalized):
        return None
    if normalized == ".." or normalized.startswith(".." + os.sep):
        return None
    return normalized


def check_dictionary_manifest(errors: list[str], dist_dir: str) -> list[str]:
    manifest_path = os.path.join(dist_dir, "data", "dictionary_manifest.json")
    if not require_file(errors, manifest_path, dist_dir):
        return []

    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        add_error(errors, f"invalid dictionary manifest: {exc}")
        return []

    files = manifest.get("files")
    if not isinstance(files, list):
        add_error(errors, "dictionary manifest files must be an array")
        return []

    roles: set[str] = set()
    manifest_files: list[str] = []
    seen_paths: set[str] = set()
    for index, item in enumerate(files):
        if not isinstance(item, dict):
            add_error(errors, f"dictionary manifest file entry #{index} must be an object")
            continue

        role = item.get("role")
        if isinstance(role, str) and role:
            roles.add(role)

        normalized_path = normalize_manifest_path(item.get("path"))
        if normalized_path is None:
            add_error(errors, f"dictionary manifest file entry #{index} has unsafe path")
            continue

        path_key = normalized_path.lower()
        if path_key in seen_paths:
            add_error(errors, f"dictionary manifest duplicate path: {normalized_path}")
            continue
        seen_paths.add(path_key)
        manifest_files.append(normalized_path.replace(os.sep, "\\"))
        require_file(errors, os.path.join(dist_dir, "data", normalized_path), dist_dir)

    required_roles = {
        "pinyin_dict",
        "pinyin_idx",
        "pinyin_spellings",
        "pinyin_topn",
        "wubi_dict",
        "wubi_idx",
    }
    missing = sorted(required_roles - roles)
    if missing:
        add_error(errors, "dictionary manifest missing role(s): " + ", ".join(missing))

    return manifest_files


def check_installer_script(
    errors: list[str],
    dist_dir: str,
    require_x86: bool,
    manifest_files: list[str],
    host_diagnostics: bool,
) -> None:
    nsi_path = os.path.join(dist_dir, "cxxime-setup.nsi")
    if not require_file(errors, nsi_path, dist_dir):
        return

    text = read_text(nsi_path)
    label = "cxxime-setup.nsi"

    require_text(errors, text, 'File "cxxime_tsf_x64.dll"', label)
    require_text(errors, text, 'File "cxxime_ime_x64.ime"', label)
    require_text(errors, text, 'File "cxxime-resources.dll"', label)
    require_text(errors, text, "!ifdef HOST_DIAGNOSTICS", label)
    require_text(errors, text, 'Delete "$INSTDIR\\cxxime-ime-host-probe-x64.exe"', label)
    require_text(errors, text, 'Delete "$INSTDIR\\export_stage_trace.ps1"', label)
    if host_diagnostics:
        require_text(errors, text, 'File "cxxime-ime-host-probe-x64.exe"', label)
        require_text(errors, text, 'File "export_stage_trace.ps1"', label)
    installer_data_files = [
        "default.json",
        "settings_presets.json",
        "themes.json",
        "punctuation.json",
        "dictionary_manifest.json",
    ] + manifest_files
    for name in installer_data_files:
        require_text(errors, text, f'"data\\{name}"', label)
        require_text(errors, text, f'"$INSTDIR\\data\\{name}"', label)
    require_text(
        errors,
        text,
        'nsExec::Exec \'"$WINDIR\\Sysnative\\regsvr32.exe" /s "$INSTDIR\\cxxime_tsf_x64.dll"\'',
        label,
    )
    require_text(
        errors,
        text,
        'nsExec::Exec \'"$WINDIR\\Sysnative\\regsvr32.exe" /u /s "$INSTDIR\\cxxime_tsf_x64.dll"\'',
        label,
    )
    require_text(
        errors,
        text,
        'SetOutPath "$WINDIR\\Sysnative"',
        label,
    )
    require_text(errors, text, 'File /oname=cxxime.ime "cxxime_ime_x64.ime"', label)
    require_text(errors, text, 'Delete "$WINDIR\\Sysnative\\cxxime.ime"', label)
    require_text(errors, text, 'Delete /REBOOTOK "$WINDIR\\Sysnative\\cxxime.ime"', label)
    require_text(errors, text, "CxxIME requires 64-bit Windows.", label)
    require_text(errors, text, 'File "data\\wubi86.dict.bin"', label)
    require_text(errors, text, 'File "data\\wubi86.dict.idx"', label)
    forbid_text(errors, text, 'File /nonfatal "data\\wubi86.dict.bin"', label)
    forbid_text(errors, text, 'File /nonfatal "data\\wubi86.dict.idx"', label)
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
    require_text(errors, text, 'LoadKeyboardLayoutW(w "00000409"', label)
    require_text(errors, text, "SendMessageTimeoutW(p 0xFFFF, i 0x0050, p 0, p r0", label)
    forbid_text(errors, text, "SendMessageTimeout(i 0xFFFF, i 0x0050, i 0, i 0", label)
    require_text(errors, text, "UninstPage custom un.UserDataPage un.UserDataPageLeave", label)
    require_text(errors, text, "User data directory:", label)
    require_text(errors, text, '${NSD_CreateText} 28u 60u 100% 12u "$UninstallUserDataDir"', label)
    require_text(errors, text, "Remove user configuration and dictionary data", label)
    require_text(errors, text, 'StrCpy $UninstallUserDataDir "$PROFILE\\cxxime"', label)
    require_text(errors, text, 'StrCpy $UninstallUserDataDirSuffix $UninstallUserDataDir 7 -7', label)
    require_text(errors, text, '${AndIf} $UninstallUserDataDirSuffix == "\\cxxime"', label)
    require_text(errors, text, 'RMDir /r "$UninstallUserDataDir"', label)

    if require_x86:
        require_text(errors, text, 'File "cxxime_tsf_x86.dll"', label)
        require_text(errors, text, 'File "cxxime_ime_x86.ime"', label)
        if host_diagnostics:
            require_text(errors, text, 'File "cxxime-ime-host-probe-x86.exe"', label)
        require_text(
            errors,
            text,
            'nsExec::Exec \'"$SYSDIR\\regsvr32.exe" /s "$INSTDIR\\cxxime_tsf_x86.dll"\'',
            label,
        )
        require_text(
            errors,
            text,
            'nsExec::Exec \'"$SYSDIR\\regsvr32.exe" /u /s "$INSTDIR\\cxxime_tsf_x86.dll"\'',
            label,
        )
        require_text(
            errors,
            text,
            'SetOutPath "$SYSDIR"',
            label,
        )
        require_text(errors, text, 'File /oname=cxxime.ime "cxxime_ime_x86.ime"', label)
        require_text(errors, text, 'Delete "$SYSDIR\\cxxime.ime"', label)
        require_text(errors, text, 'Delete /REBOOTOK "$SYSDIR\\cxxime.ime"', label)

    forbid_text(errors, text, "cxxime_tsf.dll", label)


def check_diagnostics_script(
    errors: list[str],
    dist_dir: str,
    require_x86: bool,
    manifest_files: list[str],
) -> None:
    script_path = os.path.join(dist_dir, "collect_diagnostics.ps1")
    if not require_file(errors, script_path, dist_dir):
        return

    text = read_text(script_path)
    label = "collect_diagnostics.ps1"
    require_text(errors, text, '"cxxime_tsf_x64.dll"', label)
    require_text(errors, text, '"cxxime_ime_x64.ime"', label)
    require_text(errors, text, '"cxxime-resources.dll"', label)
    require_text(errors, text, "$report.system_ime_files", label)
    require_text(errors, text, "Get-FileInfoSafe -Path $systemImeX64", label)
    require_text(errors, text, "Get-FileInfoSafe -Path $systemImeX86", label)
    require_text(errors, text, "registry-clsid-64.txt", label)
    require_text(errors, text, "registry-tip-64.txt", label)
    require_text(errors, text, "registry-keyboard-layouts-cxxime.txt", label)
    for name in ["dictionary_manifest.json"] + manifest_files:
        require_text(errors, text, f'"{name}"', label)
    if require_x86:
        require_text(errors, text, '"cxxime_tsf_x86.dll"', label)
        require_text(errors, text, '"cxxime_ime_x86.ime"', label)
        require_text(errors, text, "registry-clsid-32.txt", label)
        require_text(errors, text, "registry-tip-32.txt", label)


def run_checks(dist_dir: str, require_x86: bool, host_diagnostics: bool) -> list[str]:
    errors: list[str] = []

    required_files = [
        "cxxime_tsf_x64.dll",
        "cxxime_ime_x64.ime",
        "cxxime-resources.dll",
        "cxxime-server.exe",
        "cxxime-settings.exe",
        "collect_diagnostics.ps1",
        "cxxime-setup.nsi",
        "license.txt",
        os.path.join("data", "default.json"),
        os.path.join("data", "settings_presets.json"),
        os.path.join("data", "themes.json"),
        os.path.join("data", "punctuation.json"),
    ]
    diagnostic_files = [
        "export_stage_trace.ps1",
        "cxxime-ime-host-probe-x64.exe",
    ]
    if host_diagnostics:
        required_files.extend(diagnostic_files)
    else:
        for name in diagnostic_files + ["cxxime-ime-host-probe-x86.exe"]:
            if os.path.exists(os.path.join(dist_dir, name)):
                add_error(errors, f"host diagnostic file must not be packaged: {name}")
    if require_x86:
        required_files.append("cxxime_tsf_x86.dll")
        required_files.append("cxxime_ime_x86.ime")
        if host_diagnostics:
            required_files.append("cxxime-ime-host-probe-x86.exe")

    for name in required_files:
        require_file(errors, os.path.join(dist_dir, name), dist_dir)

    obsolete_files = ["cxxime_tsf_x64.dll.old", "cxxime_tsf_x86.dll.old"]
    for name in obsolete_files:
        path = os.path.join(dist_dir, name)
        if os.path.exists(path):
            add_error(errors, f"obsolete file must not be packaged: {name}")

    require_machine(errors, os.path.join(dist_dir, "cxxime_tsf_x64.dll"), dist_dir, MACHINE_X64)
    require_machine(errors, os.path.join(dist_dir, "cxxime_ime_x64.ime"), dist_dir, MACHINE_X64)
    require_machine(errors, os.path.join(dist_dir, "cxxime-resources.dll"), dist_dir, MACHINE_X64)
    require_machine(errors, os.path.join(dist_dir, "cxxime-server.exe"), dist_dir, MACHINE_X64)
    require_machine(errors, os.path.join(dist_dir, "cxxime-settings.exe"), dist_dir, MACHINE_X64)
    if host_diagnostics:
        require_machine(
            errors,
            os.path.join(dist_dir, "cxxime-ime-host-probe-x64.exe"),
            dist_dir,
            MACHINE_X64,
        )
    if require_x86:
        require_machine(errors, os.path.join(dist_dir, "cxxime_tsf_x86.dll"), dist_dir, MACHINE_X86)
        require_machine(errors, os.path.join(dist_dir, "cxxime_ime_x86.ime"), dist_dir, MACHINE_X86)
        if host_diagnostics:
            require_machine(
                errors,
                os.path.join(dist_dir, "cxxime-ime-host-probe-x86.exe"),
                dist_dir,
                MACHINE_X86,
            )

    manifest_files = check_dictionary_manifest(errors, dist_dir)
    check_installer_script(
        errors, dist_dir, require_x86, manifest_files, host_diagnostics
    )
    check_diagnostics_script(errors, dist_dir, require_x86, manifest_files)

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify CxxIME dist package layout")
    parser.add_argument("--dist-dir", default=DEFAULT_DIST_DIR)
    parser.add_argument(
        "--allow-missing-x86",
        action="store_true",
        help="Allow dist-only checks without 32-bit TSF/legacy IME modules.",
    )
    parser.add_argument(
        "--host-diag",
        action="store_true",
        help="Require the host diagnostics package layout.",
    )
    args = parser.parse_args()

    dist_dir = os.path.abspath(args.dist_dir)
    errors = run_checks(
        dist_dir,
        require_x86=not args.allow_missing_x86,
        host_diagnostics=args.host_diag,
    )
    if errors:
        print("Package preflight FAILED:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("Package preflight PASSED.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
