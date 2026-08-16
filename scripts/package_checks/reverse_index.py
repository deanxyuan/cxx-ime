# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

"""Validate a reverse index against its runtime dictionary."""

from __future__ import annotations

import mmap
import os
import struct
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_TOOLS = os.path.join(ROOT, "data", "tools")
if DATA_TOOLS not in sys.path:
    sys.path.insert(0, DATA_TOOLS)

from dict_builder.reverse_index_format import REVERSE_INDEX_HEADER_FORMAT
from dict_builder.reverse_index_format import REVERSE_INDEX_HEADER_SIZE
from dict_builder.reverse_index_format import REVERSE_INDEX_MAGIC
from dict_builder.reverse_index_format import REVERSE_INDEX_VERSION
from dict_builder.runtime_dictionary import ENTRY_FORMAT as DICT_ENTRY_FORMAT
from dict_builder.runtime_dictionary import ENTRY_SIZE as DICT_ENTRY_SIZE
from dict_builder.runtime_dictionary import HEADER_FORMAT as DICT_HEADER_FORMAT
from dict_builder.runtime_dictionary import HEADER_SIZE as DICT_HEADER_SIZE
from dict_builder.runtime_dictionary import MAGIC as DICT_MAGIC
from dict_builder.runtime_dictionary import VERSION as DICT_VERSION


def _read_source_layout(dictionary, filename, errors):
    if len(dictionary) < DICT_HEADER_SIZE:
        errors.append(f"{filename}: source dictionary header is truncated")
        return None
    (
        magic,
        version,
        entry_count,
        string_size,
        entries_offset,
        strings_offset,
    ) = struct.unpack_from(DICT_HEADER_FORMAT, dictionary, 0)
    expected_strings_offset = DICT_HEADER_SIZE + entry_count * DICT_ENTRY_SIZE
    if (
        magic != DICT_MAGIC
        or version != DICT_VERSION
        or entries_offset != DICT_HEADER_SIZE
        or strings_offset != expected_strings_offset
        or strings_offset + string_size != len(dictionary)
    ):
        errors.append(f"{filename}: source dictionary layout is invalid")
        return None
    return entry_count, string_size, entries_offset, strings_offset


def _entry_key(dictionary, entry_id, layout, filename, errors):
    _, string_size, entries_offset, strings_offset = layout
    entry_offset = entries_offset + entry_id * DICT_ENTRY_SIZE
    code_offset, text_offset, code_length, text_length, frequency = struct.unpack_from(
        DICT_ENTRY_FORMAT, dictionary, entry_offset
    )
    if (
        code_offset > string_size
        or code_length > string_size - code_offset
        or text_offset > string_size
        or text_length > string_size - text_offset
    ):
        errors.append(f"{filename}: source entry {entry_id} has an invalid string range")
        return None
    code_start = strings_offset + code_offset
    text_start = strings_offset + text_offset
    return (
        dictionary[text_start:text_start + text_length],
        dictionary[code_start:code_start + code_length],
        -frequency,
        entry_id,
    )


def _check_data(index, dictionary, layout, filename, errors):
    if len(index) < REVERSE_INDEX_HEADER_SIZE:
        errors.append(f"{filename}: file too small for header")
        return False
    (
        magic,
        version,
        file_size,
        entry_count,
        source_entry_count,
        source_string_size,
        entry_ids_offset,
        reserved,
    ) = struct.unpack_from(REVERSE_INDEX_HEADER_FORMAT, index, 0)
    dict_entry_count, dict_string_size, _, _ = layout
    if (
        magic != REVERSE_INDEX_MAGIC
        or version != REVERSE_INDEX_VERSION
        or file_size != len(index)
        or entry_count != dict_entry_count
        or source_entry_count != dict_entry_count
        or source_string_size != dict_string_size
        or entry_ids_offset != REVERSE_INDEX_HEADER_SIZE
        or file_size != entry_ids_offset + entry_count * 4
        or reserved != 0
    ):
        errors.append(f"{filename}: invalid header or source metadata")
        return False

    seen = bytearray(entry_count)
    previous_key = None
    for position in range(entry_count):
        entry_id = struct.unpack_from("<I", index, entry_ids_offset + position * 4)[0]
        if entry_id >= entry_count or seen[entry_id]:
            errors.append(f"{filename}: entry IDs are not a permutation")
            return False
        seen[entry_id] = 1

        key = _entry_key(dictionary, entry_id, layout, filename, errors)
        if key is None:
            return False
        if previous_key is not None and key < previous_key:
            errors.append(f"{filename}: entry IDs are not in stable reverse order")
            return False
        previous_key = key
    return True


def check_reverse_index(data_dir, filename, dictionary_filename, errors):
    path = os.path.join(data_dir, filename)
    dictionary_path = os.path.join(data_dir, dictionary_filename)
    try:
        with open(dictionary_path, "rb") as dictionary_file:
            with open(path, "rb") as index_file:
                with mmap.mmap(
                    dictionary_file.fileno(), 0, access=mmap.ACCESS_READ
                ) as dictionary, mmap.mmap(
                    index_file.fileno(), 0, access=mmap.ACCESS_READ
                ) as index:
                    layout = _read_source_layout(dictionary, filename, errors)
                    if layout is None:
                        return False
                    return _check_data(index, dictionary, layout, filename, errors)
    except (OSError, ValueError, struct.error) as error:
        errors.append(f"{filename}: cannot validate index: {error}")
        return False
