#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
#
# Standalone dictionary preparation pipeline.
#
# Runs the full workflow: .zip extraction -> spelling algebra -> binary build
# Supports both pinyin and wubi86 dictionaries.
#
# Usage:
#   python scripts/prepare_dict.py --data-dir data/ --output dist/data/

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_TOOLS = os.path.join(ROOT, "data", "tools")
SCHEMAS = os.path.join(ROOT, "data", "schemas")
SCRIPTS = os.path.join(ROOT, "scripts")

MANIFEST_FILES = [
    ("pinyin_dict", "pinyin.dict.bin"),
    ("pinyin_idx", "pinyin.dict.idx"),
    ("pinyin_spellings", "pinyin.spellings.bin"),
    ("pinyin_topn", "pinyin.topn.bin"),
    ("wubi_dict", "wubi86.dict.bin"),
    ("wubi_idx", "wubi86.dict.idx"),
]

REQUIRED_MANIFEST_ROLES = {
    "pinyin_dict",
    "pinyin_idx",
    "pinyin_spellings",
    "pinyin_topn",
    "wubi_dict",
    "wubi_idx",
}


def find_source(data_dir: str, name: str) -> str | None:
    """Find .dict.db or .dict.db.zip for a dictionary name."""
    db = os.path.join(data_dir, f"{name}.dict.db")
    if os.path.isfile(db):
        return db
    zip_path = os.path.join(data_dir, f"{name}.dict.db.zip")
    if os.path.isfile(zip_path):
        return zip_path
    return None


def extract_zip(zip_path: str, work_dir: str) -> str:
    """Extract .dict.db.zip and return path to the .db file."""
    print(f"  Extracting {os.path.basename(zip_path)}...")
    with zipfile.ZipFile(zip_path, "r") as zf:
        names = zf.namelist()
        db_name = next((n for n in names if n.endswith(".dict.db")), None)
        if db_name is None and names:
            db_name = names[0]
        if db_name is None:
            raise RuntimeError(f"No .db file found in {zip_path}")
        zf.extractall(work_dir)
    return os.path.join(work_dir, db_name)


def run_spelling_algebra(db_path: str) -> None:
    """Regenerate spellings table from schema rules."""
    script = os.path.join(DATA_TOOLS, "spelling_algebra.py")
    schema = os.path.join(SCHEMAS, "pinyin.schema.yaml")
    print(f"  Running spelling algebra...")
    subprocess.run(
        [sys.executable, script, db_path, schema],
        check=True,
        capture_output=False,
    )


def run_build_binary(
    db_path: str,
    output_prefix: str,
    skip_idx: bool = False,
    dict_only: bool = False,
) -> None:
    """Convert .dict.db to binary mmap files."""
    script = os.path.join(DATA_TOOLS, "build_binary.py")
    cmd = [sys.executable, script, "--input", db_path, "--output", output_prefix]
    if dict_only:
        cmd.append("--dict-only")
    if skip_idx:
        cmd.append("--skip-idx")
    print(f"  Building binary dicts: {os.path.basename(output_prefix)}.*")
    subprocess.run(cmd, check=True, capture_output=False)


def run_build_short_cache(db_path: str, output_path: str) -> None:
    """Build short code cache (topn) for fast path."""
    script = os.path.join(SCRIPTS, "build_short_cache.py")
    print(f"  Building short code cache...")
    subprocess.run(
        [sys.executable, script, "--input", db_path, "--output", output_path],
        check=True,
        capture_output=False,
    )


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_dictionary_manifest(output_dir: str) -> str:
    """Write dictionary_manifest.json last, after all large files are stable."""
    files = []
    for role, filename in MANIFEST_FILES:
        path = os.path.join(output_dir, filename)
        if not os.path.isfile(path):
            continue
        files.append({
            "role": role,
            "path": filename,
            "size": os.path.getsize(path),
            "sha256": sha256_file(path),
            "required": True,
        })

    roles = {item["role"] for item in files}
    missing = sorted(REQUIRED_MANIFEST_ROLES - roles)
    if missing:
        raise RuntimeError(
            "Missing required dictionary manifest role(s): " + ", ".join(missing)
        )

    manifest = {
        "schema": 1,
        "generation": datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
        "files": files,
    }

    path = os.path.join(output_dir, "dictionary_manifest.json")
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp, path)
    return path


def prepare_dict(
    data_dir: str,
    output_dir: str,
) -> list[str]:
    """Run full dictionary preparation pipeline.

    Returns list of generated files (for verification).
    """
    os.makedirs(output_dir, exist_ok=True)
    generated = []

    # ---- Pinyin dictionary ----
    print("--- Pinyin dictionary ---")
    src = find_source(data_dir, "pinyin")
    if src is None:
        print("  WARNING: pinyin.dict.db(.zip) not found, skipping pinyin dictionary")
    else:
        with tempfile.TemporaryDirectory(prefix="cxxime_prep_") as tmpdir:
            # Step 1: get .db file (extract .zip or copy .db to avoid mutating source)
            if src.endswith(".zip"):
                db_path = extract_zip(src, tmpdir)
            else:
                db_copy = os.path.join(tmpdir, os.path.basename(src))
                shutil.copy2(src, db_copy)
                db_path = db_copy

            # Step 2: spelling algebra (mutates .db — safe on copy)
            run_spelling_algebra(db_path)

            # Step 3: build binary (.bin, .idx, .spellings.bin)
            output_prefix = os.path.join(output_dir, "pinyin")
            run_build_binary(db_path, output_prefix)
            generated.extend([
                output_prefix + ".dict.bin",
                output_prefix + ".dict.idx",
                output_prefix + ".spellings.bin",
            ])

            # Step 4: short code cache (.topn.bin)
            topn_path = os.path.join(output_dir, "pinyin.topn.bin")
            run_build_short_cache(db_path, topn_path)
            generated.append(topn_path)

    # ---- Wubi86 dictionary ----
    print("--- Wubi86 dictionary ---")
    src = find_source(data_dir, "wubi86")
    if src is None:
        raise RuntimeError("wubi86.dict.db(.zip) not found")
    with tempfile.TemporaryDirectory(prefix="cxxime_prep_") as tmpdir:
        if src.endswith(".zip"):
            db_path = extract_zip(src, tmpdir)
        else:
            db_copy = os.path.join(tmpdir, os.path.basename(src))
            shutil.copy2(src, db_copy)
            db_path = db_copy

        output_prefix = os.path.join(output_dir, "wubi86")
        run_build_binary(db_path, output_prefix, dict_only=True)
        generated.extend([
            output_prefix + ".dict.bin",
            output_prefix + ".dict.idx",
        ])

    manifest_path = write_dictionary_manifest(output_dir)
    generated.append(manifest_path)
    return generated


def main():
    parser = argparse.ArgumentParser(
        description="Prepare dictionary binaries for CxxIME packaging"
    )
    parser.add_argument(
        "--data-dir", required=True,
        help="Source data directory (contains .dict.db or .dict.db.zip)",
    )
    parser.add_argument(
        "--output-dir", required=True,
        help="Output directory for binary dictionary files",
    )
    args = parser.parse_args()

    data_dir = os.path.abspath(args.data_dir)
    output_dir = os.path.abspath(args.output_dir)

    if not os.path.isdir(data_dir):
        print(f"ERROR: data directory not found: {data_dir}", file=sys.stderr)
        return 1

    print(f"Data source: {data_dir}")
    print(f"Output:      {output_dir}")
    print()

    try:
        generated = prepare_dict(data_dir, output_dir)
    except subprocess.CalledProcessError as e:
        print(f"ERROR: subprocess failed: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    print()
    print(f"Done. {len(generated)} file(s) generated:")
    for path in generated:
        size_mb = os.path.getsize(path) / (1024 * 1024)
        print(f"  {os.path.basename(path):30s} {size_mb:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
