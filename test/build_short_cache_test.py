#!/usr/bin/env python3
"""Test Top-N rule generation and DAT-16 finalization."""

import argparse
import os
import sys
import sqlite3
import tempfile
import zipfile
import struct

SCRIPTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "scripts")
sys.path.insert(0, SCRIPTS_DIR)

LONG_SYLLABLES = "a:bo:ci:du:e:fa:gu:hu:i:ji:ku:lu:mu:nu:o:pu:qu"
LONG_EXACT = LONG_SYLLABLES.replace(":", "")
LONG_ABBR = "".join(syllable[0] for syllable in LONG_SYLLABLES.split(":"))


def create_test_db(db_path):
    """Create a SQLite dict with entries covering key mixed code patterns."""
    conn = sqlite3.connect(db_path)
    conn.execute("""
        CREATE TABLE dict (
            text TEXT, code TEXT, frequency INTEGER, syllable_ids TEXT
        )
    """)
    entries = [
        ("输入法", "shurufa", 5000, "shu:ru:fa"),       # srf, shrf, shurf
        ("中国", "zhongguo", 8000, "zhong:guo"),         # zg, zhg, zhongg
        ("你好", "nihao", 9000, "ni:hao"),               # nihao
        ("exact-ni", "ni", 100, "ni"),
        ("high-frequency-ni-prefix", "nian", 90000000, "nian"),
        ("near-nih", "nihao", 100, "ni:hao"),
        ("far-nih", "ninhaimeiyou", 90000000, "nin:hai:mei:you"),
        ("北京", "beijing", 7000, "bei:jing"),           # bj
        ("的", "de", 99999, "de"),                        # d
        ("中华人民共和国", "zhonghuarenmingongheguo", 9500,
         "zhong:hua:ren:min:gong:he:guo"),               # zhrmghg
        ("long-complete", LONG_EXACT, 500, LONG_SYLLABLES),
    ]
    conn.executemany("INSERT INTO dict VALUES (?, ?, ?, ?)", entries)
    conn.commit()
    conn.close()


def run_build(input_path, output_path):
    """Run build_short_cache.py --no-verify and return the result."""
    import subprocess
    script = os.path.join(SCRIPTS_DIR, "build_short_cache.py")
    result = subprocess.run(
        [sys.executable, script, "--input", input_path, "--output", output_path,
         "--no-verify"],
        capture_output=True, text=True
    )
    return result


def read_keys(output_path):
    """Read the .topn.bin and return the set of keys."""
    with open(output_path, "rb") as f:
        data = f.read()

    HEADER_FMT = "<8sIIIIIII"
    _, version, key_count, _, _, keys_offset, _, str_offset = struct.unpack_from(HEADER_FMT, data)

    KEY_FMT = "<IIIHH"
    KEY_SIZE = struct.calcsize(KEY_FMT)

    found_keys = set()
    for i in range(key_count):
        off = keys_offset + i * KEY_SIZE
        _, _, key_offset_rel, key_len, _ = struct.unpack_from(KEY_FMT, data, off)
        abs_off = str_offset + key_offset_rel
        key = data[abs_off:abs_off + key_len].decode("ascii", errors="replace")
        found_keys.add(key)

    return found_keys


def read_key_flags(output_path):
    """Read the v1 key flags by key."""
    with open(output_path, "rb") as f:
        data = f.read()

    header = struct.unpack_from("<8sIIIIIII", data)
    _, _, key_count, _, _, keys_offset, _, strings_offset = header
    key_size = struct.calcsize("<IIIHH")
    flags_by_key = {}
    for i in range(key_count):
        key_pos = keys_offset + i * key_size
        _, _, key_offset, key_len, flags = struct.unpack_from("<IIIHH", data, key_pos)
        key = data[strings_offset + key_offset:
                   strings_offset + key_offset + key_len].decode("ascii")
        flags_by_key[key] = flags
    return flags_by_key


def read_candidates(output_path, wanted_key):
    """Read candidates for one key as (text, frequency, score) tuples."""
    with open(output_path, "rb") as f:
        data = f.read()

    header = struct.unpack_from("<8sIIIIIII", data)
    _, _, key_count, _, _, keys_offset, candidates_offset, strings_offset = header
    key_size = struct.calcsize("<IIIHH")
    candidate_size = struct.calcsize("<IIIIii")

    for i in range(key_count):
        key_pos = keys_offset + i * key_size
        candidate_index, candidate_count, key_offset, key_len, _ = \
            struct.unpack_from("<IIIHH", data, key_pos)
        key = data[strings_offset + key_offset:
                   strings_offset + key_offset + key_len].decode("ascii")
        if key != wanted_key:
            continue

        candidates = []
        for j in range(candidate_count):
            candidate_pos = candidates_offset + (candidate_index + j) * candidate_size
            text_offset, text_len, _, _, frequency, score = \
                struct.unpack_from("<IIIIii", data, candidate_pos)
            text = data[strings_offset + text_offset:
                        strings_offset + text_offset + text_len].decode("utf-8")
            candidates.append((text, frequency, score))
        return candidates

    return []


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--topn-builder", required=True)
    args = parser.parse_args()
    ok = True

    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, "test.dict.db")
        zip_path = os.path.join(tmpdir, "test.dict.db.zip")
        out_db = os.path.join(tmpdir, "test.topn.bin")
        out_zip = os.path.join(tmpdir, "test2.topn.bin")

        create_test_db(db_path)

        # Test 1: Direct .db input
        print("Test 1: Direct .db input ...", end=" ")
        result = run_build(db_path, out_db)
        if result.returncode != 0:
            print(f"FAIL (exit {result.returncode})")
            print(result.stderr)
            ok = False
        else:
            found_db = read_keys(out_db)
            print(f"OK ({len(found_db)} keys)")

        # Test 2: .db.zip input
        print("Test 2: .db.zip input ...", end=" ")
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.write(db_path, "test.dict.db")
        result = run_build(zip_path, out_zip)
        if result.returncode != 0:
            print(f"FAIL (exit {result.returncode})")
            print(result.stderr)
            ok = False
        else:
            found_zip = read_keys(out_zip)
            print(f"OK ({len(found_zip)} keys)")

        # Test 3: Both methods produce same keys
        if ok:
            print("Test 3: zip and db produce same keys ...", end=" ")
            if found_db != found_zip:
                print(f"FAIL (db={len(found_db)}, zip={len(found_zip)})")
                ok = False
            else:
                print("OK")

        # Test 4: Mixed code keys present
        if ok:
            print("Test 4: mixed and long complete keys are present ...", end=" ")
            missing = []
            if "shrf" not in found_db:
                missing.append("shrf")
            if "zhg" not in found_db:
                missing.append("zhg")
            if "zhrmghg" not in found_db:
                missing.append("zhrmghg")
            if LONG_EXACT not in found_db:
                missing.append(LONG_EXACT)
            if LONG_ABBR not in found_db:
                missing.append(LONG_ABBR)
            if missing:
                print(f"FAIL (missing: {missing})")
                ok = False
            else:
                print("OK")

        # Test 5: Match quality tiers override misleading raw frequency.
        if ok:
            print("Test 5: exact and near-prefix matches outrank distant prefixes ...", end=" ")
            ni = read_candidates(out_db, "ni")
            nih = read_candidates(out_db, "nih")
            ni_texts = [candidate[0] for candidate in ni]
            nih_texts = [candidate[0] for candidate in nih]
            required_ni = {"exact-ni", "high-frequency-ni-prefix"}
            required_nih = {"near-nih", "far-nih"}
            valid = required_ni.issubset(ni_texts) and required_nih.issubset(nih_texts)
            if valid:
                valid = (
                    ni_texts.index("exact-ni") < ni_texts.index("high-frequency-ni-prefix") and
                    nih_texts.index("near-nih") < nih_texts.index("far-nih")
                )
            if not valid:
                print(f"FAIL (ni={ni_texts}, nih={nih_texts})")
                ok = False
            else:
                print("OK")

        if ok:
            print("Test 6: long prefixes beyond the materialized range are absent ...", end=" ")
            long_prefix = LONG_EXACT[:7]
            if long_prefix in found_db:
                print(f"FAIL (unexpected prefix: {long_prefix})")
                ok = False
            else:
                print("OK")

        if ok:
            print("Test 7: prefix-complete metadata matches materialization ...", end=" ")
            flags_by_key = read_key_flags(out_db)
            prefix_complete = 0x10
            valid = (
                (flags_by_key["ni"] & prefix_complete) != 0 and
                (flags_by_key[LONG_EXACT] & prefix_complete) == 0
            )
            if not valid:
                print(
                    f"FAIL (ni={flags_by_key['ni']:#x}, "
                    f"long={flags_by_key[LONG_EXACT]:#x})"
                )
                ok = False
            else:
                print("OK")

        if ok:
            print("Test 8: v1 intermediate converts to runtime DAT-16 ...", end=" ")
            runtime_topn = os.path.join(tmpdir, "pinyin.topn.bin")
            with open(out_db, "rb") as source, open(runtime_topn, "wb") as destination:
                destination.write(source.read())
            from prepare_dict import finalize_topn_index
            finalize_topn_index(tmpdir, os.path.abspath(args.topn_builder))
            with open(runtime_topn, "rb") as f:
                header = f.read(20)
            magic = header[:8]
            version, header_size, layout = struct.unpack_from("<III", header, 8)
            if magic != b"CXTOPN\x02\x00" or (version, header_size, layout) != (2, 80, 2):
                print(
                    "FAIL "
                    f"(magic={magic!r}, version={version}, header={header_size}, layout={layout})"
                )
                ok = False
            else:
                print("OK")

    if ok:
        print("\nAll tests passed.")
        return 0
    else:
        print("\nSome tests FAILED.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
