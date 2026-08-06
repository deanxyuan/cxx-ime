#!/usr/bin/env python3
"""Build complete ranked prefix postings for a Wubi runtime dictionary."""

from __future__ import annotations

import os
import struct
from typing import Dict, List

from .runtime_dictionary import ENTRY_FORMAT as DICT_ENTRY_FORMAT
from .runtime_dictionary import ENTRY_SIZE as DICT_ENTRY_SIZE
from .runtime_dictionary import HEADER_FORMAT as DICT_HEADER_FORMAT
from .runtime_dictionary import HEADER_SIZE as DICT_HEADER_SIZE
from .runtime_dictionary import MAGIC as DICT_MAGIC


MAGIC = b"CXWIDX\x01\x00"
VERSION = 1
MAX_CODE_LENGTH = 4
HEADER_FORMAT = "<8s10I"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
KEY_FORMAT = "<III"
KEY_SIZE = struct.calcsize(KEY_FORMAT)


def pack_code(code: bytes) -> int:
    if not code or len(code) > MAX_CODE_LENGTH:
        raise ValueError(f"invalid Wubi code length: {code!r}")

    packed = 0
    for character in code:
        if character < ord("a") or character > ord("z"):
            raise ValueError(f"invalid Wubi code: {code!r}")
        packed = (packed << 5) | (character - ord("a") + 1)
    return packed


def _read_dictionary(path: str):
    with open(path, "rb") as source:
        data = source.read()
    if len(data) < DICT_HEADER_SIZE:
        raise ValueError("Wubi dictionary is too small")

    magic, version, entry_count, string_size, entries_offset, strings_offset = (
        struct.unpack_from(DICT_HEADER_FORMAT, data, 0)
    )
    entries_size = entry_count * DICT_ENTRY_SIZE
    if (
        magic != DICT_MAGIC
        or version != 2
        or entries_offset != DICT_HEADER_SIZE
        or strings_offset != entries_offset + entries_size
        or strings_offset + string_size != len(data)
    ):
        raise ValueError("Wubi dictionary has an unsupported layout")

    entries = []
    for entry_index in range(entry_count):
        entry_offset = entries_offset + entry_index * DICT_ENTRY_SIZE
        code_offset, text_offset, code_length, text_length, frequency = struct.unpack_from(
            DICT_ENTRY_FORMAT, data, entry_offset
        )
        if (
            code_offset > string_size
            or code_length > string_size - code_offset
            or text_offset > string_size
            or text_length > string_size - text_offset
        ):
            raise ValueError(f"Wubi dictionary entry {entry_index} exceeds string data")

        code = data[strings_offset + code_offset:strings_offset + code_offset + code_length]
        text = data[strings_offset + text_offset:strings_offset + text_offset + text_length]
        entries.append((code, text, frequency))
    return entries


def build(dict_path: str, output_path: str) -> int:
    """Build complete ranked postings for every reachable dictionary prefix."""
    entries = _read_dictionary(dict_path)
    prefixes: Dict[bytes, List[int]] = {}
    skipped_codes = 0
    for entry_index, (code, _, _) in enumerate(entries):
        try:
            pack_code(code)
        except ValueError:
            skipped_codes += 1
            continue
        for prefix_length in range(1, len(code) + 1):
            prefixes.setdefault(code[:prefix_length], []).append(entry_index)

    key_records = []
    postings = []
    for prefix in sorted(prefixes, key=pack_code):
        ranked = sorted(
            prefixes[prefix],
            key=lambda index: (
                0 if len(entries[index][0]) == len(prefix) else 1,
                len(entries[index][0]),
                -entries[index][2],
                entries[index][0],
                len(entries[index][1]),
                entries[index][1],
                index,
            ),
        )
        unique = []
        seen_text = set()
        for entry_index in ranked:
            text = entries[entry_index][1]
            if text in seen_text:
                continue
            seen_text.add(text)
            unique.append(entry_index)

        key_records.append((pack_code(prefix), len(postings), len(unique)))
        postings.extend(unique)

    keys_offset = HEADER_SIZE
    postings_offset = keys_offset + len(key_records) * KEY_SIZE
    file_size = postings_offset + len(postings) * 4
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        HEADER_SIZE,
        file_size,
        len(entries),
        len(key_records),
        len(postings),
        keys_offset,
        postings_offset,
        MAX_CODE_LENGTH,
        0,
    )

    temporary_path = output_path + ".tmp"
    with open(temporary_path, "wb") as output:
        output.write(header)
        for record in key_records:
            output.write(struct.pack(KEY_FORMAT, *record))
        if postings:
            output.write(struct.pack(f"<{len(postings)}I", *postings))
    os.replace(temporary_path, output_path)

    size_mb = os.path.getsize(output_path) / (1024 * 1024)
    print(
        f"  wubi dict.idx: {len(key_records)} prefixes, {len(postings)} postings, "
        f"{size_mb:.1f} MB, skipped {skipped_codes} unreachable codes"
    )
    return len(postings)
