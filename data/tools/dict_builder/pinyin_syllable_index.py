"""Build the zero-copy syllable ID index used by Pinyin lookup."""

from __future__ import annotations

import os
import sqlite3
import struct


def build(database_path: str, output_path: str) -> int:
    connection = sqlite3.connect(database_path)
    cursor = connection.cursor()
    cursor.execute("PRAGMA table_info(dict)")
    columns = {row[1] for row in cursor.fetchall()}
    if "syllable_ids" in columns:
        cursor.execute("SELECT syllable_ids FROM dict ORDER BY syllable_ids")
    else:
        cursor.execute("SELECT code FROM dict ORDER BY code")
    rows = cursor.fetchall()
    connection.close()

    syllable_ids = {}
    syllabary = []
    for (encoded,) in rows:
        if encoded:
            for syllable in encoded.split(":"):
                if syllable and syllable not in syllable_ids:
                    syllable_ids[syllable] = len(syllabary)
                    syllabary.append(syllable)

    index_entries = []
    for entry_index, (encoded,) in enumerate(rows):
        if not encoded:
            continue
        ids = [syllable_ids[syllable] for syllable in encoded.split(":") if syllable]
        if ids:
            index_entries.append((ids, entry_index))
    index_entries.sort(key=lambda entry: entry[0])

    syllable_data = bytearray()
    syllable_offsets = []
    for syllable in syllabary:
        syllable_offsets.append(len(syllable_data))
        syllable_data.extend(syllable.encode("utf-8"))
        syllable_data.append(0)

    index_data = bytearray()
    index_offsets = []
    for ids, entry_index in index_entries:
        index_offsets.append(len(index_data))
        index_data.extend(struct.pack("<I", len(ids)))
        for syllable_id in ids:
            index_data.extend(struct.pack("<I", syllable_id))
        index_data.extend(struct.pack("<I", entry_index))

    header = b"CXIDX\0\0\0"
    header += struct.pack(
        "<IIIII",
        3,
        len(syllabary),
        len(syllable_data),
        len(index_entries),
        len(index_data),
    )
    with open(output_path, "wb") as output:
        output.write(header)
        if syllable_offsets:
            output.write(struct.pack(f"<{len(syllable_offsets)}I", *syllable_offsets))
        output.write(syllable_data)
        if index_offsets:
            output.write(struct.pack(f"<{len(index_offsets)}I", *index_offsets))
        output.write(index_data)

    size_mb = os.path.getsize(output_path) / (1024 * 1024)
    offsets_mb = len(index_offsets) * 4 / (1024 * 1024)
    print(
        f"  dict.idx: {len(syllabary)} syl, {len(index_entries)} entries, "
        f"{size_mb:.1f} MB (offsets {offsets_mb:.1f} MB)"
    )
    return len(index_entries)
