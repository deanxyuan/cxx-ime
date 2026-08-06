#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
# Build the Pinyin CXTOPN v1 intermediate consumed by topn_builder.
#
# This script owns key generation and ranking. Runtime packages must convert its
# output to DAT-16 with topn_builder before writing dictionary_manifest.json.

import argparse
import os
import sqlite3
import struct
import sys
import zipfile
from collections import defaultdict

# Binary format constants (must match short_code_cache_format.h)
TOPN_MAGIC = b"CXTOPN\x01\x00"
HEADER_FMT = "<8sIIIIIII"  # 36 bytes
HEADER_SIZE = struct.calcsize(HEADER_FMT)
KEY_FMT = "<IIIHH"         # 16 bytes (candidate_offset, candidate_count, key_offset, key_len, flags)
KEY_SIZE = struct.calcsize(KEY_FMT)
CAND_FMT = "<IIIIii"       # 24 bytes (text_off, text_len, comment_off, comment_len, freq, score)
CAND_SIZE = struct.calcsize(CAND_FMT)

MAX_MATERIALIZED_PREFIX_LENGTH = 6
MAX_MIXED_KEY_LENGTH = 16
MAX_MIXED_KEYS_PER_ENTRY = 8
MAX_CANDIDATES_PER_KEY = 64

# Ranking tiers. User dictionary matches start at 120,000,000, so cache scores
# stay below that range while preserving match quality ahead of raw frequency.
EXACT_COMPLETE_BASE = 100_000_000
EXACT_PREFIX_BASE = 80_000_000
ABBR_COMPLETE_BASE = 60_000_000
MIXED_COMPLETE_BASE = 50_000_000
ABBR_PREFIX_BASE = 30_000_000
MIXED_PREFIX_BASE = 20_000_000
MAX_FREQUENCY = 99_999_900
FREQUENCY_SCALE = 100
MAX_PREFIX_PROXIMITY = 5
PREFIX_PROXIMITY_STEP = 1_000_000

# Key flags
SHORT_KEY_EXACT = 0x01
SHORT_KEY_ABBR = 0x02
SHORT_KEY_MIXED = 0x04
SHORT_KEY_PREFIX = 0x08
SHORT_KEY_PREFIX_COMPLETE = 0x10


def resolve_input(path):
    """Resolve input path, auto-extracting .zip if needed."""
    # Check .zip/.db.zip first — must extract before returning .db path
    if path.endswith(".db.zip") and os.path.isfile(path):
        extract_dir = os.path.dirname(path) or "."
        with zipfile.ZipFile(path) as zf:
            zf.extractall(extract_dir)
        db_path = path[:-4]  # remove .zip -> .db
        if os.path.isfile(db_path):
            return db_path
    if path.endswith(".zip") and os.path.isfile(path):
        extract_dir = os.path.dirname(path) or "."
        with zipfile.ZipFile(path) as zf:
            zf.extractall(extract_dir)
        db_path = path[:-4]  # remove .zip
        if os.path.isfile(db_path):
            return db_path
    if os.path.isfile(path):
        return path
    return path


def generate_keys(syllable_ids, text, frequency):
    """Generate (key, score, flags) tuples for a single dict entry.

    syllable_ids: colon-separated syllable string, e.g. "shu:ru:fa"
    Returns list of (key_string, score, flags) tuples.
    """
    syllables = syllable_ids.split(":")
    if not syllables:
        return []

    results = {}
    n = len(syllables)

    frequency_score = min(max(frequency, 0), MAX_FREQUENCY) // FREQUENCY_SCALE

    def offer(key, base, flags, proximity=0):
        score = base + proximity + frequency_score
        existing = results.get(key)
        if existing is None:
            results[key] = (score, flags)
        elif score > existing[0]:
            results[key] = (score, flags | existing[1])
        else:
            results[key] = (existing[0], flags | existing[1])

    def prefix_base(flags):
        if flags & SHORT_KEY_EXACT:
            return EXACT_PREFIX_BASE
        if flags & SHORT_KEY_ABBR:
            return ABBR_PREFIX_BASE
        return MIXED_PREFIX_BASE

    def proximity_bonus(key, prefix):
        remaining = len(key) - len(prefix)
        closeness = max(0, MAX_PREFIX_PROXIMITY + 1 - remaining)
        return min(closeness, MAX_PREFIX_PROXIMITY) * PREFIX_PROXIMITY_STEP

    # exact_code: full concatenation, e.g. "shurufa"
    exact = "".join(syllables)
    complete_keys = [(exact, EXACT_COMPLETE_BASE, SHORT_KEY_EXACT)]

    # abbr_code: first letters, e.g. "srf"
    abbr = "".join(s[0] for s in syllables if s)
    if abbr != exact:
        complete_keys.append((abbr, ABBR_COMPLETE_BASE, SHORT_KEY_ABBR))

    # mixed_code: combinations of full/abbr per syllable (limit to MAX_MIXED_KEYS_PER_ENTRY)
    if n > 1:
        mixed_list = _generate_mixed(syllables)
        for m in mixed_list[:MAX_MIXED_KEYS_PER_ENTRY]:
            if m != exact and len(m) <= MAX_MIXED_KEY_LENGTH:
                complete_keys.append((m, MIXED_COMPLETE_BASE, SHORT_KEY_MIXED))

    for key, base, flags in complete_keys:
        offer(key, base, flags)

    # Prefixes retain their source match type. The strongest path wins when the
    # same key can be generated as both an exact prefix and a mixed complete key.
    for key, _, flags in complete_keys:
        max_prefix_length = min(len(key), MAX_MATERIALIZED_PREFIX_LENGTH)
        for plen in range(1, max_prefix_length + 1):
            prefix = key[:plen]
            if prefix != key:
                offer(prefix, prefix_base(flags), flags | SHORT_KEY_PREFIX,
                      proximity_bonus(key, prefix))

    return [(key, score, flags) for key, (score, flags) in results.items()]


def _generate_mixed(syllables):
    """Generate mixed-code keys targeting common abbreviation patterns.

    Patterns:
      1. Enhanced initial: zh/ch/sh -> two-letter, others -> first letter  (shu:ru:fa -> shrf)
      2. Long phrase head (5+ syls): first letter of each syllable         (zhong:hua:ren:min:gong:he:guo -> zhrmghg)
      B: first syllable full + rest first letter    (shu:ru:fa -> shurf)
      C: first 2 full + rest first letter            (bei:jing:da:xue -> beijidx)
      D: first letter + second full + rest first     (bei:jing:da:xue -> bjingdx)
      E: first 2 chars + rest first letter           (shu:ru:fa -> shrf)
    """
    n = len(syllables)
    if n <= 1:
        return []

    results = set()

    # Pattern 1: Enhanced initial (声母增强简拼)
    # For zh/ch/sh syllables, use two-letter initial; otherwise first letter only.
    enhanced = ""
    for s in syllables:
        if len(s) >= 2 and s[:2] in ("zh", "ch", "sh"):
            enhanced += s[:2]
        else:
            enhanced += s[0]
    results.add(enhanced)

    # Pattern 2: Long phrase head (长词首字母码, 5+ syllables)
    if n >= 5:
        results.add("".join(s[0] for s in syllables))

    rest_first = "".join(s[0] for s in syllables[1:])

    # Mode B: first syllable full + rest first letter
    results.add(syllables[0] + rest_first)

    # Mode C: first 2 syllables full + rest first letter (3+ syllables)
    if n >= 3:
        rest_first_from_3 = "".join(s[0] for s in syllables[2:])
        results.add(syllables[0] + syllables[1] + rest_first_from_3)

    # Mode D: first letter + second syllable full + rest first letter (3+ syllables)
    if n >= 3:
        results.add(syllables[0][0] + syllables[1] + rest_first_from_3)

    # Mode E: first 2 chars of first syllable + rest first letter
    if len(syllables[0]) >= 2:
        results.add(syllables[0][:2] + rest_first)

    # Remove exact (handled by exact index)
    # Keep abbr in mixed — design doc requires srf/shrf/shurf all in mixed generator
    exact = "".join(syllables)
    results.discard(exact)
    results.discard("")

    return sorted(results)


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
    long_keys = sum(1 for key in key_candidates if len(key) > MAX_MATERIALIZED_PREFIX_LENGTH)
    long_candidates = sum(
        len(candidates)
        for key, candidates in key_candidates.items()
        if len(key) > MAX_MATERIALIZED_PREFIX_LENGTH
    )
    max_key_length = max((len(key) for key in key_candidates), default=0)
    print(
        f"  Keys beyond materialized prefixes: {long_keys}, "
        f"candidates: {long_candidates}, max length: {max_key_length}",
        file=sys.stderr,
    )

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
        if len(key.encode("utf-8")) > 0xFFFF:
            raise ValueError(f"Top-N key exceeds uint16 length: {key[:64]!r}")
        key_off, key_len = intern_str(key)

        flags = 0
        for candidate in cands:
            flags |= candidate[4]
        if len(key) <= MAX_MATERIALIZED_PREFIX_LENGTH:
            flags |= SHORT_KEY_PREFIX_COMPLETE

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
    parser = argparse.ArgumentParser(description="Build the pinyin Top-N index intermediate")
    parser.add_argument("--input", required=True, help="Input .dict.db or .dict.db.zip path")
    parser.add_argument("--output", required=True, help="Output .topn.bin path")
    parser.add_argument("--no-verify", action="store_true", help="Skip required keys verification")
    args = parser.parse_args()

    db_path = resolve_input(args.input)
    if not os.path.isfile(db_path):
        print(f"ERROR: Input file not found: {db_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Building Top-N index intermediate from {db_path}...", file=sys.stderr)
    key_candidates = build_cache(db_path)

    if not key_candidates:
        print("WARNING: No keys generated. Check input data.", file=sys.stderr)

    serialize(key_candidates, args.output)

    # Verify required keys are present (unless --no-verify)
    if args.no_verify:
        print("Done (verification skipped).", file=sys.stderr)
        return
    required_keys = ["s", "sd", "sdf", "sddf", "bj", "srf", "shrf", "zguo", "nihao"]
    missing = [k for k in required_keys if k not in key_candidates]
    if missing:
        print(f"ERROR: Missing required keys in topn.bin: {missing}", file=sys.stderr)
        sys.exit(1)
    else:
        print(f"  Verified: all {len(required_keys)} required keys present", file=sys.stderr)

    print("Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
