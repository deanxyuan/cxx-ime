# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

from __future__ import annotations

import os
import struct

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


def require_order(errors: list[str], text: str, needles: list[str], label: str) -> None:
    position = -1
    for needle in needles:
        next_position = text.find(needle, position + 1)
        if next_position < 0:
            add_error(errors, f"{label}: missing ordered step `{needle}`")
            return
        if next_position <= position:
            add_error(errors, f"{label}: step is out of order `{needle}`")
            return
        position = next_position
