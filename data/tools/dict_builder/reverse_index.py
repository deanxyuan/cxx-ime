"""Build a text-sorted reverse index over a runtime dictionary."""

from __future__ import annotations

import heapq
import mmap
import os
import struct
import tempfile
from typing import BinaryIO

from .reverse_index_format import REVERSE_INDEX_HEADER_FORMAT
from .reverse_index_format import REVERSE_INDEX_HEADER_SIZE
from .reverse_index_format import REVERSE_INDEX_MAGIC
from .reverse_index_format import REVERSE_INDEX_VERSION
from .runtime_dictionary import ENTRY_FORMAT as DICT_ENTRY_FORMAT
from .runtime_dictionary import ENTRY_SIZE as DICT_ENTRY_SIZE
from .runtime_dictionary import HEADER_FORMAT as DICT_HEADER_FORMAT
from .runtime_dictionary import HEADER_SIZE as DICT_HEADER_SIZE
from .runtime_dictionary import MAGIC as DICT_MAGIC
from .runtime_dictionary import VERSION as DICT_VERSION

DEFAULT_CHUNK_ENTRIES = 65536


class _RunReader:
    def __init__(self, path: str):
        self._source = open(path, "rb")

    def close(self) -> None:
        self._source.close()

    def next_entry_id(self) -> int | None:
        data = self._source.read(4)
        if not data:
            return None
        if len(data) != 4:
            raise RuntimeError("Reverse-index sort run is truncated")
        return struct.unpack("<I", data)[0]


def _read_dictionary_header(source: mmap.mmap) -> tuple[int, int, int, int]:
    if len(source) < DICT_HEADER_SIZE:
        raise ValueError("Runtime dictionary is too small for its header")
    magic, version, entry_count, string_size, entries_offset, strings_offset = (
        struct.unpack_from(DICT_HEADER_FORMAT, source, 0)
    )
    expected_strings_offset = DICT_HEADER_SIZE + entry_count * DICT_ENTRY_SIZE
    if (
        magic != DICT_MAGIC
        or version != DICT_VERSION
        or entries_offset != DICT_HEADER_SIZE
        or strings_offset != expected_strings_offset
        or strings_offset + string_size != len(source)
    ):
        raise ValueError("Runtime dictionary has an invalid section layout")
    return entry_count, string_size, entries_offset, strings_offset


def _entry_key(
    source: mmap.mmap,
    entry_id: int,
    entries_offset: int,
    strings_offset: int,
    string_size: int,
) -> tuple[bytes, bytes, int, int]:
    entry_offset = entries_offset + entry_id * DICT_ENTRY_SIZE
    code_offset, text_offset, code_length, text_length, frequency = struct.unpack_from(
        DICT_ENTRY_FORMAT, source, entry_offset
    )
    if (
        code_offset > string_size
        or code_length > string_size - code_offset
        or text_offset > string_size
        or text_length > string_size - text_offset
    ):
        raise ValueError(f"Runtime dictionary entry {entry_id} has an invalid string range")
    code_start = strings_offset + code_offset
    text_start = strings_offset + text_offset
    return (
        source[text_start:text_start + text_length],
        source[code_start:code_start + code_length],
        -frequency,
        entry_id,
    )


def _write_entry_ids(output: BinaryIO, entry_ids: list[int]) -> None:
    if entry_ids:
        output.write(struct.pack(f"<{len(entry_ids)}I", *entry_ids))


def _write_sorted_run(
    path: str,
    entry_ids: range,
    source: mmap.mmap,
    entries_offset: int,
    strings_offset: int,
    string_size: int,
) -> None:
    ordered = sorted(
        entry_ids,
        key=lambda entry_id: _entry_key(
            source,
            entry_id,
            entries_offset,
            strings_offset,
            string_size,
        ),
    )
    with open(path, "wb") as output:
        _write_entry_ids(output, ordered)


def _merge_sorted_runs(
    output: BinaryIO,
    run_paths: list[str],
    source: mmap.mmap,
    entries_offset: int,
    strings_offset: int,
    string_size: int,
) -> None:
    readers = [_RunReader(path) for path in run_paths]
    heap = []
    output_buffer = []
    try:
        for run_index, reader in enumerate(readers):
            entry_id = reader.next_entry_id()
            if entry_id is not None:
                key = _entry_key(
                    source,
                    entry_id,
                    entries_offset,
                    strings_offset,
                    string_size,
                )
                heapq.heappush(heap, (*key, run_index))

        while heap:
            _, _, _, entry_id, run_index = heapq.heappop(heap)
            output_buffer.append(entry_id)
            if len(output_buffer) == 4096:
                _write_entry_ids(output, output_buffer)
                output_buffer.clear()

            next_entry_id = readers[run_index].next_entry_id()
            if next_entry_id is not None:
                key = _entry_key(
                    source,
                    next_entry_id,
                    entries_offset,
                    strings_offset,
                    string_size,
                )
                heapq.heappush(heap, (*key, run_index))
        _write_entry_ids(output, output_buffer)
    finally:
        for reader in readers:
            reader.close()


def build(
    dictionary_path: str,
    output_path: str,
    chunk_entries: int = DEFAULT_CHUNK_ENTRIES,
) -> int:
    """Build an entry-ID permutation sorted for reverse text lookup."""
    if chunk_entries < 1:
        raise ValueError("chunk_entries must be positive")

    output_directory = os.path.dirname(os.path.abspath(output_path))
    os.makedirs(output_directory, exist_ok=True)
    with open(dictionary_path, "rb") as dictionary_file:
        with mmap.mmap(dictionary_file.fileno(), 0, access=mmap.ACCESS_READ) as source:
            entry_count, string_size, entries_offset, strings_offset = (
                _read_dictionary_header(source)
            )
            file_size = REVERSE_INDEX_HEADER_SIZE + entry_count * 4
            with tempfile.TemporaryDirectory(
                prefix="cxxime_reverse_index_", dir=output_directory
            ) as work_dir:
                run_paths = []
                for start in range(0, entry_count, chunk_entries):
                    run_path = os.path.join(work_dir, f"run-{len(run_paths):05d}.bin")
                    _write_sorted_run(
                        run_path,
                        range(start, min(start + chunk_entries, entry_count)),
                        source,
                        entries_offset,
                        strings_offset,
                        string_size,
                    )
                    run_paths.append(run_path)

                temporary_output = os.path.join(work_dir, "reverse.idx")
                with open(temporary_output, "wb") as output:
                    output.write(
                        struct.pack(
                            REVERSE_INDEX_HEADER_FORMAT,
                            REVERSE_INDEX_MAGIC,
                            REVERSE_INDEX_VERSION,
                            file_size,
                            entry_count,
                            entry_count,
                            string_size,
                            REVERSE_INDEX_HEADER_SIZE,
                            0,
                        )
                    )
                    _merge_sorted_runs(
                        output,
                        run_paths,
                        source,
                        entries_offset,
                        strings_offset,
                        string_size,
                    )
                os.replace(temporary_output, output_path)

    print(f"  reverse.idx: {entry_count} entry IDs")
    return entry_count
