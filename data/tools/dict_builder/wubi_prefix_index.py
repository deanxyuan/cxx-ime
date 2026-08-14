#!/usr/bin/env python3
"""Build complete ranked prefix postings for a Wubi runtime dictionary."""

from __future__ import annotations

import json
import os
import struct
from typing import Dict, List, Optional

from .runtime_dictionary import ENTRY_FORMAT as DICT_ENTRY_FORMAT
from .runtime_dictionary import ENTRY_SIZE as DICT_ENTRY_SIZE
from .runtime_dictionary import HEADER_FORMAT as DICT_HEADER_FORMAT
from .runtime_dictionary import HEADER_SIZE as DICT_HEADER_SIZE
from .runtime_dictionary import MAGIC as DICT_MAGIC
from .wubi_ranking import (
    RankingAudit,
    audit_ranking_change,
    load_general_frequencies,
    dictionary_fingerprint,
    frequency_fingerprint,
    ranking_rules,
    ranking_fingerprint,
    rerank_visible_candidates,
    unique_source_ranking,
)


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


def _load_baseline(path: Optional[str]) -> Optional[dict]:
    if path is None:
        return None
    with open(path, "r", encoding="utf-8") as source:
        baseline = json.load(source)
    if baseline.get("schema") != 1:
        raise ValueError(f"unsupported Wubi ranking baseline: {path}")
    return baseline


def _validate_baseline(
    baseline: Optional[dict],
    entries,
    general_frequencies,
    source_records,
    ranked_records,
    audit: RankingAudit,
) -> None:
    if baseline is None:
        return
    if baseline.get("rules") != ranking_rules():
        raise ValueError(
            "Wubi ranking baseline rule mismatch: "
            f"expected {baseline.get('rules')}, got {ranking_rules()}"
        )
    expected = baseline["fingerprints"]
    actual = {
        "dictionary": dictionary_fingerprint(entries),
        "general_frequency": frequency_fingerprint(general_frequencies),
        "source_ranking": ranking_fingerprint(source_records),
        "ranked": ranking_fingerprint(ranked_records),
    }
    for name, value in actual.items():
        if expected.get(name) != value:
            raise ValueError(
                "Wubi ranking baseline mismatch for "
                f"{name}: expected {expected.get(name)}, got {value}"
            )
    expected_audit = baseline["audit"]
    actual_audit = {
        name: getattr(audit, name)
        for name in expected_audit
    }
    if actual_audit != expected_audit:
        raise ValueError(
            "Wubi ranking baseline audit mismatch: "
            f"expected {expected_audit}, got {actual_audit}"
        )


def build(
    dict_path: str,
    output_path: str,
    ranking_source_path: str,
    baseline_path: Optional[str] = None,
) -> int:
    """Build complete ranked postings for every reachable dictionary prefix."""
    entries = _read_dictionary(dict_path)
    general_frequencies = load_general_frequencies(
        ranking_source_path, {text for _, text, _ in entries}
    )
    baseline = _load_baseline(baseline_path)
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
    source_records = []
    ranked_records = []
    audit = RankingAudit()
    for prefix in sorted(prefixes, key=pack_code):
        source_ranking = unique_source_ranking(
            prefixes[prefix], entries, len(prefix)
        )
        ranked = rerank_visible_candidates(
            source_ranking, entries, len(prefix), general_frequencies
        )
        audit_ranking_change(
            source_ranking,
            ranked,
            entries,
            len(prefix),
            general_frequencies,
            audit,
        )
        source_records.append((prefix, source_ranking))
        ranked_records.append((prefix, ranked))

        key_records.append((pack_code(prefix), len(postings), len(ranked)))
        postings.extend(ranked)

    audit.validate()
    _validate_baseline(
        baseline,
        entries,
        general_frequencies,
        source_records,
        ranked_records,
        audit,
    )

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
    print(
        "  ranking audit: "
        f"prefixes={audit.prefix_count}, "
        f"short changes={audit.short_prefix_changes}, "
        f"visible set changes={audit.visible_set_changes}, "
        f"unsafe top changes={audit.unsafe_top_changes}, "
        f"three-code top changes={audit.three_code_top_changes}/"
        f"{audit.three_code_prefixes}, "
        f"four-code top changes={audit.four_code_top_changes}/"
        f"{audit.four_code_prefixes}, "
        f"unknown exact demotions={audit.unknown_exact_demotions}, "
        f"visible order changes={audit.visible_order_changes}, "
        f"position moves={audit.visible_position_moves}, "
        f"internal frequency regressions={audit.internal_frequency_regressions}"
    )
    return len(postings)
