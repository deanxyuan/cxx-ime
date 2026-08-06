"""Build the flat runtime dictionary from a SQLite source."""

from __future__ import annotations

import sqlite3
import struct
from typing import Dict, Tuple


MAGIC = b"CXDIC\x02\x00\x00"
HEADER_FORMAT = "<8sIIIII"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
ENTRY_FORMAT = "<IIIIi"
ENTRY_SIZE = struct.calcsize(ENTRY_FORMAT)


def build(database_path: str, output_path: str) -> int:
    connection = sqlite3.connect(database_path)
    cursor = connection.cursor()
    cursor.execute("PRAGMA table_info(dict)")
    columns = {row[1] for row in cursor.fetchall()}
    if "syllable_ids" in columns:
        cursor.execute(
            "SELECT text, code, frequency, syllable_ids "
            "FROM dict ORDER BY syllable_ids, frequency DESC"
        )
    else:
        print("  Note: no syllable_ids column, using code as syllable_ids")
        cursor.execute(
            "SELECT text, code, frequency, code FROM dict ORDER BY code, frequency DESC"
        )
    rows = cursor.fetchall()
    connection.close()
    if not rows:
        print("  Warning: dict empty, skipping")
        return 0

    string_data = bytearray()
    string_offsets: Dict[bytes, int] = {}

    def intern(value: str) -> Tuple[int, int]:
        encoded = value.encode("utf-8")
        if encoded in string_offsets:
            return string_offsets[encoded], len(encoded)
        offset = len(string_data)
        string_data.extend(encoded)
        string_offsets[encoded] = offset
        return offset, len(encoded)

    entries = []
    for text, _, frequency, syllable_ids in rows:
        code_offset, code_length = intern(syllable_ids)
        text_offset, text_length = intern(text)
        entries.append((code_offset, text_offset, code_length, text_length, frequency))

    entries.sort(
        key=lambda entry: bytes(
            string_data[entry[0]:entry[0] + entry[2]]
        )
    )
    entries_offset = HEADER_SIZE
    strings_offset = entries_offset + len(entries) * ENTRY_SIZE

    with open(output_path, "wb") as output:
        output.write(
            struct.pack(
                HEADER_FORMAT,
                MAGIC,
                2,
                len(entries),
                len(string_data),
                entries_offset,
                strings_offset,
            )
        )
        for entry in entries:
            output.write(struct.pack(ENTRY_FORMAT, *entry))
        output.write(string_data)

    print(f"  dict.bin: {len(entries)} entries, {len(string_data)} bytes strings")
    return len(entries)
