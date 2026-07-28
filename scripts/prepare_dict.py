#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
#
# Standalone dictionary preparation pipeline.
#
# Runs the full workflow: .zip extraction -> spelling algebra -> binary build.
# Supports both pinyin and wubi86 dictionaries.
#
# Usage:
#   python scripts/prepare_dict.py --data-dir data/ --output-dir dist/data/ \
#       --topn-builder build/tools/topn_index/Release/topn_builder.exe

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import shutil
import struct
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


def prepare_source_copy(src: str, work_dir: str) -> str:
    """Return a writable dictionary DB path in work_dir."""
    if src.endswith(".zip"):
        return extract_zip(src, work_dir)

    db_copy = os.path.join(work_dir, os.path.basename(src))
    shutil.copy2(src, db_copy)
    return db_copy


def run_spelling_algebra(db_path: str) -> None:
    """Regenerate spellings table from schema rules."""
    script = os.path.join(DATA_TOOLS, "spelling_algebra.py")
    schema = os.path.join(SCHEMAS, "pinyin.schema.yaml")
    print("  Running spelling algebra...")
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
    """Build the v1 intermediate Top-N index."""
    script = os.path.join(SCRIPTS, "build_short_cache.py")
    print("  Building Top-N index intermediate...")
    subprocess.run(
        [sys.executable, script, "--input", db_path, "--output", output_path],
        check=True,
        capture_output=False,
    )


def finalize_topn_index(output_dir: str, topn_builder: str) -> str:
    """Convert a v1 Top-N intermediate to the runtime DAT-16 format in place."""
    topn_path = os.path.join(output_dir, "pinyin.topn.bin")
    if not os.path.isfile(topn_path):
        raise RuntimeError(f"Top-N intermediate not found: {topn_path}")
    if not os.path.isfile(topn_builder):
        raise RuntimeError(f"topn_builder not found: {topn_builder}")

    with open(topn_path, "rb") as f:
        header = f.read(20)
    if len(header) < 20:
        raise RuntimeError("pinyin.topn.bin is too small")
    magic = header[:8]
    if magic == b"CXTOPN\x01\x00":
        print("  Converting Top-N index to DAT-16...")
        subprocess.run(
            [topn_builder, "--input", topn_path, "--output", topn_path, "--format", "dat16"],
            check=True,
            capture_output=False,
        )
        with open(topn_path, "rb") as f:
            header = f.read(20)
        magic = header[:8]

    if magic != b"CXTOPN\x02\x00" or len(header) < 20:
        raise RuntimeError("pinyin.topn.bin is not a CXTOPN v2 file")
    version, header_size, layout = struct.unpack_from("<III", header, 8)
    if version != 2 or header_size != 80 or layout != 2:
        raise RuntimeError(
            "pinyin.topn.bin is not the required DAT-16 layout "
            f"(version={version}, header={header_size}, layout={layout})"
        )
    return topn_path

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


def prepare_pinyin_dictionary(data_dir: str, output_dir: str) -> list[str]:
    """Prepare pinyin binary dictionary files."""
    print("--- Pinyin dictionary ---")
    src = find_source(data_dir, "pinyin")
    if src is None:
        print("  WARNING: pinyin.dict.db(.zip) not found, skipping pinyin dictionary")
        return []

    generated = []
    with tempfile.TemporaryDirectory(prefix="cxxime_prep_pinyin_") as tmpdir:
        db_path = prepare_source_copy(src, tmpdir)
        run_spelling_algebra(db_path)

        output_prefix = os.path.join(output_dir, "pinyin")
        run_build_binary(db_path, output_prefix)
        generated.extend([
            output_prefix + ".dict.bin",
            output_prefix + ".dict.idx",
            output_prefix + ".spellings.bin",
        ])

        topn_path = os.path.join(output_dir, "pinyin.topn.bin")
        run_build_short_cache(db_path, topn_path)
        generated.append(topn_path)

    return generated


def prepare_wubi_dictionary(data_dir: str, output_dir: str) -> list[str]:
    """Prepare wubi86 binary dictionary files."""
    print("--- Wubi86 dictionary ---")
    src = find_source(data_dir, "wubi86")
    if src is None:
        raise RuntimeError("wubi86.dict.db(.zip) not found")

    generated = []
    with tempfile.TemporaryDirectory(prefix="cxxime_prep_wubi86_") as tmpdir:
        db_path = prepare_source_copy(src, tmpdir)
        output_prefix = os.path.join(output_dir, "wubi86")
        run_build_binary(db_path, output_prefix, dict_only=True)
        generated.extend([
            output_prefix + ".dict.bin",
            output_prefix + ".dict.idx",
        ])

    return generated


def prepare_dict(
    data_dir: str,
    output_dir: str,
    workers: int = 2,
    topn_builder: str | None = None,
    defer_topn_conversion: bool = False,
) -> list[str]:
    """Run dictionary preparation, using separate workers for pinyin and wubi86."""
    os.makedirs(output_dir, exist_ok=True)
    workers = max(1, min(workers, 2))
    tasks = [
        ("pinyin", prepare_pinyin_dictionary),
        ("wubi86", prepare_wubi_dictionary),
    ]
    generated = []

    if workers == 1:
        for _, task in tasks:
            generated.extend(task(data_dir, output_dir))
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            futures = [
                (name, executor.submit(task, data_dir, output_dir))
                for name, task in tasks
            ]
            for name, future in futures:
                try:
                    generated.extend(future.result())
                except Exception as e:
                    raise RuntimeError(f"{name} dictionary preparation failed: {e}") from e

    if defer_topn_conversion:
        return generated
    if not topn_builder:
        raise RuntimeError("topn_builder is required to produce the DAT-16 runtime index")
    finalize_topn_index(output_dir, os.path.abspath(topn_builder))

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
    parser.add_argument(
        "--workers", type=int, default=min(2, os.cpu_count() or 1),
        help="Dictionary worker count (default: 2, capped by available dictionaries)",
    )
    parser.add_argument(
        "--topn-builder", required=True,
        help="Path to the x64 topn_builder executable",
    )
    args = parser.parse_args()

    data_dir = os.path.abspath(args.data_dir)
    output_dir = os.path.abspath(args.output_dir)

    if not os.path.isdir(data_dir):
        print(f"ERROR: data directory not found: {data_dir}", file=sys.stderr)
        return 1
    if args.workers < 1:
        print("ERROR: --workers must be >= 1", file=sys.stderr)
        return 1

    print(f"Data source: {data_dir}")
    print(f"Output:      {output_dir}")
    print()

    try:
        generated = prepare_dict(
            data_dir,
            output_dir,
            workers=args.workers,
            topn_builder=args.topn_builder,
        )
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
