#!/usr/bin/env python3
"""Focused tests for the Wubi complete prefix index generator."""

from __future__ import annotations

import sqlite3
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = ROOT / "data" / "tools"
BUILD_TOOL = TOOLS_DIR / "build_runtime_dictionary.py"
sys.path.insert(0, str(TOOLS_DIR))
from dict_builder import runtime_dictionary as RUNTIME_DICTIONARY
from dict_builder import wubi_prefix_index as WUBI_INDEX


def read_dictionary(path: Path):
    data = path.read_bytes()
    _, _, entry_count, _, entries_offset, strings_offset = struct.unpack_from(
        RUNTIME_DICTIONARY.HEADER_FORMAT, data, 0
    )
    entries = []
    for index in range(entry_count):
        offset = entries_offset + index * RUNTIME_DICTIONARY.ENTRY_SIZE
        code_offset, text_offset, code_length, text_length, frequency = struct.unpack_from(
            RUNTIME_DICTIONARY.ENTRY_FORMAT, data, offset
        )
        code = data[strings_offset + code_offset:strings_offset + code_offset + code_length]
        text = data[strings_offset + text_offset:strings_offset + text_offset + text_length]
        entries.append((code, text, frequency))
    return entries


def read_postings(path: Path, packed_code: int):
    data = path.read_bytes()
    header = struct.unpack_from(WUBI_INDEX.HEADER_FORMAT, data, 0)
    _, _, _, _, _, key_count, _, keys_offset, postings_offset, _, _ = header
    for index in range(key_count):
        offset = keys_offset + index * WUBI_INDEX.KEY_SIZE
        key, posting_start, posting_count = struct.unpack_from(
            WUBI_INDEX.KEY_FORMAT, data, offset
        )
        if key == packed_code:
            return list(
                struct.unpack_from(
                    f"<{posting_count}I",
                    data,
                    postings_offset + posting_start * 4,
                )
            )
    return []


def test_complete_ranked_prefix_index():
    with tempfile.TemporaryDirectory(prefix="cxxime-wubi-index-") as temp_dir:
        root = Path(temp_dir)
        database_path = root / "wubi.db"
        archive_path = root / "wubi.dict.db.zip"
        output_prefix = root / "wubi"
        dictionary_path = root / "wubi.dict.bin"
        index_path = root / "wubi.dict.idx"

        connection = sqlite3.connect(database_path)
        connection.execute("create table dict(text text, code text, frequency integer)")
        connection.executemany(
            "insert into dict values(?, ?, ?)",
            [
                ("exact-primary", "d", 20),
                ("exact-secondary", "d", 10),
                ("left", "da", 10),
                ("long-high-frequency", "daaa", 10000),
                ("care", "db", 10),
                ("friend", "dc", 10),
                ("large", "dd", 10),
                ("beard", "de", 10),
                ("unreachable", "API", 10000),
                ("too-long", "abcde", 10000),
            ],
        )
        connection.commit()
        connection.close()

        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.write(database_path, "wubi.dict.db")
        subprocess.run(
            [
                sys.executable,
                str(BUILD_TOOL),
                "--input",
                str(archive_path),
                "--output",
                str(output_prefix),
                "--dict-only",
                "--wubi-prefix-index",
            ],
            check=True,
        )

        entries = read_dictionary(dictionary_path)
        postings = read_postings(index_path, WUBI_INDEX.pack_code(b"d"))
        ranked_text = [entries[index][1].decode("utf-8") for index in postings]
        assert ranked_text[:5] == [
            "exact-primary",
            "exact-secondary",
            "left",
            "care",
            "friend",
        ]
        assert len(postings) == len(entries) - 2
        assert read_postings(index_path, WUBI_INDEX.pack_code(b"a")) == []


if __name__ == "__main__":
    test_complete_ranked_prefix_index()
