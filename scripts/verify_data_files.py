#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
#
# Verify data file integrity for release packaging.
#
# Checks:
#   1. Required files exist and size > 0
#   2. Magic bytes correct for each binary file
#   3. pinyin.topn.bin contains required short input keys
#   4. pinyin.dict.idx and pinyin.dict.bin version consistency
#   5. dictionary_manifest.json describes the complete pinyin bundle
#
# Usage: python verify_data_files.py --data-dir <dir>
#
# Exit codes:
#   0 = all checks passed
#   1 = verification failed

import argparse
import hashlib
import json
import os
import struct
import sys

REQUIRED_FILES = [
    "dictionary_manifest.json",
    "pinyin.dict.bin",
    "pinyin.dict.idx",
    "pinyin.spellings.bin",
    "pinyin.topn.bin",
    "default.json",
    "settings_presets.json",
    "punctuation.json",
]

REQUIRED_TOPN_KEYS = ["s", "sd", "sdf", "sddf", "bj", "srf", "shrf"]

REQUIRED_MANIFEST_ROLES = {
    "pinyin_dict",
    "pinyin_idx",
    "pinyin_spellings",
    "pinyin_topn",
}

# Magic values (first 8 bytes of each binary file)
DICT_MAGIC_V1 = b"CXDIC\x01\x00\x00"
DICT_MAGIC_V2 = b"CXDIC\x02\x00\x00"
IDX_MAGIC = b"CXIDX\x00\x00\x00"
SPELLINGS_MAGIC_V2 = b"CXSPL\x02\x00\x00"
TOPN_MAGIC = b"CXTOPN\x01\x00"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def check_file_exists(data_dir, filename, errors):
    """Check file exists and size > 0."""
    path = os.path.join(data_dir, filename)
    if not os.path.isfile(path):
        errors.append(f"{filename}: file not found")
        return False
    size = os.path.getsize(path)
    if size == 0:
        errors.append(f"{filename}: file is empty")
        return False
    return True


def read_magic(path):
    """Read first 8 bytes of a file."""
    with open(path, "rb") as f:
        return f.read(8)


def check_dict_bin(data_dir, errors):
    """Validate pinyin.dict.bin magic and version."""
    path = os.path.join(data_dir, "pinyin.dict.bin")
    with open(path, "rb") as f:
        data = f.read(28)  # DictHeader = 28 bytes
    if len(data) < 28:
        errors.append("pinyin.dict.bin: file too small for header")
        return None
    magic = data[0:8]
    if magic != DICT_MAGIC_V1 and magic != DICT_MAGIC_V2:
        errors.append(f"pinyin.dict.bin: bad magic {magic!r}")
        return None
    version = struct.unpack_from("<I", data, 8)[0]
    if version not in (1, 2):
        errors.append(f"pinyin.dict.bin: bad version {version}")
        return None
    return version


def check_dict_idx(data_dir, errors):
    """Validate pinyin.dict.idx magic and version."""
    path = os.path.join(data_dir, "pinyin.dict.idx")
    with open(path, "rb") as f:
        data = f.read(28)
    if len(data) < 28:
        errors.append("pinyin.dict.idx: file too small for header")
        return None
    magic = data[0:8]
    if magic != IDX_MAGIC:
        errors.append(f"pinyin.dict.idx: bad magic {magic!r}")
        return None
    # Header: magic(8) version(4) syl_count(4) syl_str_size(4) idx_count(4) idx_data_size(4)
    version = struct.unpack_from("<I", data, 8)[0]
    if version < 2 or version > 3:
        errors.append(f"pinyin.dict.idx: bad version {version}")
        return None
    return version


def check_spellings_bin(data_dir, errors):
    """Validate pinyin.spellings.bin magic."""
    path = os.path.join(data_dir, "pinyin.spellings.bin")
    magic = read_magic(path)
    if magic != SPELLINGS_MAGIC_V2:
        errors.append(f"pinyin.spellings.bin: bad magic {magic!r}")
        return False
    return True


def check_topn_bin(data_dir, errors):
    """Validate pinyin.topn.bin magic and required keys."""
    path = os.path.join(data_dir, "pinyin.topn.bin")
    with open(path, "rb") as f:
        header = f.read(36)  # ShortCacheHeader = 36 bytes
    if len(header) < 36:
        errors.append("pinyin.topn.bin: file too small for header")
        return False

    magic = header[0:8]
    if magic != TOPN_MAGIC:
        errors.append(f"pinyin.topn.bin: bad magic {magic!r}")
        return False

    # Parse header: magic(8) version(4) key_count(4) cand_count(4) str_size(4) keys_off(4) cands_off(4) strs_off(4)
    _, key_count, _, _, keys_offset, _, strings_offset = struct.unpack_from("<IIIIIII", header, 8)

    # Read key entries and build set of keys
    found_keys = set()
    with open(path, "rb") as f:
        for i in range(key_count):
            f.seek(keys_offset + i * 16)  # ShortKeyEntry = 16 bytes
            entry = f.read(16)
            if len(entry) < 16:
                break
            # cand_offset(4), cand_count(4), key_offset(4), key_len(2), flags(2)
            _, _, key_offset, key_len, _ = struct.unpack_from("<IIIHH", entry, 0)
            f.seek(strings_offset + key_offset)
            key_str = f.read(key_len).decode("utf-8", errors="replace")
            found_keys.add(key_str)

    missing = [k for k in REQUIRED_TOPN_KEYS if k not in found_keys]
    if missing:
        errors.append(f"pinyin.topn.bin: missing required keys: {', '.join(missing)}")
        return False
    return True


def check_version_consistency(dict_ver, idx_ver, errors):
    """Check that dict.bin and idx versions are compatible."""
    if dict_ver is None or idx_ver is None:
        return  # Already reported errors
    # dict.bin v1/v2, idx v2/v3 — both should be >= 2 for current builds
    if dict_ver < 2 and idx_ver >= 2:
        errors.append(f"version mismatch: dict.bin v{dict_ver}, idx v{idx_ver}")
        return False
    return True


def is_safe_manifest_path(path):
    if not path:
        return False
    if path.startswith(("\\", "/")):
        return False
    if os.path.isabs(path):
        return False
    if ":" in path:
        return False
    parts = path.replace("\\", "/").split("/")
    return all(part and part not in (".", "..") for part in parts)


def check_dictionary_manifest(data_dir, errors):
    path = os.path.join(data_dir, "dictionary_manifest.json")
    try:
        with open(path, encoding="utf-8") as f:
            manifest = json.load(f)
    except Exception as e:
        errors.append(f"dictionary_manifest.json: cannot read manifest: {e}")
        return False

    if manifest.get("schema") != 1:
        errors.append("dictionary_manifest.json: unsupported schema")
        return False
    files = manifest.get("files")
    if not isinstance(files, list):
        errors.append("dictionary_manifest.json: files must be an array")
        return False

    roles = set()
    paths = set()
    for item in files:
        if not isinstance(item, dict):
            errors.append("dictionary_manifest.json: file entry must be object")
            return False
        role = item.get("role")
        rel = item.get("path")
        expected_size = item.get("size")
        expected_hash = item.get("sha256")
        if (not role or not rel or not isinstance(expected_size, int) or
                expected_size <= 0 or not expected_hash):
            errors.append("dictionary_manifest.json: file entry missing role/path/size/sha256")
            return False
        if role in roles:
            errors.append(f"dictionary_manifest.json: duplicate role: {role}")
            return False
        if rel.lower() in paths:
            errors.append(f"dictionary_manifest.json: duplicate path: {rel}")
            return False
        if not is_safe_manifest_path(rel):
            errors.append(f"dictionary_manifest.json: unsafe path: {rel}")
            return False
        if not isinstance(expected_hash, str) or len(expected_hash) != 64:
            errors.append(f"dictionary_manifest.json: invalid sha256 for {rel}")
            return False
        try:
            int(expected_hash, 16)
        except ValueError:
            errors.append(f"dictionary_manifest.json: invalid sha256 for {rel}")
            return False
        roles.add(role)
        paths.add(rel.lower())
        file_path = os.path.join(data_dir, rel)
        if not os.path.isfile(file_path):
            errors.append(f"dictionary_manifest.json: listed file missing: {rel}")
            return False
        actual_size = os.path.getsize(file_path)
        if actual_size != expected_size:
            errors.append(
                f"dictionary_manifest.json: size mismatch for {rel}: "
                f"expected {expected_size}, actual {actual_size}"
            )
            return False
        actual_hash = sha256_file(file_path)
        if actual_hash.lower() != expected_hash.lower():
            errors.append(f"dictionary_manifest.json: sha256 mismatch for {rel}")
            return False

    missing_roles = sorted(REQUIRED_MANIFEST_ROLES - roles)
    if missing_roles:
        errors.append(
            "dictionary_manifest.json: missing role(s): " + ", ".join(missing_roles)
        )
        return False
    return True


def verify(data_dir):
    """Run all checks. Return list of error strings (empty = pass)."""
    errors = []

    # 1. File existence and size
    for f in REQUIRED_FILES:
        check_file_exists(data_dir, f, errors)

    # If any files missing, can't proceed with format checks
    if errors:
        return errors

    # 2. Magic and version checks
    check_dictionary_manifest(data_dir, errors)
    dict_ver = check_dict_bin(data_dir, errors)
    idx_ver = check_dict_idx(data_dir, errors)
    check_spellings_bin(data_dir, errors)
    check_topn_bin(data_dir, errors)

    # 3. Version consistency
    check_version_consistency(dict_ver, idx_ver, errors)

    return errors


def main():
    parser = argparse.ArgumentParser(description="Verify data file integrity")
    parser.add_argument("--data-dir", required=True, help="Data directory to verify")
    args = parser.parse_args()

    if not os.path.isdir(args.data_dir):
        print(f"ERROR: Data directory not found: {args.data_dir}", file=sys.stderr)
        return 1

    errors = verify(args.data_dir)
    if errors:
        print(f"FAILED: {len(errors)} error(s):", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        return 1

    print("All data file checks PASSED.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
