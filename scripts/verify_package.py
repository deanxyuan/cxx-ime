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


def check_dictionary_manifest(errors: list[str], dist_dir: str) -> None:
    manifest_path = os.path.join(dist_dir, "data", "dictionary_manifest.json")
    if not require_file(errors, manifest_path, dist_dir):
        return

    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        add_error(errors, f"invalid dictionary manifest: {exc}")
        return

    roles = {item.get("role") for item in manifest.get("files", []) if isinstance(item, dict)}
    required_roles = {
        "pinyin_dict",
        "pinyin_idx",
        "pinyin_spellings",
        "pinyin_topn",
    }
    missing = sorted(required_roles - roles)
    if missing:
        add_error(errors, "dictionary manifest missing role(s): " + ", ".join(missing))


def check_installer_script(errors: list[str], dist_dir: str, require_x86: bool) -> None:
    nsi_path = os.path.join(dist_dir, "cxxime-setup.nsi")
    if not require_file(errors, nsi_path, dist_dir):
        return

    text = read_text(nsi_path)
    label = "cxxime-setup.nsi"

    require_text(errors, text, 'File "cxxime_tsf_x64.dll"', label)
    require_text(errors, text, 'File "cxxime-resources.dll"', label)
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
    require_text(errors, text, "CxxIME requires 64-bit Windows.", label)

    if require_x86:
        require_text(errors, text, 'File "cxxime_tsf_x86.dll"', label)
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

    forbid_text(errors, text, "cxxime_tsf.dll", label)


def check_diagnostics_script(errors: list[str], dist_dir: str, require_x86: bool) -> None:
    script_path = os.path.join(dist_dir, "collect_diagnostics.ps1")
    if not require_file(errors, script_path, dist_dir):
        return

    text = read_text(script_path)
    label = "collect_diagnostics.ps1"
    require_text(errors, text, '"cxxime_tsf_x64.dll"', label)
    require_text(errors, text, '"cxxime-resources.dll"', label)
    require_text(errors, text, "registry-clsid-64.txt", label)
    require_text(errors, text, "registry-tip-64.txt", label)
    if require_x86:
        require_text(errors, text, '"cxxime_tsf_x86.dll"', label)
        require_text(errors, text, "registry-clsid-32.txt", label)
        require_text(errors, text, "registry-tip-32.txt", label)


def run_checks(dist_dir: str, require_x86: bool) -> list[str]:
    errors: list[str] = []

    required_files = [
        "cxxime_tsf_x64.dll",
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
    if require_x86:
        required_files.append("cxxime_tsf_x86.dll")

    for name in required_files:
        require_file(errors, os.path.join(dist_dir, name), dist_dir)

    obsolete_files = ["cxxime_tsf_x64.dll.old", "cxxime_tsf_x86.dll.old"]
    for name in obsolete_files:
        path = os.path.join(dist_dir, name)
        if os.path.exists(path):
            add_error(errors, f"obsolete file must not be packaged: {name}")

    require_machine(errors, os.path.join(dist_dir, "cxxime_tsf_x64.dll"), dist_dir, MACHINE_X64)
    require_machine(errors, os.path.join(dist_dir, "cxxime-resources.dll"), dist_dir, MACHINE_X64)
    require_machine(errors, os.path.join(dist_dir, "cxxime-server.exe"), dist_dir, MACHINE_X64)
    require_machine(errors, os.path.join(dist_dir, "cxxime-settings.exe"), dist_dir, MACHINE_X64)
    if require_x86:
        require_machine(errors, os.path.join(dist_dir, "cxxime_tsf_x86.dll"), dist_dir, MACHINE_X86)

    check_dictionary_manifest(errors, dist_dir)
    check_installer_script(errors, dist_dir, require_x86)
    check_diagnostics_script(errors, dist_dir, require_x86)

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify CxxIME dist package layout")
    parser.add_argument("--dist-dir", default=DEFAULT_DIST_DIR)
    parser.add_argument(
        "--allow-missing-x86",
        action="store_true",
        help="Allow dist-only checks without cxxime_tsf_x86.dll.",
    )
    args = parser.parse_args()

    dist_dir = os.path.abspath(args.dist_dir)
    errors = run_checks(dist_dir, require_x86=not args.allow_missing_x86)
    if errors:
        print("Package preflight FAILED:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("Package preflight PASSED.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
