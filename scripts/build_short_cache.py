#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
# Build pinyin.topn.bin — short input fast path cache for Phase 4.
#
# Usage: python build_short_cache.py --input data/pinyin.dict.db --output data/pinyin.topn.bin

import argparse
import os
import sqlite3
import struct
import sys
import zipfile
from collections import defaultdict

# Binary format constants (must match short_code_cache_format.h)
TOPN_MAGIC = b"CXTOPN\x01\x00"
HEADER_FMT = "<8sIIIIIII"  # 32 bytes
HEADER_SIZE = struct.calcsize(HEADER_FMT)
KEY_FMT = "<IIIHH"         # 16 bytes (candidate_offset, candidate_count, key_offset, key_len, flags)
KEY_SIZE = struct.calcsize(KEY_FMT)
CAND_FMT = "<IIIIii"       # 24 bytes (text_off, text_len, comment_off, comment_len, freq, score)
CAND_SIZE = struct.calcsize(CAND_FMT)

MAX_SHORT_KEY_LEN = 6
MAX_MIXED_KEYS_PER_ENTRY = 16
MAX_CANDIDATES_PER_KEY = 64

# Score bonuses
EXACT_COMPLETE_BONUS = 100000
EXACT_PREFIX_BONUS = 50000
MIXED_BONUS = 10000
ABBR_BONUS = 5000

# Key flags
SHORT_KEY_EXACT = 0x01
SHORT_KEY_ABBR = 0x02
SHORT_KEY_MIXED = 0x04
SHORT_KEY_PREFIX = 0x08


def resolve_input(path):
    """Resolve input path, auto-extracting .zip if needed."""
    if os.path.isfile(path):
        return path
    if path.endswith(".zip") and os.path.isfile(path):
        extract_dir = os.path.dirname(path) or "."
        with zipfile.ZipFile(path) as zf:
            zf.extractall(extract_dir)
        db_path = path[:-4]  # remove .zip
        if os.path.isfile(db_path):
            return db_path
    # Try .db.zip -> .db
    if path.endswith(".db.zip") and os.path.isfile(path):
        extract_dir = os.path.dirname(path) or "."
        with zipfile.ZipFile(path) as zf:
            zf.extractall(extract_dir)
        db_path = path[:-4]
        if os.path.isfile(db_path):
            return db_path
    return path


def generate_keys(syllable_ids, text, frequency):
    """Generate (key, score, flags) tuples for a single dict entry.

    syllable_ids: colon-separated syllable string, e.g. "shu:ru:fa"
    Returns list of (key_string, score, flags) tuples.
    """
    syllables = syllable_ids.split(":")
    if not syllables:
        return []

    results = []
    n = len(syllables)

    # exact_code: full concatenation, e.g. "shurufa"
    exact = "".join(syllables)
    exact_score = frequency + EXACT_COMPLETE_BONUS
    results.append((exact, exact_score, SHORT_KEY_EXACT))

    # abbr_code: first letters, e.g. "srf"
    abbr = "".join(s[0] for s in syllables if s)
    if abbr != exact:
        abbr_score = frequency + ABBR_BONUS
        results.append((abbr, abbr_score, SHORT_KEY_ABBR))

    # mixed_code: combinations of full/abbr per syllable (limit to MAX_MIXED_KEYS_PER_ENTRY)
    if n > 1:
        mixed_list = _generate_mixed(syllables)
        for m in mixed_list[:MAX_MIXED_KEYS_PER_ENTRY]:
            if m != exact and m != abbr:
                mixed_score = frequency + MIXED_BONUS
                results.append((m, mixed_score, SHORT_KEY_MIXED))

    # prefix_code: all prefixes of all keys above, length 1..MAX_SHORT_KEY_LEN
    prefix_set = set()
    for key, _, flags in list(results):
        for plen in range(1, min(len(key), MAX_SHORT_KEY_LEN) + 1):
            prefix = key[:plen]
            if prefix != key:  # don't duplicate the full key
                prefix_set.add(prefix)

    for prefix in prefix_set:
        prefix_score = frequency + EXACT_PREFIX_BONUS
        results.append((prefix, prefix_score, SHORT_KEY_PREFIX))

    return results


def _generate_mixed(syllables):
    """Generate mixed-code keys: each syllable is either full or first-letter."""
    n = len(syllables)
    if n == 0:
        return []
    if n == 1:
        return [syllables[0]]

    # For large n, limit combinations to avoid explosion
    max_combos = MAX_MIXED_KEYS_PER_ENTRY * 2  # generate extra, we'll trim later
    results = []

    def backtrack(pos, current):
        if len(results) >= max_combos:
            return
        if pos == n:
            s = "".join(current)
            results.append(s)
            return
        syl = syllables[pos]
        # Full syllable
        backtrack(pos + 1, current + [syl])
        # First letter only (abbreviation)
        if len(syl) > 1:
            backtrack(pos + 1, current + [syl[0]])

    backtrack(0, [])
    # Remove exact and abbr (they're handled separately)
    exact = "".join(syllables)
    abbr = "".join(s[0] for s in syllables if s)
    results = [r for r in results if r != exact and r != abbr]
    # Deduplicate while preserving order
    seen = set()
    deduped = []
    for r in results:
        if r not in seen:
            seen.add(r)
            deduped.append(r)
    return deduped


def build_cache(db_path):
    """Read dict entries from SQLite and build the key -> candidates mapping."""
    conn = sqlite3.connect(db_path)
    cursor = conn.execute("SELECT text, code, frequency, syllable_ids FROM dict")

    # key -> list of (text, comment, frequency, score, flags)
    key_candidates = defaultdict(list)
    seen_keys_text = defaultdict(set)  # key -> set of text (for dedup)

    count = 0
    for text, code, frequency, syllable_ids in cursor:
        if not syllable_ids or not text:
            continue

        keys = generate_keys(syllable_ids, text, frequency)
        for key, score, flags in keys:
            # Only keep short keys
            if len(key) > MAX_SHORT_KEY_LEN:
                continue
            if len(key) == 0:
                continue
            # Dedup by text within each key
            if text in seen_keys_text[key]:
                continue
            seen_keys_text[key].add(text)
            key_candidates[key].append((text, "", frequency, score, flags))

        count += 1
        if count % 100000 == 0:
            print(f"  Processed {count} entries...", file=sys.stderr)

    conn.close()

    # Trim each key to top MAX_CANDIDATES_PER_KEY by score
    for key in key_candidates:
        cands = key_candidates[key]
        cands.sort(key=lambda x: (-x[3], -x[2], len(x[0]), x[0]))  # score desc, freq desc, len asc, lex asc
        key_candidates[key] = cands[:MAX_CANDIDATES_PER_KEY]

    print(f"  Total entries: {count}", file=sys.stderr)
    print(f"  Unique keys: {len(key_candidates)}", file=sys.stderr)
    total_cands = sum(len(v) for v in key_candidates.values())
    print(f"  Total candidates: {total_cands}", file=sys.stderr)

    return key_candidates


def serialize(key_candidates, output_path):
    """Write the binary cache file."""
    # Sort keys by byte order
    sorted_keys = sorted(key_candidates.keys())

    # Build string data buffer and collect entries
    strings = bytearray()
    key_entries = []   # (key_offset, key_len, flags, cand_start_idx, cand_count)
    cand_entries = []  # (text_off, text_len, comment_off, comment_len, freq, score)

    def intern_str(s):
        off = len(strings)
        strings.extend(s.encode("utf-8"))
        return off, len(s.encode("utf-8"))

    for key in sorted_keys:
        cands = key_candidates[key]
        key_off, key_len = intern_str(key)

        # Determine flags: use the flags from the first candidate
        flags = cands[0][4] if cands else SHORT_KEY_EXACT

        cand_start = len(cand_entries)
        for text, comment, freq, score, _ in cands:
            text_off, text_len = intern_str(text)
            if comment:
                comment_off, comment_len = intern_str(comment)
            else:
                comment_off, comment_len = 0, 0
            cand_entries.append((text_off, text_len, comment_off, comment_len, freq, score))

        key_entries.append((key_off, key_len, flags, cand_start, len(cands)))

    key_count = len(key_entries)
    cand_count = len(cand_entries)
    string_data_size = len(strings)

    keys_offset = HEADER_SIZE
    cand_offset = keys_offset + key_count * KEY_SIZE
    string_offset = cand_offset + cand_count * CAND_SIZE

    with open(output_path, "wb") as f:
        # Header
        hdr = struct.pack(HEADER_FMT,
                          TOPN_MAGIC, 1, key_count, cand_count,
                          string_data_size, keys_offset, cand_offset, string_offset)
        f.write(hdr)

        # Key entries
        for key_off, key_len, flags, cand_start, cand_count_entry in key_entries:
            f.write(struct.pack(KEY_FMT, cand_start, cand_count_entry, key_off, key_len, flags))

        # Candidate entries
        for text_off, text_len, comment_off, comment_len, freq, score in cand_entries:
            f.write(struct.pack(CAND_FMT, text_off, text_len, comment_off, comment_len, freq, score))

        # String data
        f.write(bytes(strings))

    file_size = string_offset + string_data_size
    print(f"  Written {file_size} bytes to {output_path}", file=sys.stderr)
    print(f"  Keys: {key_count}, Candidates: {cand_count}, Strings: {string_data_size}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Build pinyin.topn.bin short code cache")
    parser.add_argument("--input", required=True, help="Input .dict.db or .dict.db.zip path")
    parser.add_argument("--output", required=True, help="Output .topn.bin path")
    args = parser.parse_args()

    db_path = resolve_input(args.input)
    if not os.path.isfile(db_path):
        print(f"ERROR: Input file not found: {db_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Building short code cache from {db_path}...", file=sys.stderr)
    key_candidates = build_cache(db_path)

    if not key_candidates:
        print("WARNING: No keys generated. Check input data.", file=sys.stderr)

    serialize(key_candidates, args.output)
    print("Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
